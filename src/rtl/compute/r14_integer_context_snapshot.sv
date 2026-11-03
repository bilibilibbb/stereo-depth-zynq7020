`timescale 1ns/1ps
`default_nettype none
// Context (op3) reuses the existing INTEGER service and registry. ADD/RESIZE
// retain their original engine. The accepted owner stays selected until clear
// or the next idle command; changing offered descriptors cannot switch DMA.
module r14_integer_context_v221 #(
 parameter [31:0] ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000
)(
 input wire clk,rst_n,cancel,clear,cmd_valid,output wire cmd_ready,
 input wire [1:0] cmd_op,input wire [9:0] cmd_width,cmd_height,input wire [7:0] cmd_channels,
 input wire cmd_a_wide,cmd_b_wide,cmd_destination_wide,cmd_relu_a,cmd_relu_b,
 input wire [4:0] cmd_fraction_a,cmd_fraction_b,cmd_target_fraction,
 input wire [31:0] source_a,source_b,prior_address,destination,
 input wire [31:0] source_a_bytes,source_b_bytes,destination_bytes,timeout_cycles,
 output wire done,cancelled,output wire [15:0] fault,
 output wire busy,quiescent,needs_fabric_reset,
 output wire [31:0] elapsed_cycles,read_beats,write_beats,output_values,
 output wire [379:0] service_request,input wire [451:0] service_response
);
 reg owner_context,context_bad;
 wire legacy_ready,legacy_done,legacy_cancelled,legacy_busy,legacy_quiet,legacy_reset;
 wire [15:0] legacy_fault;
 wire [31:0] legacy_cycles,legacy_reads,legacy_writes,legacy_values;
 wire [379:0] legacy_request;
 wire context_busy,context_quiet,context_done,context_fault,bridge_quiet,bridge_reset;
 wire [3:0] context_fault_code;
 wire [379:0] context_request;
 reg [31:0] context_cycles,context_reads,context_writes;
 wire idle=legacy_quiet && !legacy_busy && !context_busy && bridge_quiet;
 wire selected_context=idle && cmd_valid && !clear && !cancel && !context_bad &&
      legacy_fault==0 && !context_fault?cmd_op==3:owner_context;
 wire context_ready=idle && !context_bad && !context_fault && legacy_fault==0 && !clear && !cancel;
 assign cmd_ready=selected_context?context_ready:legacy_ready && !context_busy && bridge_quiet;
 wire take=cmd_valid && cmd_ready;
 wire bad_context=source_a_bytes!=81920 || source_b_bytes!=81920 || destination_bytes!=296960 ||
      cmd_a_wide || cmd_b_wide || cmd_destination_wide || cmd_relu_a || cmd_relu_b ||
      cmd_fraction_a!=0 || cmd_fraction_b!=0 || cmd_target_fraction!=0;
 wire context_start=take && selected_context && !bad_context;
 wire context_cancelled=context_fault && (context_fault_code==3 || context_fault_code==4);
 assign busy=selected_context?(context_busy || !bridge_quiet):legacy_busy;
 assign quiescent=selected_context?(context_quiet && bridge_quiet):legacy_quiet;
 assign needs_fabric_reset=selected_context?bridge_reset:legacy_reset;
 assign done=selected_context?context_done:legacy_done;
 // Context cancellation is a level until clear reaches the child.
 // Suppress that acknowledged level on clear so MMIO cannot relatch
 // its cancelled sticky bit while the child and owner are resetting.
 assign cancelled=selected_context?(context_cancelled && bridge_quiet && !clear):legacy_cancelled;
 assign fault=selected_context?(context_bad?16'h2201:
     context_fault && !context_cancelled && bridge_quiet?{8'h22,4'd0,context_fault_code}:16'd0):legacy_fault;
 assign elapsed_cycles=selected_context?context_cycles:legacy_cycles;
 assign read_beats=selected_context?context_reads:legacy_reads;
 assign write_beats=selected_context?context_writes:legacy_writes;
 assign output_values=selected_context?context_writes<<2:legacy_values;
 // Descriptor admission may preselect a different idle child. Neither
 // child offers DMA until the accepting edge also registers its owner.
 // Route real transfers/cancellation by that retained owner, removing
 // backend quiet -> admission -> request mux -> backend control feedback.
 assign service_request=owner_context?context_request:legacy_request;

 // selected_context equals owner_context whenever cancel is asserted.
 // Use the registered owner directly to remove the idle-selection feedback.
 r14_tensor_dma_v1 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) legacy(
  .clk(clk),.rst_n(rst_n),.cancel(cancel && !owner_context),.clear(clear && !owner_context),
  .cmd_valid(cmd_valid && !selected_context && !context_busy && bridge_quiet),.cmd_ready(legacy_ready),
  .cmd_op(cmd_op),.cmd_width(cmd_width),.cmd_height(cmd_height),.cmd_channels(cmd_channels),
  .cmd_a_wide(cmd_a_wide),.cmd_b_wide(cmd_b_wide),.cmd_destination_wide(cmd_destination_wide),
  .cmd_relu_a(cmd_relu_a),.cmd_relu_b(cmd_relu_b),.cmd_fraction_a(cmd_fraction_a),
  .cmd_fraction_b(cmd_fraction_b),.cmd_target_fraction(cmd_target_fraction),
  .source_a(source_a),.source_b(source_b),.destination(destination),
  .source_a_bytes(source_a_bytes),.source_b_bytes(source_b_bytes),.destination_bytes(destination_bytes),
  .timeout_cycles(timeout_cycles),.done(legacy_done),.cancelled(legacy_cancelled),.fault(legacy_fault),
  .busy(legacy_busy),.quiescent(legacy_quiet),.needs_fabric_reset(legacy_reset),
  .elapsed_cycles(legacy_cycles),.read_beats(legacy_reads),.write_beats(legacy_writes),
  .output_values(legacy_values),.service_request(legacy_request),
  .service_response(service_response));

 wire [31:0] araddr,awaddr;
 wire arvalid,arready,rvalid,rready,awvalid,awready,wvalid,wready,bvalid,bready;
 wire [63:0] rdata,wdata;wire [7:0] wstrb;wire [1:0] rresp,bresp;
 r14_context_dma_v208 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END),.CHECK_BUSY_START(0)) producer(
  .clk(clk),.rst_n(rst_n),.clear(clear && owner_context && bridge_quiet),
  .cancel(cancel && owner_context),.start(context_start),
  .left_address(source_a),.right_address(source_b),.prior_address(prior_address),
  .output_address(destination),.cycle_limit(timeout_cycles),.busy(context_busy),.quiescent(context_quiet),
  .done(context_done),.fault(context_fault),.fault_code(context_fault_code),
  .m_axi_araddr(araddr),.m_axi_arlen(),.m_axi_arsize(),.m_axi_arburst(),
  .m_axi_arvalid(arvalid),.m_axi_arready(arready),.m_axi_rdata(rdata),.m_axi_rresp(rresp),
  .m_axi_rlast(1'b1),.m_axi_rvalid(rvalid),.m_axi_rready(rready),
  .m_axi_awaddr(awaddr),.m_axi_awlen(),.m_axi_awsize(),.m_axi_awburst(),
  .m_axi_awvalid(awvalid),.m_axi_awready(awready),.m_axi_wdata(wdata),.m_axi_wstrb(wstrb),
  .m_axi_wlast(),.m_axi_wvalid(wvalid),.m_axi_wready(wready),
  .m_axi_bresp(bresp),.m_axi_bvalid(bvalid),.m_axi_bready(bready));
 r14_context_service_bridge_v221 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) adapter(
  .clk(clk),.rst_n(rst_n),.araddr(araddr),.arvalid(arvalid),.arready(arready),
  .rdata(rdata),.rresp(rresp),.rvalid(rvalid),.rready(rready),
  .awaddr(awaddr),.awvalid(awvalid),.awready(awready),
  .wdata(wdata),.wstrb(wstrb),.wvalid(wvalid),.wready(wready),
  .bresp(bresp),.bvalid(bvalid),.bready(bready),.quiescent(bridge_quiet),.needs_fabric_reset(bridge_reset),
  .service_request(context_request),.service_response(service_response));
 always @(posedge clk)begin
  if(!rst_n)begin
   owner_context<=0;context_bad<=0;context_cycles<=0;context_reads<=0;context_writes<=0;
  end else begin
   if(clear && idle)begin owner_context<=0;context_bad<=0;end
   if(take)begin
    owner_context<=selected_context;
    if(selected_context)begin
     context_bad<=bad_context;context_cycles<=0;context_reads<=0;context_writes<=0;
    end
   end
   if(owner_context && context_busy && context_cycles!=32'hffffffff)context_cycles<=context_cycles+1;
   if(owner_context && service_response[111])context_reads<=context_reads+1;
   if(owner_context && service_response[451])context_writes<=context_writes+1;
  end
 end
 // context_start requires context_ready, hence idle and !context_busy.
 // The child may omit its impossible busy-start fault cone in this parent.
 // synthesis translate_off
 always @(posedge clk)if(rst_n && context_start && context_busy)
  $fatal(1,"Context producer started while busy despite parent admission");
 // A descriptor may change the preselection only across an idle boundary.
 // Cancel bits may differ for a drained legacy FAILED state; the shared
 // fabric has no live INTEGER transfer there. Commands/data/return strobes
 // must all be inactive until the accepted owner has been registered.
 always @(posedge clk)if(rst_n && selected_context!=owner_context &&
     (|{legacy_request[1],legacy_request[123],legacy_request[124],
        legacy_request[191],legacy_request[313],legacy_request[314],
        context_request[1],context_request[123],context_request[124],
        context_request[191],context_request[313],context_request[314]}))
  $fatal(1,"INTEGER owner preselection overlapped live DMA traffic");
 // synthesis translate_on
endmodule
`default_nettype wire
