`timescale 1ns/1ps
`default_nettype none
// Independent fixed BOX and FIELD domains; only FIELD uses the FP pool.
// Each owns its DMA receipts and recovery. Both share the physical HP1 bus.
module r14_fp_combo_pipe_v4 #(
 parameter [31:0] ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000
)(
 input wire clk,rst_n,
 input wire field_cancel,field_clear,field_cfg_valid,output wire field_cfg_ready,
 input wire[1:0]field_cfg_kind,input wire[7:0]field_cfg_index,
 input wire[2:0]field_cfg_bank,input wire[3:0]field_cfg_slot,
 input wire field_cfg_scalar,field_cfg_wide,input wire[63:0]field_cfg_data,
 input wire field_cmd_valid,output wire field_cmd_ready,
 input wire[16:0]field_cmd_values,input wire[5:0]field_cmd_inputs,
 input wire[3:0]field_cmd_outputs,input wire[8:0]field_cmd_length,
 input wire[15:0]field_cmd_tag,input wire[31:0]field_timeout_cycles,
 output wire field_done,field_cancelled,field_busy,field_quiescent,field_needs_fabric_reset,
 output wire[15:0]field_fault,field_accepted_tag,field_completed_tag,
 output wire[31:0]field_elapsed_cycles,field_read_beats,field_write_beats,field_output_values,
 input wire box_cancel,box_clear,box_cmd_valid,output wire box_cmd_ready,
 input wire[1:0]box_cmd_mode,input wire[6:0]box_cmd_width,box_cmd_height,
 input wire[15:0]box_cmd_tag,
 input wire[31:0]box_source,box_destination,box_source_bytes,box_destination_bytes,box_timeout_cycles,
 output wire box_done,box_cancelled,box_busy,box_quiescent,box_needs_fabric_reset,
 output wire[15:0]box_fault,box_accepted_tag,box_completed_tag,
 output wire[31:0]box_elapsed_cycles,box_read_beats,box_write_beats,box_output_values,
 output wire[1:0]owner,output wire quiescent,needs_fabric_reset,
 output wire[759:0]service_request,input wire[903:0]service_response,
 output wire[4:0]fp_in_valid,input wire[4:0]fp_in_ready,input wire[4:0]fp_read_hint,
 output wire[14:0]fp_in_op,output wire[319:0]fp_in_a,fp_in_b,output wire[159:0]fp_in_tag,
 input wire[4:0]fp_out_valid,output wire[4:0]fp_out_ready,
 input wire[319:0]fp_out_data,input wire[159:0]fp_out_tag,input wire[14:0]fp_out_op,
 input wire fp_quiescent,input wire[7:0]fp_fault,output wire fp_cancel,fp_clear
);
 reg field_lease,box_lease,field_clearing,box_clearing,field_clear_pending,box_clear_pending;
 reg color_lease,color_failed;
 wire color_request,color_take,color_release_quiet;
 wire cs_start,cs_cancel,cs_valid,cs_ready,cs_last,cs_stats_valid,cs_stats_ready,cs_stats_last;
 wire[63:0]cs_data,cs_stats_data;
 wire bs_start,bs_cancel,bs_valid,bs_ready,bs_last,bs_output_valid,bs_output_ready,bs_output_last;
 wire[1:0]bs_mode;wire[6:0]bs_height;wire[31:0]bs_limit;
 wire[63:0]bs_data,bs_output_data;
 wire kernel_quiet,kernel_busy,kernel_done,kernel_cancelled;wire[7:0]kernel_fault;
 wire[31:0]kernel_cycles,kernel_inputs,kernel_outputs;
 wire color_available=!box_lease && !color_lease && kernel_quiet && !box_clear && !box_clear_pending;
 assign owner={box_lease,field_lease};
 wire field_enable=field_lease && !field_clearing && !field_clear_pending && !field_clear && !field_cancel;
 wire box_enable=box_lease && !box_clearing && !box_clear_pending && !box_clear && !box_cancel;
 wire field_cmd_r,field_cfg_r,box_cmd_r;
 wire[379:0]field_request,box_request;
 wire[451:0]field_response=service_response[451:0],box_response=service_response[903:452];
 wire[4:0]f_in_valid,f_out_ready;wire[14:0]f_in_op;
 wire[319:0]f_in_a,f_in_b;wire[159:0]f_in_tag;
 wire b_in_valid,b_out_ready;wire[2:0]b_in_op;
 wire[63:0]b_in_a,b_in_b;wire[31:0]b_in_tag;
 wire f_cancel,f_clear,b_cancel,b_clear;
 wire f_clear_input=field_lease && (field_clear || field_clear_pending);
 wire b_clear_input=box_lease && (box_clear || box_clear_pending);
 assign field_cfg_ready=field_enable && field_cfg_r && !field_done;
 assign field_cmd_ready=field_enable && field_cmd_r && !field_done;
 assign box_cmd_ready=box_enable && box_cmd_r && !box_done;
 assign service_request={box_request,field_request};
 assign fp_in_valid=field_lease?f_in_valid:5'd0;
 assign fp_in_op={3'd5,3'd4,3'd3,3'd2,field_lease?f_in_op[2:0]:3'd0};
 // synthesis translate_off
 wire[14:0]baseline_op=field_lease?f_in_op:15'd0;
 genvar opcode_channel;
 generate for(opcode_channel=0;opcode_channel<5;opcode_channel=opcode_channel+1)begin:OPCODE_EQUIVALENCE
  always @(posedge clk)if(rst_n && fp_in_valid[opcode_channel] &&
    fp_in_op[opcode_channel*3+:3]!==baseline_op[opcode_channel*3+:3])
   $fatal(1,"Canonical inactive opcode changed a valid FP command");
 end endgenerate
 always @(posedge clk)if(rst_n && (fp_in_op[2:0]>1 ||
   fp_in_op[5:3]!=2 || fp_in_op[8:6]!=3 || fp_in_op[11:9]!=4 || fp_in_op[14:12]!=5))
  $fatal(1,"Actual FIELD/BOX pool opcode invariant violated");
 // synthesis translate_on
 assign fp_in_a=field_lease?f_in_a:320'd0;
 assign fp_in_b=field_lease?f_in_b:320'd0;
 assign fp_in_tag=field_lease?f_in_tag:160'd0;
 assign fp_out_ready=field_lease?f_out_ready:5'd0;
 // BOX's clear is a local kernel receipt; it must never reset live FIELD
 // FP queues. BOX has no accepted FP command in the Q40 implementation.
 assign fp_cancel=field_lease?f_cancel:1'b0;
 assign fp_clear=field_lease?f_clear:1'b0;
 assign needs_fabric_reset=field_needs_fabric_reset || box_needs_fabric_reset;
 assign quiescent=!field_lease && !box_lease && !color_lease && kernel_quiet && field_quiescent && box_quiescent && fp_quiescent;
 // FIELD arithmetic runs in the exact CPU candidate. Never accept a
 // stale PL FIELD command as successful: return sticky unsupported status.
 reg field_done_q;reg[15:0]field_fault_q,field_tag_q;
 assign field_done=field_done_q;assign field_fault=field_fault_q;
 assign field_accepted_tag=field_tag_q;assign field_completed_tag=field_tag_q;
 assign field_cancelled=1'b0;assign field_busy=1'b0;assign field_quiescent=1'b1;
 assign field_needs_fabric_reset=1'b0;assign field_elapsed_cycles=0;
 assign field_read_beats=0;assign field_write_beats=0;assign field_output_values=0;
 assign field_cfg_r=1'b1;assign field_cmd_r=!field_fault_q;
 assign field_request=380'd0;assign f_in_valid=0;assign f_in_op=0;
 assign f_in_a=0;assign f_in_b=0;assign f_in_tag=0;assign f_out_ready=0;
 assign f_cancel=0;assign f_clear=f_clear_input;
 assign color_request=0;assign color_take=0;assign color_release_quiet=1;
 assign cs_start=0;assign cs_cancel=0;assign cs_valid=0;assign cs_data=0;
 assign cs_last=0;assign cs_stats_ready=0;
 always @(posedge clk)begin
  field_done_q<=0;
  if(!rst_n)begin field_done_q<=0;field_fault_q<=0;field_tag_q<=0;end
  else if(f_clear_input)field_fault_q<=0;
  else if(field_cmd_valid && field_cmd_ready)begin
   field_done_q<=1;field_fault_q<=16'ha201;field_tag_q<=field_cmd_tag;
  end
 end
 r14_box_q40_dma_v122 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) box_engine(
  .clk(clk),.rst_n(rst_n),.resource_available(box_lease&&!color_lease),
  .kernel_start(bs_start),.kernel_cancel(bs_cancel),.kernel_mode(bs_mode),.kernel_height(bs_height),.kernel_limit(bs_limit),
  .kernel_input_valid(bs_valid),.kernel_input_ready(bs_ready),.kernel_input_data(bs_data),.kernel_input_last(bs_last),
  .kernel_output_valid(bs_output_valid),.kernel_output_ready(bs_output_ready),.kernel_output_data(bs_output_data),.kernel_output_last(bs_output_last),
  .kernel_busy(box_lease?kernel_busy:1'b0),.kernel_quiet(box_lease?kernel_quiet:1'b1),
  .kernel_done(box_lease&&kernel_done),.kernel_cancelled(box_lease&&kernel_cancelled),.kernel_fault(box_lease?kernel_fault:8'd0),
  .kernel_cycles(kernel_cycles),.kernel_inputs(kernel_inputs),.kernel_outputs(kernel_outputs),
  .cancel(box_cancel && box_lease),.clear(b_clear_input),
  .cmd_valid(box_cmd_valid && box_enable && !box_done),.cmd_ready(box_cmd_r),
  .cmd_mode(box_cmd_mode),.cmd_width(box_cmd_width),.cmd_height(box_cmd_height),.cmd_tag(box_cmd_tag),
  .source(box_source),.destination(box_destination),.source_bytes(box_source_bytes),
  .destination_bytes(box_destination_bytes),.timeout_cycles(box_timeout_cycles),
  .done(box_done),.cancelled(box_cancelled),.fault(box_fault),.busy(box_busy),
  .quiescent(box_quiescent),.needs_fabric_reset(box_needs_fabric_reset),
  .elapsed_cycles(box_elapsed_cycles),.read_beats(box_read_beats),.write_beats(box_write_beats),
  .output_values(box_output_values),.accepted_tag(box_accepted_tag),.completed_tag(box_completed_tag),
  .service_request(box_request),.service_response(box_response),
  .fp_in_valid(b_in_valid),.fp_in_ready(box_lease && fp_in_ready[0]),.fp_in_op(b_in_op),
  .fp_in_a(b_in_a),.fp_in_b(b_in_b),.fp_in_tag(b_in_tag),
  .fp_out_valid(box_lease && fp_out_valid[0]),.fp_out_ready(b_out_ready),
  .fp_out_data(fp_out_data[63:0]),.fp_out_tag(fp_out_tag[31:0]),.fp_out_op(fp_out_op[2:0]),
  .fp_quiescent(1'b1),
  .fp_fault(8'd0),.fp_cancel(b_cancel),.fp_clear(b_clear));
 wire shared_box_input_ready,shared_box_output_valid,shared_box_output_last;
 wire[63:0]shared_box_output_data;
 assign cs_ready=color_lease && shared_box_input_ready;
 assign cs_stats_valid=color_lease && shared_box_output_valid;
 assign cs_stats_data=shared_box_output_data;assign cs_stats_last=shared_box_output_last;
 assign bs_ready=box_lease && shared_box_input_ready;
 assign bs_output_valid=box_lease && shared_box_output_valid;
 assign bs_output_data=shared_box_output_data;assign bs_output_last=shared_box_output_last;
 // Two registered output slots stop DMA ready from propagating through
 // the complete BOX datapath into the read DMA in the same cycle. Pending
 // output remains part of the kernel lease until consumed or cancelled.
 wire raw_box_valid,raw_box_ready,raw_box_last,output_pending;
 wire raw_kernel_quiet,raw_kernel_busy;
 wire[63:0]raw_box_data;
 wire shared_start=color_lease?cs_start:(box_lease&&bs_start);
 wire shared_cancel=color_lease?cs_cancel:(box_lease&&bs_cancel);
 retained_ready_fifo2_v1 #(.WIDTH(65)) output_fifo(
  .clk(clk),.rst_n(rst_n),.cancel(shared_start || shared_cancel),
  .in_valid(raw_box_valid),.in_ready(raw_box_ready),.in_data({raw_box_last,raw_box_data}),
  .out_valid(shared_box_output_valid),.out_ready(color_lease?cs_stats_ready:(box_lease&&bs_output_ready)),
  .out_data({shared_box_output_last,shared_box_output_data}),.busy(output_pending));
 assign kernel_quiet=raw_kernel_quiet && !output_pending;
 assign kernel_busy=raw_kernel_busy || output_pending;
 r14_box_resident_stream_v155 one_shared_box(
  .clk(clk),.rst_n(rst_n),.start(color_lease?cs_start:(box_lease&&bs_start)),
  .cancel(color_lease?cs_cancel:(box_lease&&bs_cancel)),
  .height(color_lease?7'd64:bs_height),.cmd_mode(color_lease?2'd2:bs_mode),.planes_four(color_lease),
  .cycle_limit(color_lease?field_timeout_cycles:bs_limit),
  .in_valid(color_lease?cs_valid:(box_lease&&bs_valid)),.in_ready(shared_box_input_ready),
  .in_data(color_lease?cs_data:bs_data),.in_last(color_lease?cs_last:bs_last),
  .out_valid(raw_box_valid),.out_ready(raw_box_ready),
  .out_data(raw_box_data),.out_last(raw_box_last),
  .quiescent(raw_kernel_quiet),.busy(raw_kernel_busy),.done(kernel_done),.cancelled(kernel_cancelled),
  .fault(kernel_fault),.cycles(kernel_cycles),.accepted_values(kernel_inputs),.output_values(kernel_outputs));
 always @(posedge clk)begin
  if(!rst_n)begin
   field_lease<=0;box_lease<=0;color_lease<=0;color_failed<=0;field_clearing<=0;box_clearing<=0;field_clear_pending<=0;box_clear_pending<=0;
  end else begin
   if(!field_lease && !field_busy && fp_quiescent && !fp_fault)begin
    if(field_clear)begin field_lease<=1;field_clear_pending<=1;end
    else if((field_cfg_valid || field_cmd_valid) && !field_cancel && !field_fault)field_lease<=1;
   end else if(field_lease)begin
    if(f_clear)begin field_clearing<=1;field_clear_pending<=0;end
    if(field_clearing && !field_clear && fp_quiescent && field_quiescent && !field_fault && !field_needs_fabric_reset)begin
     field_lease<=0;field_clearing<=0;
    end else if(!field_clearing && !f_cancel && !f_clear && field_done && !field_cancelled && !field_fault && field_quiescent && fp_quiescent)field_lease<=0;
   end
   if(!box_lease && !box_busy && !color_lease && !color_request && kernel_quiet)begin
    if(box_clear)begin box_lease<=1;box_clear_pending<=1;end
    else if(box_cmd_valid && !box_cancel && !box_fault)box_lease<=1;
   end else if(box_lease)begin
    if(b_clear)begin box_clearing<=1;box_clear_pending<=0;end
    if(box_clearing && !box_clear && box_quiescent && !box_fault && !box_needs_fabric_reset)begin
     box_lease<=0;box_clearing<=0;
    end else if(!box_clearing && !b_cancel && !b_clear && box_done && !box_cancelled && !box_fault && box_quiescent)box_lease<=0;
   end
   if(color_take)begin color_lease<=1;color_failed<=0;end
   if(color_lease && (field_fault || field_cancelled || field_cancel))color_failed<=1;
   if(color_lease && color_release_quiet && !field_fault && !field_cancel &&
      (!color_failed || field_clearing))begin color_lease<=0;color_failed<=0;end
  end
 end
 // synthesis translate_off
 always @(posedge clk)if(rst_n)begin
  if(shared_start && output_pending)$fatal(1,"BOX started before buffered output retired");
  if(box_lease && color_lease)$fatal(1,"Shared BOX kernel acquired by two jobs");
  if(color_take && !color_available)$fatal(1,"Resident FIELD accepted without BOX reservation");
  if(kernel_busy && !box_lease && !color_lease)$fatal(1,"Live BOX kernel lost its lease");
  if(b_in_valid || b_out_ready || b_cancel)$fatal(1,"Q40 BOX unexpectedly uses the shared FP pool");
  if(field_busy && !field_lease || box_busy && !box_lease)$fatal(1,"Active local job lost its independent lease");
  if(b_clear && fp_clear && !f_clear)$fatal(1,"BOX clear leaked into FIELD pool");
 end
 // synthesis translate_on
endmodule
`default_nettype wire
