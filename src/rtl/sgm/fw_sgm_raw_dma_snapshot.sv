// Derived from frozen reference/sgm_v327/retained_sgm_raw_dma_v1.v SHA256 61fd94271d7115705c7d4a63e58fd053e83c0f3298d7c5aef99cbcd0124d0aa2.
// v224: finite native FIFO batches16 records through the existing sole writer.
// Arithmetic/record layout is frozen. Prefixes advance only after successful B.
`timescale 1ns/1ps
`default_nettype none
// Exact current-model observations. DDR layout is native raster64bits/pixel
// and coarse raster72bytes/cell (17U16 numerators+17U16 masses+32zero bits).
(* use_dsp="no" *) module fw_sgm_raw_dma_rtl #(
 parameter[31:0]ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000
)(
 input wire clk,rst_n,cancel,clear,cmd_valid,output wire cmd_ready,
 input wire[31:0]left_address,right_address,curve_address,native_address,cmd_tag,timeout_cycles,
 output reg done,cancelled,output reg[15:0]fault,output wire busy,quiescent,needs_fabric_reset,
 output wire[31:0]debug_state,output reg[31:0]elapsed_cycles,read_beats,write_beats,accepted_pixels,native_values,curve_values,
 output wire[31:0]prefix_tag,output reg[31:0]native_committed,curve_committed,
 output wire[379:0]service_request,input wire[451:0]service_response
);
 localparam IDLE=0,CHECK=1,START=2,RUN=3,DRAIN=4,FAILED=5;
 reg[2:0]state,read_state;reg[1:0]check_i,check_j;reg checking_pairs;
 reg[31:0]bases[0:3],tag,watchdog,drain_cycles;reg was_cancel,drain_stuck,abort_sent,core_finished;
 reg[7:0]input_y,read_word;reg[8:0]input_x;reg raw_full,raw_half;reg[63:0]raw,left_q;
 (* ram_style="block" *)reg[63:0]left_row[0:159];
 reg[63:0]native_packet;reg native_full,curve_full,writing,write_native;
 // One producer write and one registered read; no multiport/frame replication.
 // The existing native_packet is the read/output register, held under stalls.
 (* ram_style="distributed" *)reg[63:0]native_fifo[0:15];
 reg[3:0]native_head,native_tail;reg[4:0]native_count,native_fetch_left;
 reg prefer_native;
 reg[575:0]curve_packet;reg[3:0]curve_beat;
 reg[31:0]native_cursor,curve_cursor;
 reg[12:0]left_row_low,right_row_low;
 assign prefix_tag=tag;
 reg[8:0]expected_nx;reg[7:0]expected_ny;
 reg[6:0]expected_cx;reg[5:0]expected_cy;
 // Every fault assignment enters DRAIN on the same edge. FAILED
 // preserves it until quiet CLEAR enters IDLE and clears fault together.
 // Avoid duplicating the sticky16bit fault decode on all DMA controls.
 wire stop=cancel || state==DRAIN || state==FAILED;
 wire rr,rd,rc,rq,rp,rv;wire[7:0]rf;wire[31:0]rt;wire[63:0]rdata;
 wire wr,wd,wc,wq,wp,ww;wire[7:0]wf;wire[31:0]wt;
 wire cr,ci,cd,cc,cb,cq;wire[15:0]cf;
 wire nv,cv,nlast,clast;wire[63:0]ndata;wire[575:0]cdata;wire[15:0]nx,ny,cx,cy;
 wire child_clear=state==FAILED && clear && !cancel && rq && wq && cq && !needs_fabric_reset;
 wire[31:0]size_i=check_i<2?32'd307200:(check_i==2?32'd368640:32'd614400);
 wire[31:0]size_j=check_j<2?32'd307200:(check_j==2?32'd368640:32'd614400);
 wire[32:0]end_i={1'b0,bases[check_i]}+{1'b0,size_i},end_j={1'b0,bases[check_j]}+{1'b0,size_j};
 wire range_bad=bases[check_i][2:0]!=0 || bases[check_i]<ADDRESS_MIN || end_i>{1'b0,ADDRESS_END} || end_i[32];
 wire overlap={1'b0,bases[check_i]}<end_j && {1'b0,bases[check_j]}<end_i;
 wire[31:0]input_offset=({24'd0,input_y}<<10)+({24'd0,input_y}<<8);
 // Each row advances1280bytes=5*256. Low8bits never change.
 // A13bit partial cursor holds original bits19:8 plus at most1275;
 // its single carry increments the untouched12high address bits.
 wire[12:0]read_row_low=read_state<2?left_row_low:right_row_low;
 wire[11:0]read_row_high=(read_state<2?bases[0][31:20]:bases[1][31:20])+read_row_low[12];
 wire[31:0]read_address={read_row_high,read_row_low[11:0],(read_state<2?bases[0][7:0]:bases[1][7:0])};
 wire read_command=state==RUN && (read_state==0 || read_state==2) && !stop;
 wire read_take=rv && state==RUN && (read_state==1 || (read_state==3 && !raw_full)) && !stop;
 wire pixel_valid=state==RUN && raw_full && !stop;wire pixel_take=pixel_valid && ci;
 wire[31:0]lp=raw_half?left_q[63:32]:left_q[31:0],rpixel=raw_half?raw[63:32]:raw[31:0];
 wire native_ready=state==RUN && native_count<16 && !stop;
 wire curve_ready=state==RUN && !curve_full && !stop;
 wire native_take=nv&&native_ready,curve_take=cv&&curve_ready;
 // Waiting for a whole batch must not occupy the writer: curve backpressure
 // stops the common volume producer. Alternate when both are ready, so a
 // full native batch and a pending curve each wait at most one other command.
 wire select_native=native_count==16 && (!curve_full || prefer_native);
 wire write_command=state==RUN && !writing && (native_count==16 || curve_full) && !stop;
 wire[31:0]write_address=select_native?native_cursor:curve_cursor;
 wire write_valid=writing && (write_native?native_full:(curve_full && curve_beat<9)) && !stop;
 wire write_take=write_valid&&ww;
 wire native_fetch=state==RUN && writing && write_native && native_fetch_left!=0 && native_count!=0 && (!native_full || write_take) && !stop;
 wire service_read_beat=|{service_response[111],service_response[337]};
 wire service_write_beat=|{service_response[225],service_response[451]};
 assign cmd_ready=rst_n && state==IDLE && rq && wq && cq && cr && !cancel && !fault;
 assign busy=state!=IDLE && state!=FAILED;assign quiescent=(state==IDLE || state==FAILED) && rq && wq && cq;
 assign needs_fabric_reset=drain_stuck || rf==4 || rf==5 || wf==4 || wf==5;
 assign debug_state={12'd0,state,read_state,curve_beat,input_y[3:0],writing,write_native,native_full,curve_full,raw_full,raw_half};
 always @(posedge clk)begin
  if(read_take && read_state==1)left_row[read_word]<=rdata;
  if(native_take)native_fifo[native_tail]<=ndata;
  if(native_fetch)native_packet<=native_fifo[native_head];
  // native_head!=native_tail whenever read/write are both enabled. Full
  // blocks enqueue; empty blocks fetch. No RAM read-during-write dependency.
  if(!rst_n)left_q<=0;
  else if(read_take && read_state==3)left_q<=left_row[read_word];
 end
 fw_sgm_raw_stream_rtl core(
  .clk(clk),.rst_n(rst_n),.cancel(state==DRAIN && !abort_sent),.clear(child_clear),.cmd_valid(state==START&&!stop),.cmd_ready(cr),.cmd_tag(tag),.timeout_cycles(watchdog),
  .in_valid(pixel_valid),.in_ready(ci),.in_left(lp[23:0]),.in_right(rpixel[23:0]),.in_left_support(lp[24]),.in_right_support(rpixel[24]),.in_last(input_y==239 && input_x==319),
  .native_valid(nv),.native_ready(native_ready),.native_data(ndata),.native_x(nx),.native_y(ny),.native_last(nlast),
  .curve_valid(cv),.curve_ready(curve_ready),.curve_data(cdata),.curve_x(cx),.curve_y(cy),.curve_last(clast),
  .done(cd),.cancelled(cc),.fault(cf),.busy(cb),.quiescent(cq),.elapsed_cycles(),.accepted_pixels(),.emitted_native(),.emitted_curves());
 shared_dma_client_v2 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) reader(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.cmd_valid(read_command),.cmd_ready(rr),.cmd_write(1'b0),.cmd_address(read_address),.cmd_beats(16'd160),
  .wr_data(64'd0),.wr_strb(8'hff),.wr_valid(1'b0),.wr_ready(),.rd_data(rdata),.rd_valid(rv),.rd_ready(state==RUN && (read_state==1 || (read_state==3 && !raw_full)) && !stop),
  .done(rd),.cancelled(rc),.fault(rf),.quiescent(rq),.progress(rp),.transferred(rt),
  .m_axi_araddr(),.m_axi_arlen(),.m_axi_arsize(),.m_axi_arburst(),.m_axi_arvalid(),.m_axi_arready(1'b0),.m_axi_rdata(64'd0),.m_axi_rresp(2'd0),.m_axi_rlast(1'b0),.m_axi_rvalid(1'b0),.m_axi_rready(),
  .m_axi_awaddr(),.m_axi_awlen(),.m_axi_awsize(),.m_axi_awburst(),.m_axi_awvalid(),.m_axi_awready(1'b0),.m_axi_wdata(),.m_axi_wstrb(),.m_axi_wlast(),.m_axi_wvalid(),.m_axi_wready(1'b0),.m_axi_bresp(2'd0),.m_axi_bvalid(1'b0),.m_axi_bready(),
  .service_request(service_request[0+:190]),.service_response(service_response[0+:226]));
 shared_dma_client_v2 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) writer(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.cmd_valid(write_command),.cmd_ready(wr),.cmd_write(1'b1),.cmd_address(write_address),.cmd_beats(select_native?16'd16:16'd9),
  .wr_data(write_native?native_packet:curve_packet[63:0]),.wr_strb(8'hff),.wr_valid(write_valid),.wr_ready(ww),.rd_data(),.rd_valid(),.rd_ready(1'b0),
  .done(wd),.cancelled(wc),.fault(wf),.quiescent(wq),.progress(wp),.transferred(wt),
  .m_axi_araddr(),.m_axi_arlen(),.m_axi_arsize(),.m_axi_arburst(),.m_axi_arvalid(),.m_axi_arready(1'b0),.m_axi_rdata(64'd0),.m_axi_rresp(2'd0),.m_axi_rlast(1'b0),.m_axi_rvalid(1'b0),.m_axi_rready(),
  .m_axi_awaddr(),.m_axi_awlen(),.m_axi_awsize(),.m_axi_awburst(),.m_axi_awvalid(),.m_axi_awready(1'b0),.m_axi_wdata(),.m_axi_wstrb(),.m_axi_wlast(),.m_axi_wvalid(),.m_axi_wready(1'b0),.m_axi_bresp(2'd0),.m_axi_bvalid(1'b0),.m_axi_bready(),
  .service_request(service_request[190+:190]),.service_response(service_response[226+:226]));
 integer i;
 always @(posedge clk)begin
  if(!rst_n)begin
   state<=IDLE;read_state<=0;check_i<=0;check_j<=0;checking_pairs<=0;tag<=0;watchdog<=0;drain_cycles<=0;was_cancel<=0;drain_stuck<=0;abort_sent<=0;core_finished<=0;
   input_y<=0;read_word<=0;input_x<=0;raw_full<=0;raw_half<=0;raw<=0;curve_packet<=0;native_full<=0;curve_full<=0;writing<=0;write_native<=0;curve_beat<=0;
   native_head<=0;native_tail<=0;native_count<=0;native_fetch_left<=0;prefer_native<=0;
   native_cursor<=0;curve_cursor<=0;left_row_low<=0;right_row_low<=0;
   native_committed<=0;curve_committed<=0;expected_nx<=0;expected_ny<=0;expected_cx<=0;expected_cy<=0;
   done<=0;cancelled<=0;fault<=0;elapsed_cycles<=0;read_beats<=0;write_beats<=0;accepted_pixels<=0;native_values<=0;curve_values<=0;
   for(i=0;i<4;i=i+1)bases[i]<=0;
  end else begin
   done<=0;cancelled<=0;
   if(busy && state!=DRAIN)elapsed_cycles<=elapsed_cycles+1'b1;
   if(state==RUN && service_read_beat)read_beats<=read_beats+1'b1;
   if(state==RUN && service_write_beat)write_beats<=write_beats+1'b1;
   if(state==RUN && cd)core_finished<=1;
   if(read_take)begin read_word<=read_word+1'b1;if(read_state==3)begin raw<=rdata;raw_full<=1;raw_half<=0;end end
   if(pixel_take)begin accepted_pixels<=accepted_pixels+1'b1;input_x<=input_x+1'b1;raw_half<=!raw_half;if(raw_half)raw_full<=0;end
   if(native_take)begin native_tail<=native_tail+1'b1;native_values<=native_values+1'b1;if(expected_nx==319)begin expected_nx<=0;expected_ny<=expected_ny+1'b1;end else expected_nx<=expected_nx+1'b1;end
   case({native_take,native_fetch})
    2'b10:native_count<=native_count+1'b1;
    2'b01:native_count<=native_count-1'b1;
    default:begin end
   endcase
   if(curve_take)begin curve_packet<=cdata;curve_full<=1;curve_values<=curve_values+1'b1;if(expected_cx==79)begin expected_cx<=0;expected_cy<=expected_cy+1'b1;end else expected_cx<=expected_cx+1'b1;end
   if(write_command && wr)begin writing<=1;write_native<=select_native;prefer_native<=!select_native;curve_beat<=0;native_fetch_left<=select_native?5'd16:5'd0;end
   if(write_take)begin if(write_native)native_full<=0;else begin curve_packet<=curve_packet>>64;curve_beat<=curve_beat+1'b1;end end
   if(native_fetch)begin native_head<=native_head+1'b1;native_fetch_left<=native_fetch_left-1'b1;native_full<=1;end
   if(cancel && state!=DRAIN && state!=FAILED)begin state<=DRAIN;drain_cycles<=0;was_cancel<=1;abort_sent<=0;end
   else if(state==RUN && elapsed_cycles>=watchdog)begin fault<=16'hfdc0;state<=DRAIN;drain_cycles<=0;abort_sent<=0;end
   else if(state==RUN && (((read_state==1 || read_state==3) && rf) || (writing && wf) || cf))begin fault<=cf?cf:(((read_state==1 || read_state==3) && rf)?{8'hfe,rf}:{8'hff,wf});state<=DRAIN;drain_cycles<=0;abort_sent<=0;end
   else if(pixel_take && (lp[31:25]!=0 || rpixel[31:25]!=0))begin fault<=16'hfd91;state<=DRAIN;drain_cycles<=0;abort_sent<=0;end
   else if(native_take && (nx!=expected_nx || ny!=expected_ny || nlast!=(native_values==76799)))begin fault<=16'hfdb1;state<=DRAIN;drain_cycles<=0;abort_sent<=0;end
   else if(curve_take && (cx!=expected_cx || cy!=expected_cy || clast!=(curve_values==5119)))begin fault<=16'hfdb0;state<=DRAIN;drain_cycles<=0;abort_sent<=0;end
   else if(state==RUN && writing && wd && (wc || wt!=(write_native?16:9)))begin fault<=16'hfdb2;state<=DRAIN;drain_cycles<=0;abort_sent<=0;end
   else begin
    if(state==RUN && writing && wd)begin writing<=0;if(write_native)begin native_committed<=native_committed+32'd16;native_cursor<=native_cursor+32'd128;end else begin curve_full<=0;curve_committed<=curve_committed+1'b1;curve_cursor<=curve_cursor+32'd72;end end
    case(state)
     IDLE:if(cmd_valid && cmd_ready)begin
      native_cursor<=native_address;curve_cursor<=curve_address;
      left_row_low<={1'b0,left_address[19:8]};right_row_low<={1'b0,right_address[19:8]};
      bases[0]<=left_address;bases[1]<=right_address;bases[2]<=curve_address;bases[3]<=native_address;tag<=cmd_tag;watchdog<=timeout_cycles;
      check_i<=0;check_j<=0;checking_pairs<=0;state<=CHECK;elapsed_cycles<=0;read_beats<=0;write_beats<=0;accepted_pixels<=0;native_values<=0;curve_values<=0;
      was_cancel<=0;drain_stuck<=0;abort_sent<=0;core_finished<=0;drain_cycles<=0;read_state<=0;input_y<=0;read_word<=0;input_x<=0;raw_full<=0;raw_half<=0;
      native_full<=0;curve_full<=0;writing<=0;curve_beat<=0;native_committed<=0;curve_committed<=0;expected_nx<=0;expected_ny<=0;expected_cx<=0;expected_cy<=0;
      native_head<=0;native_tail<=0;native_count<=0;native_fetch_left<=0;prefer_native<=0;
     end
     CHECK:begin
      if(watchdog==0 || range_bad || (checking_pairs && overlap))begin fault<=16'hfd90;state<=DRAIN;drain_cycles<=0;abort_sent<=0;end
      else if(!checking_pairs && check_i==3)begin checking_pairs<=1;check_i<=1;check_j<=0;end
      else if(!checking_pairs)check_i<=check_i+1'b1;
      else if(check_j+1<check_i)check_j<=check_j+1'b1;
      else if(check_i==3)state<=START;
      else begin check_i<=check_i+1'b1;check_j<=0;end
     end
     START:if(cr)state<=RUN;
     RUN:begin
      case(read_state)
       0,2:if(rr)begin read_state<=read_state+1'b1;read_word<=0;end
       1,3:if(rd)begin
        if(rc || rt!=160 || read_word!=160)begin fault<=16'hfdb3;state<=DRAIN;drain_cycles<=0;abort_sent<=0;end
        else if(read_state==1)begin read_state<=2;read_word<=0;end else read_state<=4;
       end
       4:if(!raw_full)begin
        if(input_x!=320)begin fault<=16'hfdb4;state<=DRAIN;drain_cycles<=0;abort_sent<=0;end
        else if(input_y==239)read_state<=5;else begin left_row_low<=left_row_low+13'd5;right_row_low<=right_row_low+13'd5;input_y<=input_y+1'b1;input_x<=0;read_word<=0;read_state<=0;end
       end
       default:begin end
      endcase
      // Fixed320x240 geometry has exactly4800 complete native batches.
      // Partial buffers on cancel/error remain quarantined until quiet CLEAR.
      if(core_finished && read_state==5 && native_committed==76800 && curve_committed==5120 && !writing && !native_full && native_count==0 && native_fetch_left==0 && !curve_full && rq && wq && cq)begin
       if(read_beats!=76800 || write_beats!=122880 || accepted_pixels!=76800 || native_values!=76800 || curve_values!=5120)begin fault<=16'hfdb5;state<=DRAIN;drain_cycles<=0;abort_sent<=0;end
       else begin done<=1;state<=IDLE;end
      end
     end
     DRAIN:begin abort_sent<=1;if(drain_cycles!=32'hffffffff)drain_cycles<=drain_cycles+1'b1;if(abort_sent && rq && wq && cq)begin state<=FAILED;cancelled<=was_cancel;end else if(drain_cycles>=watchdog && watchdog!=0)drain_stuck<=1;end
     FAILED:if(child_clear)begin state<=IDLE;fault<=0;raw_full<=0;native_full<=0;curve_full<=0;writing<=0;native_head<=0;native_tail<=0;native_count<=0;native_fetch_left<=0;end
     default:begin fault<=16'hfdff;state<=DRAIN;drain_cycles<=0;abort_sent<=0;end
    endcase
   end
  end
 end
 // synthesis translate_off
 always @(posedge clk)if(rst_n && fault!=0 && state!=DRAIN && state!=FAILED)
  $fatal(1,"SGM sticky fault escaped drain/failed state");
 // synthesis translate_on
endmodule
`default_nettype wire
