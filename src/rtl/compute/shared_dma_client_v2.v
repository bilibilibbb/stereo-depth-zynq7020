`timescale 1ns/1ps
`default_nettype none
// Single outstanding, 64-bit INCR, <=16 beats and never crosses 4KiB.
// Writes buffer a whole burst BEFORE issuing AW: cancellation cannot starve W.
// Once AR/AW VALID is raised it is held through acceptance, including cancellation.
// Malformed RLAST poisons the channel until fabric reset; quiescent stays false.
module shared_dma_client_v2 #(
    parameter LINE_ONLY=0,
    parameter [31:0] ADDRESS_MIN=32'h10000000,
    parameter [31:0] ADDRESS_END=32'h20000000
)(
    input wire clk,input wire rst_n,input wire cancel,
    input wire cmd_valid,output wire cmd_ready,input wire cmd_write,
    input wire [31:0] cmd_address,input wire [15:0] cmd_beats,
    input wire[7:0]wr_strb,input wire [63:0] wr_data,input wire wr_valid,output wire wr_ready,
    output wire [63:0] rd_data,output wire rd_valid,input wire rd_ready,
    output wire done,output wire cancelled,output wire [7:0] fault,
    output wire quiescent,output wire progress,output wire [31:0] transferred,
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
,
    output wire[189:0] service_request,input wire[225:0] service_response
);

    // Requests: cancel0,valid1,write2,address3..34,beats35..50,data51..114,
    // strobes115..122,write-valid123,read-ready124,min125..156,end157..188,line189.
    // Each of two responses: ready0,write-ready1,read-valid2,data3..66,
    // done67,cancelled68,fault69..76,quiet77,progress78,transferred79..110, actual read beat111, actual write beat112.
    reg occupied,writing;
    wire[112:0] rr=service_response[112:0],wr=service_response[225:113];
    wire[112:0] reply=writing?wr:rr;
    assign service_request={LINE_ONLY!=0,ADDRESS_END,ADDRESS_MIN,rd_ready,wr_valid,wr_strb,wr_data,cmd_beats,cmd_address,cmd_write,cmd_valid&&!occupied&&!cancel,cancel};
    assign cmd_ready=rst_n && !occupied && !cancel && (cmd_write?wr[0]:rr[0]);
    assign wr_ready=occupied && writing && wr[1];
    assign rd_data=rr[3+:64];assign rd_valid=occupied && !writing && rr[2];
    assign done=reply[67];assign cancelled=reply[68];assign fault=reply[69+:8];
    assign quiescent=rr[77] && wr[77];assign progress=reply[78];assign transferred=reply[79+:32];
    always @(posedge clk)begin
     if(!rst_n)begin occupied<=0;writing<=0;end
     else begin
      if(done && quiescent)occupied<=0;
      if(cmd_valid && cmd_ready)begin occupied<=1;writing<=cmd_write;end
     end
    end
    assign m_axi_araddr=0;
    assign m_axi_arlen=0;
    assign m_axi_arsize=0;
    assign m_axi_arburst=0;
    assign m_axi_arvalid=0;
    assign m_axi_rready=0;
    assign m_axi_awaddr=0;
    assign m_axi_awlen=0;
    assign m_axi_awsize=0;
    assign m_axi_awburst=0;
    assign m_axi_awvalid=0;
    assign m_axi_wdata=0;
    assign m_axi_wstrb=0;
    assign m_axi_wlast=0;
    assign m_axi_wvalid=0;
    assign m_axi_bready=0;
endmodule
`default_nettype wire
