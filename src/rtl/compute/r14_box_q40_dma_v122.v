`timescale 1ns/1ps
`default_nettype none
// Statistics owner using the actual shared DMA clients. Read and write jobs
// overlap; completion is published only after final write response and drain.
module r14_box_q40_dma_v122 #(parameter[31:0]ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000)(
 input wire clk,rst_n,cancel,clear,cmd_valid,resource_available,output wire cmd_ready,
 output wire kernel_start,kernel_cancel,output wire[1:0]kernel_mode,
 output wire[6:0]kernel_height,output wire[31:0]kernel_limit,
 output wire kernel_input_valid,input wire kernel_input_ready,
 output wire[63:0]kernel_input_data,output wire kernel_input_last,
 input wire kernel_output_valid,output wire kernel_output_ready,
 input wire[63:0]kernel_output_data,input wire kernel_output_last,
 input wire kernel_busy,kernel_quiet,kernel_done,kernel_cancelled,
 input wire[7:0]kernel_fault,input wire[31:0]kernel_cycles,kernel_inputs,kernel_outputs,
 input wire[1:0]cmd_mode,input wire[6:0]cmd_width,cmd_height,input wire[15:0]cmd_tag,
 input wire[31:0]source,destination,source_bytes,destination_bytes,timeout_cycles,
 output reg done,cancelled,output reg[15:0]fault,output wire busy,quiescent,needs_fabric_reset,
 output reg[31:0]elapsed_cycles,read_beats,write_beats,output_values,
 output reg[15:0]accepted_tag,completed_tag,
 output wire[379:0]service_request,input wire[451:0]service_response,
 output wire fp_in_valid,input wire fp_in_ready,output wire[2:0]fp_in_op,
 output wire[63:0]fp_in_a,fp_in_b,output wire[31:0]fp_in_tag,
 input wire fp_out_valid,output wire fp_out_ready,input wire[63:0]fp_out_data,
 input wire[31:0]fp_out_tag,input wire[2:0]fp_out_op,
 input wire fp_quiescent,input wire[7:0]fp_fault,output wire fp_cancel,fp_clear
);
 localparam IDLE=0,PRODUCT=1,CHECK=2,COMMIT=3,KSTART=4,RUN=5,DRAIN=6,FAILED=7;
 localparam SEND=0,ACTIVE=1,FINISHED=2;
 reg[2:0]state;reg[1:0]mode,rphase,wphase;reg[6:0]height;reg[15:0]tag;
 reg[31:0]read_address,write_address,watchdog,drain_cycles,saved_source_bytes,saved_destination_bytes;
 reg[32:0]source_end,destination_end;
 reg[12:0]source_values,destination_values,input_index,output_index,chunk_received,chunk_produced;
 reg[15:0]read_chunk,write_chunk;
 reg scalar_bad;reg[2:0]extent_bad;
 reg was_cancel,drain_stuck,read_owned,write_owned,kernel_finished,kernel_clear;
 wire rr,rd,rc,rq,rv,wr,wd,wc,wq,ww;
 wire[7:0]rf,wf,k_fault;wire[31:0]rt,wt,k_cycles,k_inputs,k_outputs;
 wire[63:0]rdata,k_data;wire k_in_ready,k_valid,k_last,k_busy,k_quiet,k_done,k_cancelled;
 wire timeout_q;
 r14_deadline32_v8 deadline(.clk(clk),.rst_n(rst_n),.busy(busy),.elapsed(elapsed_cycles),.limit(watchdog),.expired(timeout_q));
 wire preparing=state==PRODUCT || state==CHECK || state==COMMIT;
 wire timed_out=busy && state!=DRAIN && !preparing && timeout_q;
 wire stop=cancel || state==DRAIN || state==FAILED || timed_out || fault!=0;
 wire read_ready=state==RUN && rphase==ACTIVE && k_in_ready && !stop;
 wire read_take=rv && read_ready;
 wire write_valid=state==RUN && wphase==ACTIVE && k_valid && !stop;
 wire write_take=write_valid && ww;
 wire bad_last=write_take && (k_last!=(output_index==destination_values-1'b1));
 wire accept_clear=clear && !kernel_clear && (state==IDLE || state==FAILED) && rq && wq && k_quiet && !needs_fabric_reset;
 wire scalar_invalid=cmd_width!=80 || (cmd_height!=60 && cmd_height!=64) || cmd_mode==1 || !timeout_cycles ||
       source[2:0]!=0 || destination[2:0]!=0 || source<ADDRESS_MIN || destination<ADDRESS_MIN;
 assign busy=state!=IDLE && state!=FAILED;
 assign quiescent=!kernel_clear && !accept_clear && (state==IDLE || state==FAILED) && rq && wq && k_quiet;
 assign cmd_ready=rst_n && !kernel_clear && !accept_clear && state==IDLE && rq && wq && k_quiet && resource_available && !cancel && !clear && !fault;
 assign needs_fabric_reset=drain_stuck || rf==4 || rf==5 || wf==4 || wf==5;
 // This statistics component does not request the shared FP pool. The frozen
 // substantive NN and other graph operators keep their existing implementations.
 assign fp_in_valid=0;assign fp_in_op=0;assign fp_in_a=0;assign fp_in_b=0;assign fp_in_tag=0;
 assign fp_out_ready=0;assign fp_cancel=0;// Registered accepted-clear receipt releases the existing shared owner.
 assign fp_clear=kernel_clear;
 assign kernel_start=state==KSTART && !stop;
 assign kernel_cancel=stop || kernel_clear;
 assign kernel_mode=mode;assign kernel_height=height;assign kernel_limit=watchdog;
 assign kernel_input_valid=state==RUN && rphase==ACTIVE && rv && !stop;
 assign kernel_input_data=rdata;assign kernel_input_last=input_index==source_values-1'b1;
 assign kernel_output_ready=state==RUN && wphase==ACTIVE && ww && !stop;
 assign k_in_ready=kernel_input_ready;assign k_valid=kernel_output_valid;
 assign k_data=kernel_output_data;assign k_last=kernel_output_last;
 assign k_busy=kernel_busy;assign k_quiet=kernel_quiet;assign k_done=kernel_done;
 assign k_cancelled=kernel_cancelled;assign k_fault=kernel_fault;
 assign k_cycles=kernel_cycles;assign k_inputs=kernel_inputs;assign k_outputs=kernel_outputs;
 shared_dma_client_v2 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) reader(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.cmd_valid(state==RUN && rphase==SEND && !stop),.cmd_ready(rr),.cmd_write(1'b0),
  .cmd_address(read_address),.cmd_beats(read_chunk),.wr_strb(8'd0),.wr_data(64'd0),.wr_valid(1'b0),.wr_ready(),
  .rd_data(rdata),.rd_valid(rv),.rd_ready(read_ready),.done(rd),.cancelled(rc),.fault(rf),.quiescent(rq),.progress(),.transferred(rt),
  .service_request(service_request[189:0]),.service_response(service_response[225:0]),
  .m_axi_arready(1'b0),.m_axi_rdata(64'd0),.m_axi_rresp(2'd0),.m_axi_rlast(1'b0),.m_axi_rvalid(1'b0),
  .m_axi_awready(1'b0),.m_axi_wready(1'b0),.m_axi_bresp(2'd0),.m_axi_bvalid(1'b0));
 shared_dma_client_v2 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) writer(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.cmd_valid(state==RUN && wphase==SEND && !stop),.cmd_ready(wr),.cmd_write(1'b1),
  .cmd_address(write_address),.cmd_beats(write_chunk),.wr_strb(8'hff),.wr_data(k_data),.wr_valid(write_valid),.wr_ready(ww),
  .rd_data(),.rd_valid(),.rd_ready(1'b0),.done(wd),.cancelled(wc),.fault(wf),.quiescent(wq),.progress(),.transferred(wt),
  .service_request(service_request[379:190]),.service_response(service_response[451:226]),
  .m_axi_arready(1'b0),.m_axi_rdata(64'd0),.m_axi_rresp(2'd0),.m_axi_rlast(1'b0),.m_axi_rvalid(1'b0),
  .m_axi_awready(1'b0),.m_axi_wready(1'b0),.m_axi_bresp(2'd0),.m_axi_bvalid(1'b0));
 always @(posedge clk)begin
  if(!rst_n)kernel_clear<=0;else kernel_clear<=accept_clear;
 end
 always @(posedge clk)begin
  done<=0;cancelled<=0;
  if(!rst_n)begin
   state<=IDLE;mode<=0;height<=0;tag<=0;rphase<=SEND;wphase<=SEND;
   read_address<=0;write_address<=0;watchdog<=0;drain_cycles<=0;saved_source_bytes<=0;saved_destination_bytes<=0;
   source_end<=0;destination_end<=0;source_values<=0;destination_values<=0;
   input_index<=0;output_index<=0;chunk_received<=0;chunk_produced<=0;read_chunk<=0;write_chunk<=0;
   scalar_bad<=0;extent_bad<=0;was_cancel<=0;drain_stuck<=0;read_owned<=0;write_owned<=0;kernel_finished<=0;
   fault<=0;elapsed_cycles<=0;read_beats<=0;write_beats<=0;output_values<=0;accepted_tag<=0;completed_tag<=0;
  end else if(accept_clear)begin
   state<=IDLE;fault<=0;drain_stuck<=0;was_cancel<=0;read_owned<=0;write_owned<=0;kernel_finished<=0;
  end else begin
   if(busy)elapsed_cycles<=elapsed_cycles+1'b1;
   if(service_response[111])read_beats<=read_beats+1'b1;
   if(service_response[451])write_beats<=write_beats+1'b1;
   if(k_done)kernel_finished<=1;
   if(busy && state!=DRAIN && (cancel || timed_out || (read_owned && rf!=0) || (write_owned && wf!=0) || k_fault || bad_last))begin
    state<=DRAIN;drain_cycles<=0;was_cancel<=cancel;
    if(!cancel && !fault)begin
     if(timed_out)fault<=16'hdb01;
     else if(read_owned && rf!=0)fault<={8'hd1,rf};
     else if(write_owned && wf!=0)fault<={8'hd2,wf};
     else if(k_fault)fault<={8'hd3,k_fault};else fault<=16'hdb13;
    end
   end else case(state)
    IDLE:if(cmd_valid && cmd_ready)begin
     mode<=cmd_mode;height<=cmd_height;tag<=cmd_tag;accepted_tag<=cmd_tag;
     read_address<=source;write_address<=destination;saved_source_bytes<=source_bytes;saved_destination_bytes<=destination_bytes;
     scalar_bad<=scalar_invalid;extent_bad<=0;watchdog<=timeout_cycles;
     elapsed_cycles<=0;read_beats<=0;write_beats<=0;output_values<=0;
     input_index<=0;output_index<=0;was_cancel<=0;drain_stuck<=0;read_owned<=0;write_owned<=0;kernel_finished<=0;
     state<=PRODUCT;
    end
    PRODUCT:begin
     source_values<=height==60?13'd4800:13'd5120;
     destination_values<=mode==3?(height==60?13'd4860:13'd5184):(height==60?13'd4800:13'd5120);
     source_end<={1'b0,read_address}+{1'b0,saved_source_bytes};
     destination_end<={1'b0,write_address}+{1'b0,saved_destination_bytes};state<=CHECK;
    end
    CHECK:begin
     extent_bad[0]<=source_end[32] || destination_end[32] || source_end>{1'b0,ADDRESS_END} || destination_end>{1'b0,ADDRESS_END};
     extent_bad[1]<=saved_source_bytes!=({19'd0,source_values}<<3) || saved_destination_bytes!=({19'd0,destination_values}<<3);
     extent_bad[2]<={1'b0,read_address}<destination_end && {1'b0,write_address}<source_end;
     read_chunk<=4096;write_chunk<=4096;rphase<=SEND;wphase<=SEND;state<=COMMIT;
    end
    COMMIT:if(scalar_bad || |extent_bad)begin state<=FAILED;fault<=16'hdb90;done<=1;end else state<=KSTART;
    KSTART:state<=RUN;
    RUN:begin
     if(rphase==SEND && rr)begin rphase<=ACTIVE;chunk_received<=0;read_owned<=1;end
     if(wphase==SEND && wr)begin wphase<=ACTIVE;chunk_produced<=0;write_owned<=1;end
     if(read_take)begin input_index<=input_index+1'b1;chunk_received<=chunk_received+1'b1;end
     if(write_take)begin output_index<=output_index+1'b1;chunk_produced<=chunk_produced+1'b1;end
     if(rphase==ACTIVE && rd && rq)begin
      read_owned<=0;
      if(rc || rt!=read_chunk || chunk_received!=read_chunk)begin fault<=16'hdb10;state<=DRAIN;drain_cycles<=0;end
      else if(input_index==source_values)rphase<=FINISHED;
      else begin read_address<=read_address+({16'd0,read_chunk}<<3);read_chunk<=source_values-input_index;rphase<=SEND;end
     end
     if(wphase==ACTIVE && wd && wq)begin
      write_owned<=0;
      if(wc || wt!=write_chunk || chunk_produced!=write_chunk)begin fault<=16'hdb11;state<=DRAIN;drain_cycles<=0;end
      else if(output_index==destination_values)wphase<=FINISHED;
      else begin write_address<=write_address+({16'd0,write_chunk}<<3);write_chunk<=destination_values-output_index;wphase<=SEND;end
     end
     if(rphase==FINISHED && wphase==FINISHED && kernel_finished && k_quiet && rq && wq)begin
      if(k_inputs!=source_values || k_outputs!=destination_values || input_index!=source_values || output_index!=destination_values)begin
       fault<=16'hdb12;state<=DRAIN;drain_cycles<=0;
      end else begin completed_tag<=tag;output_values<=destination_values;done<=1;state<=IDLE;end
     end
    end
    DRAIN:begin
     drain_cycles<=drain_cycles+1'b1;
     if(rq && wq && k_quiet)begin state<=FAILED;done<=1;cancelled<=was_cancel;read_owned<=0;write_owned<=0;end
     else if(drain_cycles>=1024)begin drain_stuck<=1;state<=FAILED;done<=1;cancelled<=was_cancel;end
    end
    default:begin end
   endcase
  end
 end
endmodule
`default_nettype wire
