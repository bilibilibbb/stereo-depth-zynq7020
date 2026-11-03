`ifndef FW_MEM_STRESS
`define FW_MEM_STRESS 1
`endif
`ifndef FW_MEM_READ_FIRST
`define FW_MEM_READ_FIRST 8
`endif
`ifndef FW_MEM_READ_GAP
`define FW_MEM_READ_GAP 0
`endif
`ifndef FW_MEM_WRITE_GAP
`define FW_MEM_WRITE_GAP 1
`endif
`ifndef FW_MEM_WRITE_RESPONSE
`define FW_MEM_WRITE_RESPONSE 6
`endif
`timescale 1ns/1ps
`default_nettype none
// Verification-only shared fabric and independently stalled AXI memory.
module fw_nn_dma_axi_fixture #(parameter integer ADDRESS_END=131072,parameter EXTERNAL_AXI=0,parameter integer WATCHDOG_CYCLES=2000000)(
 input wire clk,rst_n,input wire[189:0]request,output wire[225:0]response,output wire poisoned,
 input wire background_enabled,input wire hold_ar,hold_r,hold_aw,hold_w,hold_b,
 input wire read_error,write_error,input wire[1:0]last_error,
 input wire[31:0] x_araddr,x_awaddr,input wire[7:0] x_arlen,x_awlen,
 input wire[2:0] x_arsize,x_awsize,input wire[1:0] x_arburst,x_awburst,
 input wire x_arvalid,x_awvalid,output wire x_arready,x_awready,
 output wire[63:0] x_rdata,output wire[1:0] x_rresp,x_bresp,
 output wire x_rlast,x_rvalid,x_bvalid,input wire x_rready,x_bready,
 input wire[63:0] x_wdata,input wire[7:0] x_wstrb,input wire x_wlast,x_wvalid,output wire x_wready
);
 wire[189:0]background_request;wire[451:0]replies;wire[1:0]poisons;wire quiet;
 wire bg_valid,bg_done,bg_idle;wire[63:0]bg_data;
 integer background_completions=0,background_values=0,background_index=0;
 assign response=replies[225:0];assign poisoned=poisons[0];
 wire[31:0]araddr,awaddr;wire[7:0]arlen,awlen,wstrb;wire[2:0]arsize,awsize;wire[1:0]arburst,awburst;
 wire arvalid,arready,awvalid,awready,wvalid,wready,wlast,rready,bready;wire[63:0]wdata;
 reg[63:0]rdata=0;reg[1:0]rresp=0,bresp=0;reg rlast=0,rvalid=0,bvalid=0;
 generate if(!EXTERNAL_AXI)begin: INTERNAL_FABRIC
 fw_shared_dma_fabric #(.CLIENTS(2),.ADDRESS_MIN(0),.ADDRESS_END(ADDRESS_END)) fabric(
  .clk(clk),.rst_n(rst_n),.request({background_request,request}),.response(replies),.permit(2'b11),.quiescent(quiet),.poisoned(poisons),
  .m_axi_araddr(araddr),.m_axi_arlen(arlen),.m_axi_arsize(arsize),.m_axi_arburst(arburst),.m_axi_arvalid(arvalid),.m_axi_arready(arready),
  .m_axi_rdata(rdata),.m_axi_rresp(rresp),.m_axi_rlast(rlast),.m_axi_rvalid(rvalid),.m_axi_rready(rready),
  .m_axi_awaddr(awaddr),.m_axi_awlen(awlen),.m_axi_awsize(awsize),.m_axi_awburst(awburst),.m_axi_awvalid(awvalid),.m_axi_awready(awready),
  .m_axi_wdata(wdata),.m_axi_wstrb(wstrb),.m_axi_wlast(wlast),.m_axi_wvalid(wvalid),.m_axi_wready(wready),.m_axi_bresp(bresp),.m_axi_bvalid(bvalid),.m_axi_bready(bready));
 fw_color_shared_transport #(.ADDRESS_MIN(0),.ADDRESS_END(ADDRESS_END)) background(
  .clk(clk),.rst_n(rst_n),.req_valid(background_enabled),.req_ready(),.req_write(1'b0),.req_addr(32'd98304),.req_words(5'd8),
  .w_valid(1'b0),.w_ready(),.w_data(64'd0),.r_valid(bg_valid),.r_ready(1'b1),.r_data(bg_data),.done(bg_done),.fault(),.idle(bg_idle),.poisoned(),
  .service_request(background_request),.service_response(replies[451:226]),.service_poisoned(poisons[1]));
 end else begin: EXTERNAL_MEMORY
  assign replies=0;assign poisons=0;assign quiet=1;assign background_request=0;
  assign bg_valid=0;assign bg_done=0;assign bg_idle=1;assign bg_data=0;
  assign araddr=x_araddr;assign arlen=x_arlen;assign arsize=x_arsize;assign arburst=x_arburst;assign arvalid=x_arvalid;assign rready=x_rready;
  assign awaddr=x_awaddr;assign awlen=x_awlen;assign awsize=x_awsize;assign awburst=x_awburst;assign awvalid=x_awvalid;assign bready=x_bready;
  assign wdata=x_wdata;assign wstrb=x_wstrb;assign wlast=x_wlast;assign wvalid=x_wvalid;
 end endgenerate
 assign x_arready=arready;assign x_awready=awready;assign x_wready=wready;
 assign x_rdata=rdata;assign x_rresp=rresp;assign x_rlast=rlast;assign x_rvalid=rvalid;assign x_bresp=bresp;assign x_bvalid=bvalid;
 reg[63:0]memory[0:ADDRESS_END/8-1],pending_write[0:15];
 reg read_pending=0,write_pending=0,write_complete=0,read_failed=0,write_failed=0;reg[1:0]last_failure=0;
 integer cycle=0,ra=0,rn=0,ri=0,wa=0,wn=0,wi=0,read_delay=0,write_delay=0;
 integer write_gap=0;
 integer read_bursts=0,write_bursts=0,read_words=0,write_words=0,boundary_splits=0,j;
 assign arready=!read_pending && !rvalid && !hold_ar && (!`FW_MEM_STRESS || cycle%5!=0);
 assign awready=!write_pending && !bvalid && !hold_aw && (!`FW_MEM_STRESS || cycle%7!=0);
 assign wready=write_pending && !write_complete && !hold_w && (`FW_MEM_STRESS ? cycle%4!=0 : write_gap==0);
 reg ar_held=0,aw_held=0,w_held=0;reg[39:0]last_ar,last_aw;reg[72:0]last_w;
 always @(posedge clk)begin
  cycle<=cycle+1;if(WATCHDOG_CYCLES>0 && cycle>WATCHDOG_CYCLES)$fatal(1,"DMA fixture watchdog");
  if(rst_n)begin
   if(write_gap>0)write_gap<=write_gap-1;
   if(ar_held && (!arvalid || {araddr,arlen}!==last_ar))$fatal(1,"AR changed under stall/cancel");
   if(aw_held && (!awvalid || {awaddr,awlen}!==last_aw))$fatal(1,"AW changed under stall/cancel");
   if(w_held && (!wvalid || {wdata,wstrb,wlast}!==last_w))$fatal(1,"W changed under stall/cancel");
   ar_held<=arvalid && !arready;aw_held<=awvalid && !awready;w_held<=wvalid && !wready;
   last_ar<={araddr,arlen};last_aw<={awaddr,awlen};last_w<={wdata,wstrb,wlast};
   if(bg_valid)begin
    if(background_index>=8 || bg_data!==64'hb000000000000000+background_index)$fatal(1,"background owner/data");
    background_index<=background_index+1;background_values<=background_values+1;
   end
   if(bg_done)begin if(background_index!=8)$fatal(1,"background length");background_completions<=background_completions+1;background_index<=0;end
   if(arvalid && arready)begin
    if(araddr[2:0]!=0 || araddr>=ADDRESS_END || araddr+(arlen+1)*8>ADDRESS_END || arsize!=3 || arburst!=1 || arlen>15 || araddr[11:0]+(arlen+1)*8>4096)$fatal(1,"DMA READ address/burst");
    read_pending<=1;ra<=araddr/8;rn<=arlen+1;ri<=0;read_delay<=`FW_MEM_STRESS?1:`FW_MEM_READ_FIRST;read_bursts<=read_bursts+1;
    read_failed<=read_error;last_failure<=last_error;
    if(araddr[11:0]==0)boundary_splits<=boundary_splits+1;
   end
   if(read_pending && !rvalid && !hold_r)begin
    if(read_delay!=0)read_delay<=read_delay-1;
    else begin rdata<=memory[ra+ri];rlast<=last_failure==1?1:last_failure==2?0:ri+1==rn;rresp<=read_failed && ri==0?2:0;rvalid<=1;end
   end
   if(rvalid && rready)begin
    rvalid<=0;read_words<=read_words+1;if(ri+1==rn)read_pending<=0;else ri<=ri+1;read_delay<=`FW_MEM_STRESS?(cycle%3==0?1:0):`FW_MEM_READ_GAP;
   end
   if(awvalid && awready)begin
    if(awaddr[2:0]!=0 || awaddr>=ADDRESS_END || awaddr+(awlen+1)*8>ADDRESS_END || awsize!=3 || awburst!=1 || awlen>15 || awaddr[11:0]+(awlen+1)*8>4096)$fatal(1,"DMA WRITE address/burst");
    write_pending<=1;write_complete<=0;wa<=awaddr/8;wn<=awlen+1;wi<=0;write_bursts<=write_bursts+1;write_failed<=write_error;
    if(awaddr[11:0]==0)boundary_splits<=boundary_splits+1;
   end
   if(wvalid && wready)begin
    write_gap<=`FW_MEM_WRITE_GAP;
    if(wstrb!=255 || wlast!=(wi+1==wn))$fatal(1,"DMA WRITE framing");
    pending_write[wi]<=wdata;wi<=wi+1;write_words<=write_words+1;
    if(wlast)begin write_complete<=1;write_delay<=`FW_MEM_STRESS?2+cycle%3:`FW_MEM_WRITE_RESPONSE;end
   end
   if(write_complete && !bvalid && !hold_b)begin
    if(write_delay!=0)write_delay<=write_delay-1;
    else begin bvalid<=1;bresp<=write_failed?2:0;end
   end
   if(bvalid && bready)begin
    // Visibility only after B; an error can still leave a dirty DDR prefix.
    for(j=0;j<wn;j=j+1)memory[wa+j]<=pending_write[j];
    bvalid<=0;write_pending<=0;write_complete<=0;
   end
  end
 end
endmodule
`default_nettype wire
