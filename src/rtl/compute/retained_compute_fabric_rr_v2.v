`timescale 1ns/1ps
`default_nettype none
module retained_compute_fabric_rr_v2 #(parameter CLIENTS=1,parameter[31:0]ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000)(
 input wire clk,rst_n,input wire[CLIENTS*190-1:0]request,input wire[CLIENTS-1:0]permit,
 output wire[CLIENTS*226-1:0]response,output wire quiescent,
    output wire [31:0] m_axi_araddr,output wire [7:0] m_axi_arlen,
    output wire [2:0] m_axi_arsize,output wire [1:0] m_axi_arburst,
    output wire m_axi_arvalid,input wire m_axi_arready,
    input wire [63:0] m_axi_rdata,input wire [1:0] m_axi_rresp,
    input wire m_axi_rlast,input wire m_axi_rvalid,output wire m_axi_rready,
    output wire [31:0] m_axi_awaddr,output wire [7:0] m_axi_awlen,
    output wire [2:0] m_axi_awsize,output wire [1:0] m_axi_awburst,
    output wire m_axi_awvalid,input wire m_axi_awready,
    output wire [63:0] m_axi_wdata,output wire [7:0] m_axi_wstrb,
    output wire m_axi_wlast,output wire m_axi_wvalid,input wire m_axi_wready,
    input wire [1:0] m_axi_bresp,input wire m_axi_bvalid,output wire m_axi_bready

);
 wire[CLIENTS*113-1:0]read_response,write_response;wire read_quiet,write_quiet;
 assign quiescent=read_quiet && write_quiet;
 genvar g;generate for(g=0;g<CLIENTS;g=g+1)begin:REPLY
  assign response[g*226+:226]={write_response[g*113+:113],read_response[g*113+:113]};
 end endgenerate
 retained_compute_dma_side_rr_v2 #(.CLIENTS(CLIENTS),.WRITE(0),.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) reader(
  .clk(clk),.rst_n(rst_n),.request(request),.permit(permit),.response(read_response),.backend_quiescent(read_quiet),
  .m_axi_araddr(m_axi_araddr),.m_axi_arlen(m_axi_arlen),.m_axi_arsize(m_axi_arsize),.m_axi_arburst(m_axi_arburst),.m_axi_arvalid(m_axi_arvalid),.m_axi_arready(m_axi_arready),.m_axi_rdata(m_axi_rdata),.m_axi_rresp(m_axi_rresp),.m_axi_rlast(m_axi_rlast),.m_axi_rvalid(m_axi_rvalid),.m_axi_rready(m_axi_rready),.m_axi_awaddr(),.m_axi_awlen(),.m_axi_awsize(),.m_axi_awburst(),.m_axi_awvalid(),.m_axi_awready(1'b0),.m_axi_wdata(),.m_axi_wstrb(),.m_axi_wlast(),.m_axi_wvalid(),.m_axi_wready(1'b0),.m_axi_bresp(2'd0),.m_axi_bvalid(1'b0),.m_axi_bready());
 retained_compute_dma_side_rr_v2 #(.CLIENTS(CLIENTS),.WRITE(1),.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) writer(
  .clk(clk),.rst_n(rst_n),.request(request),.permit(permit),.response(write_response),.backend_quiescent(write_quiet),
  .m_axi_araddr(),.m_axi_arlen(),.m_axi_arsize(),.m_axi_arburst(),.m_axi_arvalid(),.m_axi_arready(1'b0),.m_axi_rdata(64'd0),.m_axi_rresp(2'd0),.m_axi_rlast(1'b0),.m_axi_rvalid(1'b0),.m_axi_rready(),.m_axi_awaddr(m_axi_awaddr),.m_axi_awlen(m_axi_awlen),.m_axi_awsize(m_axi_awsize),.m_axi_awburst(m_axi_awburst),.m_axi_awvalid(m_axi_awvalid),.m_axi_awready(m_axi_awready),.m_axi_wdata(m_axi_wdata),.m_axi_wstrb(m_axi_wstrb),.m_axi_wlast(m_axi_wlast),.m_axi_wvalid(m_axi_wvalid),.m_axi_wready(m_axi_wready),.m_axi_bresp(m_axi_bresp),.m_axi_bvalid(m_axi_bvalid),.m_axi_bready(m_axi_bready));
endmodule
`default_nettype wire
