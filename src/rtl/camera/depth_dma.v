`timescale 1ns/1ps
`default_nettype none
// Single outstanding, 64-bit INCR, <=16 beats and never crosses 4KiB.
// Writes buffer a whole burst BEFORE issuing AW: cancellation cannot starve W.
// Once AR/AW VALID is raised it is held through acceptance, including cancellation.
// Malformed RLAST poisons the channel until fabric reset; quiescent stays false.
module depth_dma #(
    parameter [31:0] ADDRESS_MIN=32'h10000000,
    parameter [31:0] ADDRESS_END=32'h20000000
)(
    input wire clk,input wire rst_n,input wire cancel,
    input wire cmd_valid,output wire cmd_ready,input wire cmd_write,
    input wire [31:0] cmd_address,input wire [15:0] cmd_beats,
    input wire [63:0] wr_data,input wire wr_valid,output wire wr_ready,
    output wire [63:0] rd_data,output wire rd_valid,input wire rd_ready,
    output reg done,output reg cancelled,output reg [7:0] fault,
    output wire quiescent,output wire progress,output reg [31:0] transferred,
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
    localparam IDLE=0,AR=1,READ=2,FILL=3,AW=4,WRITE=5,BRESP=6,POISON=7;
    reg [3:0] state;
    reg [31:0] address;
    reg [15:0] remaining;
    reg [4:0] burst,index;
    reg killed;
    reg [63:0] buffer [0:15];
    wire stop=cancel || killed;
    wire [32:0] cmd_end={1'b0,cmd_address}+({17'b0,cmd_beats}<<3);
    function [4:0] burst_size;
        input [31:0] addr;input [15:0] left;
        reg [9:0] boundary;reg [15:0] n;
        begin boundary=(13'd4096-{1'b0,addr[11:0]})>>3;
            n=left>16?16:left;if(n>boundary)n=boundary;burst_size=n[4:0];end
    endfunction
    assign quiescent=state==IDLE;
    assign cmd_ready=quiescent && !cancel;
    assign wr_ready=state==FILL && !stop;
    assign rd_data=m_axi_rdata;
    assign rd_valid=state==READ && !stop && fault==0 && m_axi_rvalid && m_axi_rresp==0;
    assign m_axi_araddr=address;assign m_axi_arlen={3'b0,burst}-1'b1;
    assign m_axi_arsize=3;assign m_axi_arburst=1;assign m_axi_arvalid=state==AR;
    assign m_axi_rready=(state==READ && (stop || fault!=0 || m_axi_rresp!=0 || rd_ready)) || state==POISON;
    assign m_axi_awaddr=address;assign m_axi_awlen={3'b0,burst}-1'b1;
    assign m_axi_awsize=3;assign m_axi_awburst=1;assign m_axi_awvalid=state==AW;
    assign m_axi_wdata=buffer[index[3:0]];assign m_axi_wstrb=8'hff;
    assign m_axi_wlast=index==burst-1;assign m_axi_wvalid=state==WRITE;
    assign m_axi_bready=state==BRESP;
    assign progress=(m_axi_arvalid && m_axi_arready)||(m_axi_rvalid && m_axi_rready)||
        (m_axi_awvalid && m_axi_awready)||(m_axi_wvalid && m_axi_wready)||(m_axi_bvalid && m_axi_bready);
    always @(posedge clk) begin
        if(!rst_n) begin
            state<=IDLE;address<=0;remaining<=0;burst<=0;index<=0;killed<=0;
            done<=0;cancelled<=0;fault<=0;transferred<=0;
        end else begin
            done<=0;cancelled<=0;
            if(cancel && state!=IDLE)killed<=1;
            case(state)
            IDLE:if(cmd_valid && cmd_ready) begin
                killed<=0;fault<=0;transferred<=0;
                if(cmd_beats==0 || cmd_beats>4096 || cmd_address[2:0]!=0 || cmd_address<ADDRESS_MIN ||
                    cmd_end>{1'b0,ADDRESS_END} || cmd_end[32])begin fault<=1;done<=1;end
                else begin
                    address<=cmd_address;remaining<=cmd_beats;burst<=burst_size(cmd_address,cmd_beats);index<=0;
                    state<=cmd_write?FILL:AR;
                end
            end
            AR:if(m_axi_arready)begin state<=READ;index<=0;end
            READ:if(m_axi_rvalid && m_axi_rready)begin
                transferred<=transferred+1'b1;
                if(m_axi_rresp!=0 && fault==0)fault<=2;
                if(m_axi_rlast!=(index==burst-1)) begin fault<=fault==0?4:fault;state<=POISON;end
                else if(index==burst-1)begin
                    if(stop || fault!=0 || m_axi_rresp!=0 || remaining==burst)begin
                        state<=IDLE;done<=1;cancelled<=stop;
                    end else begin
                        address<=address+({27'b0,burst}<<3);remaining<=remaining-burst;
                        burst<=burst_size(address+({27'b0,burst}<<3),remaining-burst);index<=0;state<=AR;
                    end
                end else index<=index+1'b1;
            end
            FILL:if(stop)begin state<=IDLE;done<=1;cancelled<=1;end
                else if(wr_valid && wr_ready)begin
                    buffer[index[3:0]]<=wr_data;
                    if(index==burst-1)begin state<=AW;index<=0;end else index<=index+1'b1;
                end
            AW:if(m_axi_awready)begin state<=WRITE;index<=0;end
            WRITE:if(m_axi_wready)begin
                if(index==burst-1)begin state<=BRESP;index<=0;end else index<=index+1'b1;
            end
            BRESP:if(m_axi_bvalid)begin
                transferred<=transferred+burst;
                if(m_axi_bresp!=0 && fault==0)fault<=3;
                if(stop || m_axi_bresp!=0 || remaining==burst)begin state<=IDLE;done<=1;cancelled<=stop;end
                else begin
                    address<=address+({27'b0,burst}<<3);remaining<=remaining-burst;
                    burst<=burst_size(address+({27'b0,burst}<<3),remaining-burst);index<=0;state<=FILL;
                end
            end
            POISON:state<=POISON;
            default:begin state<=POISON;fault<=5;end
            endcase
        end
    end
endmodule
`default_nettype wire
