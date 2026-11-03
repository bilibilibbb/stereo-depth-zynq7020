`timescale 1ns/1ps
`default_nettype none
module box_field_fabric_v149 #(parameter SPLIT=1,parameter[31:0]ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000)(
 input wire clk,rst_n,input wire[1899:0]request,input wire[9:0]permit,output wire[2259:0]response,output wire quiescent,
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
    input wire [1:0] m_axi_bresp,input wire m_axi_bvalid,output wire m_axi_bready,
output wire [31:0] fp_axi_araddr,output wire [7:0] fp_axi_arlen,
    output wire [2:0] fp_axi_arsize,output wire [1:0] fp_axi_arburst,
    output wire fp_axi_arvalid,input wire fp_axi_arready,
    input wire [63:0] fp_axi_rdata,input wire [1:0] fp_axi_rresp,
    input wire fp_axi_rlast,input wire fp_axi_rvalid,output wire fp_axi_rready,
    output wire [31:0] fp_axi_awaddr,output wire [7:0] fp_axi_awlen,
    output wire [2:0] fp_axi_awsize,output wire [1:0] fp_axi_awburst,
    output wire fp_axi_awvalid,input wire fp_axi_awready,
    output wire [63:0] fp_axi_wdata,output wire [7:0] fp_axi_wstrb,
    output wire fp_axi_wlast,output wire fp_axi_wvalid,input wire fp_axi_wready,
    input wire [1:0] fp_axi_bresp,input wire fp_axi_bvalid,output wire fp_axi_bready);
generate if(SPLIT==1)begin:DUAL
 wire quiet0,quiet1;wire[1807:0]processing_response;assign quiescent=quiet0&&quiet1;
 // Keep the validated power-of-two RR encoder; two inactive slots have no
 // permit/request and cannot acquire ownership. The six real clients retain
 // their original indices and complete read/write response bundles.
 assign response[1355:0]=processing_response[1355:0];
 retained_compute_fabric_rr_v2 #(.CLIENTS(8),.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) processing(
 .clk(clk),.rst_n(rst_n),.request({380'd0,request[1139:0]}),.response(processing_response),.permit({2'b00,permit[5:0]}),.quiescent(quiet0),.m_axi_araddr(m_axi_araddr),.m_axi_arlen(m_axi_arlen),.m_axi_arsize(m_axi_arsize),.m_axi_arburst(m_axi_arburst),.m_axi_arvalid(m_axi_arvalid),.m_axi_arready(m_axi_arready),.m_axi_rdata(m_axi_rdata),.m_axi_rresp(m_axi_rresp),.m_axi_rlast(m_axi_rlast),.m_axi_rvalid(m_axi_rvalid),.m_axi_rready(m_axi_rready),.m_axi_awaddr(m_axi_awaddr),.m_axi_awlen(m_axi_awlen),.m_axi_awsize(m_axi_awsize),.m_axi_awburst(m_axi_awburst),.m_axi_awvalid(m_axi_awvalid),.m_axi_awready(m_axi_awready),.m_axi_wdata(m_axi_wdata),.m_axi_wstrb(m_axi_wstrb),.m_axi_wlast(m_axi_wlast),.m_axi_wvalid(m_axi_wvalid),.m_axi_wready(m_axi_wready),.m_axi_bresp(m_axi_bresp),.m_axi_bvalid(m_axi_bvalid),.m_axi_bready(m_axi_bready));
 retained_compute_fabric_rr_v2 #(.CLIENTS(4),.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) filters(
 .clk(clk),.rst_n(rst_n),.request(request[1899:1140]),.response(response[2259:1356]),.permit(permit[9:6]),.quiescent(quiet1),.m_axi_araddr(fp_axi_araddr),.m_axi_arlen(fp_axi_arlen),.m_axi_arsize(fp_axi_arsize),.m_axi_arburst(fp_axi_arburst),.m_axi_arvalid(fp_axi_arvalid),.m_axi_arready(fp_axi_arready),.m_axi_rdata(fp_axi_rdata),.m_axi_rresp(fp_axi_rresp),.m_axi_rlast(fp_axi_rlast),.m_axi_rvalid(fp_axi_rvalid),.m_axi_rready(fp_axi_rready),.m_axi_awaddr(fp_axi_awaddr),.m_axi_awlen(fp_axi_awlen),.m_axi_awsize(fp_axi_awsize),.m_axi_awburst(fp_axi_awburst),.m_axi_awvalid(fp_axi_awvalid),.m_axi_awready(fp_axi_awready),.m_axi_wdata(fp_axi_wdata),.m_axi_wstrb(fp_axi_wstrb),.m_axi_wlast(fp_axi_wlast),.m_axi_wvalid(fp_axi_wvalid),.m_axi_wready(fp_axi_wready),.m_axi_bresp(fp_axi_bresp),.m_axi_bvalid(fp_axi_bvalid),.m_axi_bready(fp_axi_bready));
end endgenerate
// synthesis translate_off
 initial if(SPLIT!=1)$fatal(1,"Independent domains require the two-port route");
 // synthesis translate_on
endmodule
`default_nettype wire
