`timescale 1ns/1ps
`default_nettype none
// Dual-clock RAM with registered read prefetch. Common asynchronous assertion;
// each domain releases reset synchronously. Never reset one side during a job.
module camera_async_fifo #(parameter DATA_BITS=64, parameter LOG2_DEPTH=9)(
    input wire arst_n,input wire wclk,input wire rclk,
    input wire [DATA_BITS-1:0] wdata,input wire wvalid,output wire wready,
    output wire [LOG2_DEPTH:0] wlevel,
    output reg [DATA_BITS-1:0] rdata,output reg rvalid,input wire rready,
    output wire rempty
);
    localparam PTR=LOG2_DEPTH+1;
    localparam [PTR-1:0] FULL_XOR=(1<<LOG2_DEPTH)|(1<<(LOG2_DEPTH-1));
    (* ram_style="block" *) reg [DATA_BITS-1:0] memory[0:(1<<LOG2_DEPTH)-1];
    (* ASYNC_REG="TRUE" *) reg [1:0] wrst,rrst;
    (* ASYNC_REG="TRUE" *) reg [PTR-1:0] rg1,rg2,wg1,wg2;
    reg [PTR-1:0] wb,rb;
    // Retain registered Gray bits so constraints bind the complete physical bus.
    (* DONT_TOUCH="TRUE" *) reg [PTR-1:0] wgray,rgray;
    reg full,empty;
    wire wp=wvalid&&wready;
    wire fetch=rrst[1]&&(!rvalid||rready)&&!empty;
    wire [PTR-1:0] wn=wb+wp,rn=rb+fetch;
    wire [PTR-1:0] wgn=(wn>>1)^wn,rgn=(rn>>1)^rn;
    function [PTR-1:0] gray_binary;
        input [PTR-1:0] g;integer i;
        begin gray_binary[PTR-1]=g[PTR-1];for(i=PTR-2;i>=0;i=i-1)gray_binary[i]=gray_binary[i+1]^g[i];end
    endfunction
    assign wready=wrst[1]&&!full;
    assign wlevel=wb-gray_binary(rg2);
    assign rempty=empty&&!rvalid;
    always @(posedge wclk or negedge arst_n)
        if(!arst_n)wrst<=0;else wrst<={wrst[0],1'b1};
    always @(posedge rclk or negedge arst_n)
        if(!arst_n)rrst<=0;else rrst<={rrst[0],1'b1};
    always @(posedge wclk or negedge wrst[1]) begin
        if(!wrst[1])begin wb<=0;wgray<=0;full<=0;rg1<=0;rg2<=0;end
        else begin
            rg1<=rgray;rg2<=rg1;wb<=wn;wgray<=wgn;full<=wgn==(rg2^FULL_XOR);
        end
    end
    always @(posedge wclk)if(wp)memory[wb[LOG2_DEPTH-1:0]]<=wdata;
    always @(posedge rclk)if(fetch)rdata<=memory[rb[LOG2_DEPTH-1:0]];
    always @(posedge rclk or negedge rrst[1]) begin
        if(!rrst[1])begin rb<=0;rgray<=0;empty<=1;wg1<=0;wg2<=0;rvalid<=0;end
        else begin
            wg1<=wgray;wg2<=wg1;rb<=rn;rgray<=rgn;empty<=rgn==wg2;
            if(fetch)rvalid<=1;
            else if(rready)rvalid<=0;
        end
    end
    initial if(LOG2_DEPTH<2 || DATA_BITS<1)$error("Invalid FIFO geometry");
endmodule
`default_nettype wire
