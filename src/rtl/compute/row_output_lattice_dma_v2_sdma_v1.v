`timescale 1ns/1ps
`default_nettype none
// Two owned output rows, each <=320 pixels x16 channels, in four TDP banks.
// Capture the next row while a previous complete row drains as NCHW bursts.
// Pixel/channel ordering, row identity and tile-last are checked before store.
// All output is provisional until frame done; cancel drains accepted traffic.
module row_output_lattice_dma_v2_sdma_v1 #(
 parameter[31:0]ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000
)(
 input wire clk,rst_n,cancel,clear,cmd_valid,output wire cmd_ready,
 input wire[31:0]destination_address,destination_bytes,cmd_tag,timeout_cycles,
 input wire[8:0]width,height,input wire[4:0]channels,
 input wire[8:0]destination_width,destination_height,destination_channels,channel_start,
 input wire lattice_two,phase_x,phase_y,
 input wire in_valid,output wire in_ready,input wire[63:0]in_data,
 input wire[6:0]in_row,input wire[4:0]in_column,input wire[31:0]in_tag,in_frame,input wire in_last,
 output reg done,cancelled,output reg[7:0]fault,output wire busy,quiescent,needs_fabric_reset,
 output wire[7:0]debug_state,output reg[31:0]accepted_beats,committed_values,elapsed_cycles,
 output wire[189:0]service_request,input wire[225:0]service_response
);
 localparam IDLE=0,ACTIVE=1,DRAIN=2,FAILED=3,EXTENT=4,VALIDATE=5;
 localparam W_IDLE=0,W_COMMAND=1,W_STREAM=2,W_RECEIPT=3,W_ADDRESS=4,W_BOUND=5;
 reg[2:0]state;reg[1:0]ready_rows;reg[2:0]wstate;reg[8:0]cw,ch,cap_x,cap_y,head_y;
 reg[8:0]dw,dh,cstart;reg lattice,xphase,yphase;reg[1:0]lane_offset;
 reg[7:0]read_select,pack_strb;reg[3:0]read_mask;
 reg[4:0]cn,column,channel;reg capture_done,dma_inflight,was_cancel,drain_stuck;
 reg[31:0]base,span,tag,watchdog,drain_cycles;reg[18:0]plane_bytes;
 reg[6:0]words,issued;reg[31:0]write_address;
 reg qvalid,pack_valid;reg[63:0]pack;
 wire stop=cancel || fault!=0 || state==DRAIN || state==FAILED;
 wire dr,dd,dc,dq,dp,dwr;wire[7:0]df;wire[31:0]dt;
 wire[18:0]input_plane=destination_width*destination_height*2;
 reg[27:0]input_bytes;reg[8:0]allocation_channels;reg command_bad;
 wire[9:0]last_x=lattice_two?(width-1)*2+phase_x:width-1,last_y=lattice_two?(height-1)*2+phase_y:height-1;
 wire[32:0]allocation_end={1'b0,destination_address}+{1'b0,destination_bytes};
 wire invalid=timeout_cycles==0 || width==0 || width>320 || height==0 || height>256 || channels==0 || channels>16 ||
  destination_width==0 || destination_width>320 || destination_height==0 || destination_height>256 || destination_channels==0 || destination_channels>384 ||
  {1'b0,channel_start}+channels>{1'b0,destination_channels} || last_x>=destination_width || last_y>=destination_height || (!lattice_two && (phase_x || phase_y)) ||
  destination_address[2:0]!=0 || destination_bytes[2:0]!=0 ||
  destination_address<ADDRESS_MIN || allocation_end>{1'b0,ADDRESS_END} || allocation_end[32];
 assign cmd_ready=rst_n && state==IDLE && !stop && dq;
 assign busy=state!=IDLE && state!=FAILED;
 assign quiescent=dq && (state==IDLE || state==FAILED);
 assign needs_fabric_reset=drain_stuck || df==4 || df==5;
 assign debug_state={2'd0,wstate,state};
 assign in_ready=rst_n && state==ACTIVE && !stop && !capture_done && !ready_rows[cap_y[0]];
 wire take=in_valid && in_ready;
 wire end_column={1'b0,column}+6'd4>={1'b0,cn};
 wire bad_packet=in_frame!=tag || in_tag[31:16]!={7'd0,cap_y} || in_tag[2:0]!=0 || in_row>7 ||
  {1'b0,in_tag[15:0]}+{10'd0,in_row}!={8'd0,cap_x} || in_column!=column ||
  in_last!=((in_row==7 || cap_x+1==cw) && end_column);
 wire[9:0]capture_address={cap_y[0],9'd0}+{5'd0,column[4:2]}*10'd80+{3'd0,cap_x[8:2]};
 wire advance=!pack_valid || dwr;
 wire issue=state==ACTIVE && wstate==W_STREAM && !stop && advance && issued<words;
 wire[63:0]q[0:3];wire[63:0]assembled;wire[7:0]assembled_strb;
 // issued*4+lane-offset is in[-3,511]; a signed10-bit value
 // exactly represents the old integer algebra. Legal normal/lattice lanes
 // never share a bank in one read, so parallel selection replaces a cascade.
 reg[9:0]read_base;
 wire[39:0]read_addresses;wire[7:0]next_select;wire[3:0]next_mask;
 wire[8:0]read_x[0:3];wire[9:0]lane_address[0:3];
 genvar read_lane,read_bank;
 generate for(read_lane=0;read_lane<4;read_lane=read_lane+1)begin:READ_LANE
  localparam signed[9:0]LANE=read_lane;
  wire signed[9:0]delta=$signed({1'b0,issued,2'b00})+LANE-$signed({8'd0,lane_offset});
  assign read_x[read_lane]=lattice?{1'b0,delta[8:1]}:delta[8:0];
  assign next_mask[read_lane]=!delta[9] && (!lattice || !delta[0]) && read_x[read_lane]<cw;
  assign next_select[read_lane*2+:2]=next_mask[read_lane]?read_x[read_lane][1:0]:2'd0;
  assign lane_address[read_lane]=read_base+{3'd0,read_x[read_lane][8:2]};
 end
 for(read_bank=0;read_bank<4;read_bank=read_bank+1)begin:READ_BANK
  wire[9:0]selected[0:3];
  for(genvar pick=0;pick<4;pick=pick+1)begin:PICK
   assign selected[pick]=(next_mask[pick] && read_x[pick][1:0]==read_bank)?lane_address[pick]:10'd0;
  end
  assign read_addresses[read_bank*10+:10]=selected[0]|selected[1]|selected[2]|selected[3];
 end endgenerate
 genvar b;
 generate for(b=0;b<4;b=b+1)begin:BANK
  (* ram_style="block" *)reg[63:0]memory[0:1023];reg[63:0]read_q;
  always @(posedge clk)if(take && !bad_packet && cap_x[1:0]==b)memory[capture_address]<=in_data;
  always @(posedge clk)if(issue)read_q<=memory[read_addresses[b*10+:10]];
  assign q[b]=read_q;
  assign assembled[b*16+:16]=q[read_select[b*2+:2]][{channel[1:0],4'd0}+:16];
  assign assembled_strb[b*2+:2]={2{read_mask[b]}};
 end endgenerate
 (* use_dsp="yes" *)wire[27:0]channel_bytes;
 (* use_dsp="yes" *)wire[18:0]row_bytes;
 wire[8:0]actual_y=lattice?head_y*2+yphase:head_y;
 assign channel_bytes=({1'b0,cstart}+channel)*plane_bytes;assign row_bytes=actual_y*dw*2;
 reg[9:0]span_pixels;reg[11:0]row_extent_plus7;
 wire[9:0]command_span_pixels=lattice_two?({1'b0,width}*2-1'b1):{1'b0,width};
 reg[27:0]channel_offset;
 reg[18:0]row_offset;
 reg[32:0]row_destination,allocation_limit;
 wire[32:0]row_end_aligned=(row_destination+{21'd0,row_extent_plus7})&33'h1fffffff8;
 wire[9:0]physical_pixels=span_pixels+row_destination[2:1]+3;
 shared_dma_client_v2 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) writer(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.cmd_valid(state==ACTIVE && wstate==W_COMMAND && !stop),.cmd_ready(dr),.cmd_write(1'b1),
  .cmd_address(write_address),.cmd_beats({9'd0,words}),.wr_strb(pack_strb),.wr_data(pack),.wr_valid(state==ACTIVE && wstate==W_STREAM && pack_valid && !stop),.wr_ready(dwr),
  .rd_data(),.rd_valid(),.rd_ready(1'b0),.done(dd),.cancelled(dc),.fault(df),.quiescent(dq),.progress(dp),.transferred(dt),
  .m_axi_araddr(),.m_axi_arlen(),.m_axi_arsize(),.m_axi_arburst(),.m_axi_arvalid(),.m_axi_arready(1'b0),.m_axi_rdata(64'd0),.m_axi_rresp(2'd0),.m_axi_rlast(1'b0),.m_axi_rvalid(1'b0),.m_axi_rready(),
  .m_axi_awaddr(),.m_axi_awlen(),.m_axi_awsize(),.m_axi_awburst(),.m_axi_awvalid(),.m_axi_awready(1'b0),.m_axi_wdata(),.m_axi_wstrb(),.m_axi_wlast(),.m_axi_wvalid(),.m_axi_wready(1'b0),.m_axi_bresp(2'd0),.m_axi_bvalid(1'b0),.m_axi_bready(),
  .service_request(service_request),.service_response(service_response));
 reg[6:0]sent;
 always @(posedge clk)begin
  if(!rst_n)begin
   span_pixels<=0;row_extent_plus7<=0;
   read_base<=0;input_bytes<=0;allocation_channels<=0;command_bad<=0;
   channel_offset<=0;row_offset<=0;row_destination<=0;allocation_limit<=0;
   dw<=0;dh<=0;cstart<=0;lattice<=0;xphase<=0;yphase<=0;lane_offset<=0;read_select<=0;read_mask<=0;pack_strb<=0;
   state<=IDLE;wstate<=W_IDLE;ready_rows<=0;cw<=0;ch<=0;cn<=0;cap_x<=0;cap_y<=0;head_y<=0;column<=0;channel<=0;capture_done<=0;dma_inflight<=0;
   was_cancel<=0;drain_stuck<=0;base<=0;span<=0;tag<=0;watchdog<=0;drain_cycles<=0;plane_bytes<=0;words<=0;issued<=0;write_address<=0;qvalid<=0;pack_valid<=0;pack<=0;sent<=0;
   done<=0;cancelled<=0;fault<=0;accepted_beats<=0;committed_values<=0;elapsed_cycles<=0;
  end else begin
   done<=0;cancelled<=0;if(busy)elapsed_cycles<=elapsed_cycles+1'b1;
   if(cancel && state!=DRAIN && state!=FAILED)begin state<=DRAIN;was_cancel<=1;drain_cycles<=0;ready_rows<=0;end
   else if(state==ACTIVE && elapsed_cycles>=watchdog)begin fault<=8'hc0;state<=DRAIN;drain_cycles<=0;ready_rows<=0;end
   else if(state==ACTIVE && dma_inflight && df)begin fault<=8'ha0|df;state<=DRAIN;drain_cycles<=0;ready_rows<=0;end
   else if(take && bad_packet)begin fault<=8'h91;state<=DRAIN;drain_cycles<=0;ready_rows<=0;end
   else case(state)
    IDLE:if(cmd_valid && cmd_ready)begin
     span_pixels<=command_span_pixels;row_extent_plus7<={command_span_pixels,1'b0}+12'd7;
     command_bad<=invalid;allocation_channels<=destination_channels;
     allocation_limit<=allocation_end;cw<=width;ch<=height;cn<=channels;base<=destination_address;span<=destination_bytes;tag<=cmd_tag;watchdog<=timeout_cycles;plane_bytes<=input_plane;words<=0;
     dw<=destination_width;dh<=destination_height;cstart<=channel_start;lattice<=lattice_two;xphase<=phase_x;yphase<=phase_y;lane_offset<=0;read_select<=0;read_mask<=0;pack_strb<=0;
     cap_x<=0;cap_y<=0;head_y<=0;column<=0;channel<=0;capture_done<=0;ready_rows<=0;wstate<=W_IDLE;qvalid<=0;pack_valid<=0;issued<=0;sent<=0;dma_inflight<=0;
     was_cancel<=0;drain_stuck<=0;drain_cycles<=0;elapsed_cycles<=0;accepted_beats<=0;committed_values<=0;
     state<=EXTENT;
    end
    EXTENT:begin input_bytes<=plane_bytes*allocation_channels;state<=VALIDATE;end
    VALIDATE:begin
     if(command_bad || span<input_bytes)begin fault<=8'h90;state<=FAILED;end else state<=ACTIVE;
    end
    ACTIVE:begin
     if(take)begin
      accepted_beats<=accepted_beats+1'b1;
      if(end_column)begin
       column<=0;
       if(cap_x+1==cw)begin cap_x<=0;ready_rows[cap_y[0]]<=1;
        if(cap_y+1==ch)capture_done<=1;else cap_y<=cap_y+1'b1;
       end else cap_x<=cap_x+1'b1;
      end else column<=column+5'd4;
     end
     case(wstate)
      // Separate multiply, address sum and bounds check. Row ownership stays
      // held through every stage; cancellation still uses the outer drain.
      W_IDLE:if(ready_rows[head_y[0]])begin
       channel_offset<=channel_bytes;row_offset<=row_bytes;wstate<=W_ADDRESS;
      end
      W_ADDRESS:begin
       row_destination<={1'b0,base}+{5'd0,channel_offset}+{14'd0,row_offset}+(lattice?{31'd0,xphase,1'b0}:33'd0);
       wstate<=W_BOUND;
      end
      W_BOUND:begin
       read_base<={head_y[0],9'd0}+{5'd0,channel[4:2]}*10'd80;
       if(row_destination[32] || row_end_aligned>allocation_limit)begin fault<=8'h92;state<=DRAIN;drain_cycles<=0;ready_rows<=0;end
       else begin write_address<={row_destination[31:3],3'b000};lane_offset<=row_destination[2:1];words<=physical_pixels[8:2];issued<=0;sent<=0;qvalid<=0;pack_valid<=0;wstate<=W_COMMAND;end
      end
      W_COMMAND:if(dr)begin wstate<=W_STREAM;dma_inflight<=1;end
      W_STREAM:begin
       if(advance)begin qvalid<=issue;pack_valid<=qvalid;if(qvalid)begin pack<=assembled;pack_strb<=assembled_strb;end end
       if(issue)begin issued<=issued+1'b1;read_select<=next_select;read_mask<=next_mask;end
       if(pack_valid && dwr)begin sent<=sent+1'b1;if(sent+1==words)wstate<=W_RECEIPT;end
      end
      W_RECEIPT:if(dd)begin
       if(dc || dt!={25'd0,words})begin fault<=8'hb0;state<=DRAIN;drain_cycles<=0;ready_rows<=0;end
       else begin
        dma_inflight<=0;committed_values<=committed_values+cw;wstate<=W_IDLE;
        if(channel+1==cn)begin channel<=0;ready_rows[head_y[0]]<=0;
         if(head_y+1==ch)begin
          if(!capture_done)begin fault<=8'hb1;state<=DRAIN;drain_cycles<=0;end
          else begin done<=1;state<=IDLE;end
         end else head_y<=head_y+1'b1;
        end else channel<=channel+1'b1;
       end
      end
     endcase
    end
    DRAIN:begin
     ready_rows<=0;qvalid<=0;pack_valid<=0;
     if(drain_cycles!=32'hffffffff)drain_cycles<=drain_cycles+1'b1;
     if(drain_cycles>1 && dq)begin state<=FAILED;cancelled<=was_cancel;dma_inflight<=0;end
     else if(drain_cycles>=(watchdog<32?32:watchdog))drain_stuck<=1;
    end
    FAILED:if(clear && dq && !needs_fabric_reset && !cancel)begin fault<=0;state<=IDLE;wstate<=W_IDLE;ready_rows<=0;end
   endcase
  end
 end
endmodule
`default_nettype wire
