`timescale 1ns/1ps
`default_nettype none
// Exact ADD/quarter RESIZE. Two source rows rotate rather than being reloaded
// for every output row. Two output values use one read of each four-bank row.
// RAM payload and external descriptor/leases remain the production v111 shape.
module r14_tensor_dma_v1 #(
 parameter[31:0]ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000
)(
 input wire clk,rst_n,cancel,clear,cmd_valid,output wire cmd_ready,
 input wire[1:0]cmd_op,input wire[9:0]cmd_width,cmd_height,input wire[7:0]cmd_channels,
 input wire cmd_a_wide,cmd_b_wide,cmd_destination_wide,cmd_relu_a,cmd_relu_b,
 input wire[4:0]cmd_fraction_a,cmd_fraction_b,cmd_target_fraction,
 input wire[31:0]source_a,source_b,destination,source_a_bytes,source_b_bytes,destination_bytes,timeout_cycles,
 output reg done,cancelled,output reg[15:0]fault,output wire busy,quiescent,needs_fabric_reset,
 output reg[31:0]elapsed_cycles,read_beats,write_beats,output_values,
 output wire[379:0]service_request,input wire[451:0]service_response
);
 localparam IDLE=0,READ_A_CMD=1,READ_A=2,READ_B_CMD=3,READ_B=4,EXEC=5,
            WRITE_CMD=6,WRITE_ROW=7,NEXT_ROW=8,DRAIN=9,FAILED=10,
            WRITE_ADDR=13,WRITE_CAPTURE=14,WRITE_PREP=15,READ_ADDR=16,READ_END=17,READ_CHECK=18;
 reg[4:0]state;reg[1:0]op;reg[9:0]width,height,out_width,out_height,y,input_y;
 reg[7:0]channels,channel;reg a_wide,b_wide,relu_a,relu_b;
 reg[4:0]fa,fb;reg[31:0]a_row,b_row,a_plane,d_row,d_base,a_limit,b_limit,d_limit;
 reg[31:0]a_stride,b_stride,d_stride,a_plane_bytes,watchdog,drain_cycles;
 reg[8:0]load_index,write_index,received_groups;reg[9:0]fetch_x,raw_x;
 reg fetch_valid,raw_valid,was_cancel,drain_stuck,read_b_phase,bad_read_span_q;
 reg top_b,bottom_same;
 reg[31:0]read_address_q;reg[15:0]row_read_beats_q;reg[32:0]read_end_q;
 reg[63:0]fetch_a,fetch_b,fetch_c,fetch_d;reg[3:0]fetch_dx;
 (* ram_style="block" *) reg[31:0]a0[0:127],a1[0:127],a2[0:127],a3[0:127];
 (* ram_style="block" *) reg[31:0]b0[0:127],b1[0:127],b2[0:127],b3[0:127];
 (* ram_style="block" *) reg[63:0]output_row[0:159];
 reg[63:0]output_quad,output_beat;
 wire rr,rd,rc,rq,rp,rv,wr,wd,wc,wq,wp,ww;wire[7:0]rf,wf;
 wire[31:0]rt,wt;wire[63:0]rdata;
 wire[1:0]alu_ready_lane,alu_valid_lane,alu_error;wire[63:0]alu_number;
 wire alu_ready=&alu_ready_lane,alu_valid=&alu_valid_lane;
 wire timeout_q;
 r14_deadline32_v8 deadline(.clk(clk),.rst_n(rst_n),.busy(busy),.elapsed(elapsed_cycles),.limit(watchdog),.expired(timeout_q));
 wire timed_out=busy && state!=DRAIN && timeout_q;
 wire stop=cancel || state==DRAIN || state==FAILED || timed_out || fault!=0;
 wire reading_b=read_b_phase;
 wire[31:0]read_address=reading_b?(op==2 && input_y+1<height?a_row+a_stride:(op==2?a_row:b_row)):a_row;
 wire read_wide=reading_b?(op==2?a_wide:b_wide):a_wide;
 wire[15:0]row_read_beats=read_wide?({6'd0,width}>>1):({6'd0,width}>>2);
 wire[15:0]row_write_beats={6'd0,out_width}>>1;
 wire read_cmd=(state==READ_A_CMD || state==READ_B_CMD) && !stop;
 wire read_take=rv && (state==READ_A || state==READ_B) && !stop;
 wire write_cmd=state==WRITE_CMD && !stop;
 wire write_valid=state==WRITE_ROW && !stop && write_index<row_write_beats;
 // State-exclusive prefetches share one physical synchronous RAM read.
 // Three separate if/read sites caused output_row to expand into registers.
 wire output_fetch=state==WRITE_ADDR || (state==WRITE_CAPTURE && row_write_beats>1) ||
  (state==WRITE_ROW && write_valid && ww && write_index+2<row_write_beats);
 wire[8:0]output_read_address=state==WRITE_ADDR?9'd0:
  state==WRITE_CAPTURE?9'd1:write_index+9'd2;
 wire[32:0]write_end={1'b0,d_row}+({17'd0,row_write_beats}<<3);
 wire bad_read_span=read_address_q<ADDRESS_MIN || read_end_q[32] || read_end_q>{1'b0,(reading_b && op==1?b_limit:a_limit)};
 wire bad_write_span=d_row<d_base || write_end[32] || write_end>{1'b0,d_limit};
 wire[32:0]a_end={1'b0,source_a}+{1'b0,source_a_bytes},b_end={1'b0,source_b}+{1'b0,source_b_bytes},d_end={1'b0,destination}+{1'b0,destination_bytes};
 wire bad_command=(cmd_op!=1 && cmd_op!=2) || cmd_width<4 || cmd_width>320 || cmd_width[1:0]!=0 ||
  cmd_height==0 || cmd_height>256 || cmd_channels==0 || cmd_channels>32 ||
  (cmd_op==2 && (cmd_width>160 || cmd_height>128)) || !cmd_destination_wide ||
  cmd_fraction_a<9 || cmd_fraction_a>16 || cmd_target_fraction<11 || cmd_target_fraction>14 ||
  (cmd_op==1 && (cmd_fraction_b<9 || cmd_fraction_b>16)) || (cmd_op==2 && cmd_fraction_a>12) ||
  source_a[2:0]!=0 || destination[2:0]!=0 || source_a_bytes==0 || destination_bytes==0 ||
  source_a<ADDRESS_MIN || destination<ADDRESS_MIN || a_end>{1'b0,ADDRESS_END} || d_end>{1'b0,ADDRESS_END} ||
  a_end[32] || d_end[32] || timeout_cycles==0 ||
  ({1'b0,source_a}<d_end && {1'b0,destination}<a_end) ||
  (cmd_op==1 && (source_b[2:0]!=0 || source_b_bytes==0 || source_b<ADDRESS_MIN || b_end>{1'b0,ADDRESS_END} || b_end[32] ||
   ({1'b0,source_b}<d_end && {1'b0,destination}<b_end)));
 assign cmd_ready=rst_n && state==IDLE && rq && wq && !cancel && !fault;
 assign busy=state!=IDLE && state!=FAILED;
 assign quiescent=(state==IDLE || state==FAILED) && rq && wq && !fetch_valid && !raw_valid;
 assign needs_fabric_reset=drain_stuck || rf==4 || rf==5 || wf==4 || wf==5;

 // Consecutive ADD values need two bank addresses; a RESIZE pair needs at
 // most three adjacent raw values. Clamped values have identical addresses.
 wire[9:0]window_base=op==2?(fetch_x==0?10'd0:(fetch_x-1)>>1):fetch_x;
 wire[9:0]index0=window_base;
 wire[9:0]index1=window_base+1>=width?width-1'b1:window_base+1'b1;
 wire[9:0]index2=window_base+2>=width?width-1'b1:window_base+2'd2;
 reg[6:0]bank_address[0:3];integer k;reg[9:0]ix;
 always @*begin
  for(k=0;k<4;k=k+1)bank_address[k]=0;
  bank_address[index0[1:0]]=index0[8:2];bank_address[index1[1:0]]=index1[8:2];
  if(op==2)bank_address[index2[1:0]]=index2[8:2];
 end
 reg[31:0]bank_a0,bank_a1,bank_a2,bank_a3,bank_b0,bank_b1,bank_b2,bank_b3;
 wire[6:0]load_address=read_wide?load_index[7:1]:load_index[6:0];
 wire load_first=read_take && (!read_wide || !load_index[0]);
 wire load_second=read_take && (!read_wide || load_index[0]);
 wire fill_b=op==2?(read_b_phase?!top_b:top_b):read_b_phase;
 wire[31:0]load0=read_wide?rdata[31:0]:{{16{rdata[15]}},rdata[15:0]};
 wire[31:0]load1=read_wide?rdata[63:32]:{{16{rdata[31]}},rdata[31:16]};
 wire[31:0]load2=read_wide?rdata[31:0]:{{16{rdata[47]}},rdata[47:32]};
 wire[31:0]load3=read_wide?rdata[63:32]:{{16{rdata[63]}},rdata[63:48]};
 wire advance=state==EXEC && !stop && (!fetch_valid || alu_ready);
 wire issue=advance && fetch_x<out_width;
 always @(posedge clk)begin
  if(load_first && !fill_b)begin a0[load_address]<=load0;a1[load_address]<=load1;end
  if(load_second && !fill_b)begin a2[load_address]<=load2;a3[load_address]<=load3;end
  if(load_first && fill_b)begin b0[load_address]<=load0;b1[load_address]<=load1;end
  if(load_second && fill_b)begin b2[load_address]<=load2;b3[load_address]<=load3;end
  if(issue)begin
   bank_a0<=a0[bank_address[0]];bank_a1<=a1[bank_address[1]];bank_a2<=a2[bank_address[2]];bank_a3<=a3[bank_address[3]];
   bank_b0<=b0[bank_address[0]];bank_b1<=b1[bank_address[1]];bank_b2<=b2[bank_address[2]];bank_b3<=b3[bank_address[3]];
  end
  if(alu_valid && state==EXEC && !stop && !(|alu_error))output_row[received_groups]<=alu_number;
  if(output_fetch)output_quad<=output_row[output_read_address];
 end
 function[31:0]pick;input[1:0]bank;input[31:0]v0,v1,v2,v3;begin
  case(bank)0:pick=v0;1:pick=v1;2:pick=v2;3:pick=v3;endcase end endfunction
 genvar lane;
 generate for(lane=0;lane<2;lane=lane+1)begin:PAIR
  wire[9:0]x=raw_x+lane;
  wire[9:0]lo=op==2?(x==0?10'd0:(x-1)>>1):x;
  wire[9:0]hi=lo+1>=width?width-1'b1:lo+1'b1;
  wire[31:0]alo=pick(lo[1:0],bank_a0,bank_a1,bank_a2,bank_a3);
  wire[31:0]ahi=pick(hi[1:0],bank_a0,bank_a1,bank_a2,bank_a3);
  wire[31:0]blo=pick(lo[1:0],bank_b0,bank_b1,bank_b2,bank_b3);
  wire[31:0]bhi=pick(hi[1:0],bank_b0,bank_b1,bank_b2,bank_b3);
  wire[31:0]next_a=op==2 && top_b?blo:alo;
  wire[31:0]next_b=op==2?(top_b?bhi:ahi):blo;
  wire[31:0]next_c=bottom_same?next_a:(top_b?alo:blo);
  wire[31:0]next_d=bottom_same?next_b:(top_b?ahi:bhi);
  always @(posedge clk)if(advance && raw_valid)begin
   fetch_a[lane*32+:32]<=next_a;fetch_b[lane*32+:32]<=next_b;
   fetch_c[lane*32+:32]<=next_c;fetch_d[lane*32+:32]<=next_d;
   fetch_dx[lane*2+:2]<=x==0?2'd0:(x[0]?2'd1:2'd3);
  end
  r14_tensor_alu_v1 arithmetic(.clk(clk),.rst_n(rst_n),.cancel(stop),
   .in_valid(state==EXEC && fetch_valid && alu_ready && !stop),.in_ready(alu_ready_lane[lane]),
   .in_op(op),.in_fraction_a(fa),.in_fraction_b(fb),.in_target_fraction(5'd13),
   .in_a(fetch_a[lane*32+:32]),.in_b(fetch_b[lane*32+:32]),.in_c(fetch_c[lane*32+:32]),.in_d(fetch_d[lane*32+:32]),
   .in_dx(fetch_dx[lane*2+:2]),.in_dy(y==0?2'd0:(y[0]?2'd1:2'd3)),
   .in_relu_a(relu_a),.in_relu_b(relu_b),.out_valid(alu_valid_lane[lane]),.out_ready(state==EXEC && !stop),
   .out_numerator(alu_number[lane*32+:32]),.out_quantized(),.out_error(alu_error[lane]));
 end endgenerate

 shared_dma_client_v2 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) reader(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.cmd_valid(read_cmd && !bad_read_span_q),.cmd_ready(rr),.cmd_write(1'b0),
  .cmd_address(read_address_q),.cmd_beats(row_read_beats_q),.wr_strb(8'd0),.wr_data(64'd0),.wr_valid(1'b0),.wr_ready(),
  .rd_data(rdata),.rd_valid(rv),.rd_ready(read_take),.done(rd),.cancelled(rc),.fault(rf),.quiescent(rq),.progress(rp),.transferred(rt),
  .service_request(service_request[189:0]),.service_response(service_response[225:0]),
  .m_axi_arready(1'b0),.m_axi_rdata(64'd0),.m_axi_rresp(2'd0),.m_axi_rlast(1'b0),.m_axi_rvalid(1'b0),
  .m_axi_awready(1'b0),.m_axi_wready(1'b0),.m_axi_bresp(2'd0),.m_axi_bvalid(1'b0));
 shared_dma_client_v2 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) writer(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.cmd_valid(write_cmd),.cmd_ready(wr),.cmd_write(1'b1),
  .cmd_address(d_row),.cmd_beats(row_write_beats),.wr_strb(8'hff),.wr_data(output_beat),.wr_valid(write_valid),.wr_ready(ww),
  .rd_data(),.rd_valid(),.rd_ready(1'b0),.done(wd),.cancelled(wc),.fault(wf),.quiescent(wq),.progress(wp),.transferred(wt),
  .service_request(service_request[379:190]),.service_response(service_response[451:226]),
  .m_axi_arready(1'b0),.m_axi_rdata(64'd0),.m_axi_rresp(2'd0),.m_axi_rlast(1'b0),.m_axi_rvalid(1'b0),
  .m_axi_awready(1'b0),.m_axi_wready(1'b0),.m_axi_bresp(2'd0),.m_axi_bvalid(1'b0));

 always @(posedge clk)begin
  if(!rst_n)begin
   read_b_phase<=0;bad_read_span_q<=0;read_address_q<=0;row_read_beats_q<=0;read_end_q<=0;
   state<=IDLE;op<=0;width<=0;height<=0;out_width<=0;out_height<=0;y<=0;input_y<=0;channels<=0;channel<=0;
   a_wide<=0;b_wide<=0;relu_a<=0;relu_b<=0;fa<=0;fb<=0;
   a_row<=0;b_row<=0;a_plane<=0;d_row<=0;d_base<=0;a_limit<=0;b_limit<=0;d_limit<=0;
   a_stride<=0;b_stride<=0;d_stride<=0;a_plane_bytes<=0;watchdog<=0;drain_cycles<=0;
   load_index<=0;write_index<=0;fetch_x<=0;raw_x<=0;received_groups<=0;fetch_valid<=0;raw_valid<=0;
   top_b<=0;bottom_same<=0;was_cancel<=0;drain_stuck<=0;
   done<=0;cancelled<=0;fault<=0;elapsed_cycles<=0;read_beats<=0;write_beats<=0;output_values<=0;
  end else begin
   done<=0;cancelled<=0;if(busy)elapsed_cycles<=elapsed_cycles+1;
   if(service_response[111])read_beats<=read_beats+1;if(service_response[451])write_beats<=write_beats+1;
   if(state==FAILED && clear && quiescent && !needs_fabric_reset && !cancel)begin state<=IDLE;fault<=0;end
   else if(busy && state!=DRAIN && (stop ||
      ((state==READ_A || state==READ_B) && rf!=0) || (state==WRITE_ROW && wf!=0) ||
      (read_cmd && bad_read_span_q) || (state==WRITE_PREP && bad_write_span) || (state==EXEC && alu_valid && |alu_error)))begin
    state<=DRAIN;drain_cycles<=0;was_cancel<=cancel;fetch_valid<=0;raw_valid<=0;
    if(!cancel && !fault)begin
     if(timed_out)fault<=16'hda01;
     else if((state==READ_A || state==READ_B) && rf)fault<={8'hd1,rf};
     else if(state==WRITE_ROW && wf)fault<={8'hd2,wf};else fault<=16'hda03;
    end
   end else case(state)
    IDLE:if(cmd_valid && cmd_ready)begin
     op<=cmd_op;width<=cmd_width;height<=cmd_height;channels<=cmd_channels;
     out_width<=cmd_op==2?cmd_width<<1:cmd_width;out_height<=cmd_op==2?cmd_height<<1:cmd_height;
     a_wide<=cmd_a_wide;b_wide<=cmd_b_wide;relu_a<=cmd_relu_a;relu_b<=cmd_relu_b;fa<=cmd_fraction_a;fb<=cmd_fraction_b;
     a_row<=source_a;a_plane<=source_a;b_row<=source_b;d_row<=destination;d_base<=destination;
     a_limit<=a_end[31:0];b_limit<=b_end[31:0];d_limit<=d_end[31:0];
     a_stride<={22'd0,cmd_width}<<(cmd_a_wide?2:1);b_stride<={22'd0,cmd_width}<<(cmd_b_wide?2:1);
     d_stride<={22'd0,cmd_width}<<(cmd_op==2?3:2);
     a_plane_bytes<=({22'd0,cmd_width}*{22'd0,cmd_height})<<(cmd_a_wide?2:1);
     watchdog<=timeout_cycles;elapsed_cycles<=0;read_beats<=0;write_beats<=0;output_values<=0;
     y<=0;input_y<=0;channel<=0;fetch_valid<=0;raw_valid<=0;was_cancel<=0;drain_stuck<=0;read_b_phase<=0;top_b<=0;bottom_same<=cmd_height==1;
     if(bad_command)begin state<=FAILED;fault<=16'hda90;done<=1;end else state<=READ_ADDR;
    end
    // Keep production address/extent/admission timing boundaries.
    READ_ADDR:begin read_address_q<=read_address;row_read_beats_q<=row_read_beats;state<=READ_END;end
    READ_END:begin read_end_q<={1'b0,read_address_q}+({17'd0,row_read_beats_q}<<3);state<=READ_CHECK;end
    READ_CHECK:begin bad_read_span_q<=bad_read_span;state<=read_b_phase?READ_B_CMD:READ_A_CMD;end
    READ_A_CMD,READ_B_CMD:if(read_cmd && rr)begin load_index<=0;state<=read_b_phase?READ_B:READ_A;end
    READ_A,READ_B:begin
     if(read_take)load_index<=load_index+1;
     if(rd && rq)begin
      if(rc || rt!=row_read_beats || load_index!=row_read_beats)begin fault<=16'hda10;state<=DRAIN;drain_cycles<=0;end
      else if(state==READ_A && (op==1 || !bottom_same))begin read_b_phase<=1;state<=READ_ADDR;end
      else begin state<=EXEC;fetch_x<=0;received_groups<=0;fetch_valid<=0;raw_valid<=0;end
     end
    end
    EXEC:if(advance)begin
     raw_valid<=issue;fetch_valid<=raw_valid;
     if(issue)begin raw_x<=fetch_x;fetch_x<=fetch_x+2;end
    end
    WRITE_PREP:if(!bad_write_span)state<=WRITE_CMD;
    WRITE_CMD:if(write_cmd && wr)begin write_index<=0;state<=WRITE_ADDR;end
    WRITE_ADDR:state<=WRITE_CAPTURE;
    WRITE_CAPTURE:begin output_beat<=output_quad;state<=WRITE_ROW;end
    WRITE_ROW:begin
     if(write_valid && ww)begin
      write_index<=write_index+1;
      if(write_index+1<row_write_beats)output_beat<=output_quad;
     end
     if(wd && wq)begin
      if(wc || wt!=row_write_beats || write_index!=row_write_beats)begin fault<=16'hda11;state<=DRAIN;drain_cycles<=0;end
      else state<=NEXT_ROW;
     end
    end
    NEXT_ROW:begin
     d_row<=d_row+d_stride;fetch_x<=0;received_groups<=0;fetch_valid<=0;raw_valid<=0;
     if(y+1==out_height)begin
      if(channel+1==channels)begin state<=IDLE;done<=1;end
      else begin
       channel<=channel+1;y<=0;input_y<=0;a_plane<=a_plane+a_plane_bytes;a_row<=a_plane+a_plane_bytes;
       b_row<=b_row+b_stride;read_b_phase<=0;top_b<=0;bottom_same<=height==1;state<=READ_ADDR;
      end
     end else begin
      y<=y+1;
      if(op==1)begin a_row<=a_row+a_stride;b_row<=b_row+b_stride;read_b_phase<=0;state<=READ_ADDR;end
      else if(y>=2 && !y[0])begin
       // Old bottom becomes new top. Load only the one new bottom row into
       // the old top bank; last clamped bottom aliases the already loaded top.
       a_row<=a_row+a_stride;input_y<=input_y+1;top_b<=!top_b;bottom_same<=input_y+2>=height;read_b_phase<=1;
       state<=input_y+2>=height?EXEC:READ_ADDR;
      end else state<=EXEC;
     end
    end
    DRAIN:begin
     fetch_valid<=0;raw_valid<=0;drain_cycles<=drain_cycles+1;
     if(rq && wq)begin state<=FAILED;done<=1;cancelled<=was_cancel;end
     else if(drain_cycles>=watchdog)begin drain_stuck<=1;state<=FAILED;done<=1;cancelled<=was_cancel;end
    end
   endcase
   if(alu_valid && state==EXEC && !stop && !(|alu_error))begin
    received_groups<=received_groups+1;output_values<=output_values+2;
    if(received_groups+1==row_write_beats)begin state<=WRITE_PREP;write_index<=0;fetch_valid<=0;raw_valid<=0;end
   end
  end
 end
endmodule
`default_nettype wire
