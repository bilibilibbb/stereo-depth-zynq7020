`timescale 1ns/1ps
`default_nettype none
// Actual frozen master and the existing independently stalled memory fixture.
// This tests the short-burst precondition, not PS7 or the vendor converter.
module short_burst_dma_tb;
reg clk=0; always #5 clk=~clk;
reg rst_n=0,cancel=0,cmd_valid=0,cmd_write=0;
reg [31:0] cmd_address=0;reg [15:0] cmd_beats=0;
wire cmd_ready,wr_ready,rd_valid,done,cancelled,quiescent;
wire [7:0] fault;wire [31:0] transferred;wire [63:0] rd_data;
integer sent=0,received=0,cases=0,cycles=0,checks=0;
wire wr_valid=(cycles%5!=0);wire rd_ready=(cycles%7!=0);
wire [63:0] wr_data=64'hc000000000000000+cmd_address/8+sent;
wire [31:0] araddr,awaddr;wire [7:0] arlen,awlen,wstrb;
wire [2:0] arsize,awsize;wire [1:0] arburst,awburst,rresp,bresp;
wire arvalid,arready,awvalid,awready,rvalid,rready,rlast,wvalid,wready,wlast,bvalid,bready;
wire [63:0] rdata,wdata;
reg hold_ar=0,hold_r=0,hold_aw=0,hold_w=0,hold_b=0;
reg read_error=0,write_error=0;reg [1:0] last_error=0;
`DMA_MODULE #(.ADDRESS_MIN(0),.ADDRESS_END(65536)) dut(
 .clk(clk),.rst_n(rst_n),.cancel(cancel),.cmd_valid(cmd_valid),.cmd_ready(cmd_ready),
 .cmd_write(cmd_write),.cmd_address(cmd_address),.cmd_beats(cmd_beats),
 .wr_data(wr_data),.wr_valid(wr_valid),.wr_ready(wr_ready),
`ifdef COMPUTE_DMA
 .wr_strb(8'hff),.poisoned(),
`endif
`ifdef LEGACY_DMA
 .wr_strb(8'hff),
`endif
 .rd_data(rd_data),.rd_valid(rd_valid),.rd_ready(rd_ready),.done(done),.cancelled(cancelled),
 .fault(fault),.quiescent(quiescent),.progress(),.transferred(transferred),
 .m_axi_araddr(araddr),.m_axi_arlen(arlen),.m_axi_arsize(arsize),.m_axi_arburst(arburst),
 .m_axi_arvalid(arvalid),.m_axi_arready(arready),.m_axi_rdata(rdata),.m_axi_rresp(rresp),
 .m_axi_rlast(rlast),.m_axi_rvalid(rvalid),.m_axi_rready(rready),
 .m_axi_awaddr(awaddr),.m_axi_awlen(awlen),.m_axi_awsize(awsize),.m_axi_awburst(awburst),
 .m_axi_awvalid(awvalid),.m_axi_awready(awready),.m_axi_wdata(wdata),.m_axi_wstrb(wstrb),
 .m_axi_wlast(wlast),.m_axi_wvalid(wvalid),.m_axi_wready(wready),
 .m_axi_bresp(bresp),.m_axi_bvalid(bvalid),.m_axi_bready(bready));
fw_nn_dma_axi_fixture #(.EXTERNAL_AXI(1),.ADDRESS_END(65536),.WATCHDOG_CYCLES(300000)) mem(
 .clk(clk),.rst_n(rst_n),.request(190'd0),.response(),.poisoned(),.background_enabled(1'b0),
 .hold_ar(hold_ar),.hold_r(hold_r),.hold_aw(hold_aw),.hold_w(hold_w),.hold_b(hold_b),
 .read_error(read_error),.write_error(write_error),.last_error(last_error),
 .x_araddr(araddr),.x_arlen(arlen),.x_arsize(arsize),.x_arburst(arburst),.x_arvalid(arvalid),.x_arready(arready),
 .x_rdata(rdata),.x_rresp(rresp),.x_rlast(rlast),.x_rvalid(rvalid),.x_rready(rready),
 .x_awaddr(awaddr),.x_awlen(awlen),.x_awsize(awsize),.x_awburst(awburst),.x_awvalid(awvalid),.x_awready(awready),
 .x_wdata(wdata),.x_wstrb(wstrb),.x_wlast(wlast),.x_wvalid(wvalid),.x_wready(wready),
 .x_bresp(bresp),.x_bvalid(bvalid),.x_bready(bready));
always @(posedge clk)begin
 cycles<=cycles+1;
 if(rst_n)begin
  if(wr_valid&&wr_ready)sent<=sent+1;
  if(rd_valid&&rd_ready)begin
   if(rd_data!==64'hc000000000000000+cmd_address/8+received)$fatal(1,"read payload/order");
   received<=received+1;
  end
  // Check every VALID cycle, including held addresses; fixture also checks
  // accepted transactions, WLAST and stable address/data during cancellation.
  if(arvalid && (arlen>15 || araddr[2:0]!=0 || arsize!=3 || arburst!=1 || araddr[11:0]+(arlen+1)*8>4096))$fatal(1,"short AR precondition");
  if(awvalid && (awlen>15 || awaddr[2:0]!=0 || awsize!=3 || awburst!=1 || awaddr[11:0]+(awlen+1)*8>4096))$fatal(1,"short AW precondition");
 end
end
task issue(input integer writing,address,beats);begin
 @(negedge clk);if(!cmd_ready)$fatal(1,"not ready for next command");
 sent=0;received=0;cmd_write=writing;cmd_address=address;cmd_beats=beats;cmd_valid=1;
 @(negedge clk);cmd_valid=0;
end endtask
task finish(input integer error_code,was_cancelled);integer n;begin
 n=0;while(!done)begin @(negedge clk);n=n+1;if(n>100000)$fatal(1,"done timeout");end
 if(fault!==error_code || cancelled!==was_cancelled || !quiescent)$fatal(1,"completion/fault/drain %d %d",fault,cancelled);
 cases=cases+1;@(negedge clk);
end endtask
task normal(input integer writing,address,beats);integer i;begin
 issue(writing,address,beats);finish(0,0);
 if(transferred!=beats || (!writing&&received!=beats) || (writing&&sent!=beats))$fatal(1,"normal beat count");
 if(writing)for(i=0;i<beats;i=i+1)if(mem.memory[address/8+i]!==64'hc000000000000000+address/8+i)$fatal(1,"write data/order");
end endtask
task rejected(input integer address,beats);integer old_bursts;begin
 old_bursts=mem.read_bursts+mem.write_bursts;issue(0,address,beats);finish(1,0);
 if(old_bursts!=mem.read_bursts+mem.write_bursts)$fatal(1,"invalid command reached AXI");
end endtask
task stalled_cancel(input integer writing);begin
 hold_ar=!writing;hold_aw=writing;issue(writing,4088,33);
 while(!(writing?awvalid:arvalid))@(negedge clk);
 cancel=1;repeat(5)@(negedge clk);hold_ar=0;hold_aw=0;
 finish(0,1);cancel=0;@(negedge clk);normal(writing,4088,33);
end endtask
integer i,j,w,expected,count;
initial begin
 for(i=0;i<8192;i=i+1)mem.memory[i]=64'hc000000000000000+i;
 // All aligned offsets, every short count plus representative branch/limit
 // values. Source binding separately fixes the min(remaining,16,boundary).
 for(i=0;i<512;i=i+1)for(j=1;j<=18;j=j+1)begin
  count=j==18?4096:j;expected=count>16?16:count;
  if(expected>512-i)expected=512-i;
  if(dut.burst_size(i*8,count)!==expected)$fatal(1,"burst function");checks=checks+1;
 end
 repeat(4)@(negedge clk);rst_n=1;
 for(w=0;w<2;w=w+1)begin
  normal(w,0,1);normal(w,4088,15);normal(w,4000,16);normal(w,4088,17);
  normal(w,4080,33);normal(w,8,511);normal(w,4088,4096);
 end
 rejected(0,0);rejected(0,4097);rejected(1,16);rejected(65528,2);
 stalled_cancel(0);stalled_cancel(1);
 read_error=1;issue(0,4088,33);finish(2,0);read_error=0;normal(0,4088,33);
 write_error=1;issue(1,4088,33);finish(3,0);write_error=0;normal(1,4088,33);
 // A malformed final marker must poison; it cannot publish a fake completion.
 last_error=1;issue(0,0,16);repeat(100)@(negedge clk);
 if(fault!=4 || quiescent || done || arvalid || awvalid)$fatal(1,"poison semantics");
 $display("SHORT_BURST_PASS cases=%0d checks=%0d read_bursts=%0d write_bursts=%0d cycles=%0d poison=1",cases,checks,mem.read_bursts,mem.write_bursts,cycles);
 $finish;
end
endmodule
`default_nettype wire
