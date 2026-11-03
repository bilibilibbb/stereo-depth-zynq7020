`timescale 1ns/1ps
`default_nettype none
// Recover frame blanking from an independently measured continuous PCLK/HREF.
// 4096 cycles exceeds ordinary VGA line blanking; true vertical gaps exceed it.
// Both lanes use the same rule. No reference to the faulty external VS pin.
// Frame VS is a registered single-bit source for asynchronous diagnostic monitors.
// Existing downstream framing/byte/CRC/watchdog checks remain authoritative.
module dvp_href_frame_v2 #(
    parameter integer GAP_CYCLES=4096,
    parameter integer DATA_DELAY=8
)(
    input wire arst_n,input wire pclk,input wire href,input wire [7:0] data,
    output reg frame_vs,output wire delayed_href,output wire [7:0] delayed_data
);
    localparam integer CW=$clog2(GAP_CYCLES+1);
    (* ASYNC_REG="TRUE" *) reg [1:0] local_reset;
    reg [CW-1:0] low_cycles;
    reg [DATA_DELAY-1:0] href_pipe;
    reg [7:0] data_pipe[0:DATA_DELAY-1];
    integer i;
    always @(posedge pclk or negedge arst_n)
        if(!arst_n)local_reset<=0;else local_reset<={local_reset[0],1'b1};
    always @(posedge pclk or negedge local_reset[1]) begin
        if(!local_reset[1])begin
            low_cycles<=0;href_pipe<=0;frame_vs<=0;
            for(i=0;i<DATA_DELAY;i=i+1)data_pipe[i]<=0;
        end else begin
            frame_vs<=!href&&(low_cycles>=GAP_CYCLES-1);
            if(href)low_cycles<=0;
            else if(low_cycles<GAP_CYCLES)low_cycles<=low_cycles+1'b1;
            href_pipe<={href_pipe[DATA_DELAY-2:0],href};
            data_pipe[0]<=data;
            for(i=1;i<DATA_DELAY;i=i+1)data_pipe[i]<=data_pipe[i-1];
        end
    end
    assign delayed_href=href_pipe[DATA_DELAY-1];
    assign delayed_data=data_pipe[DATA_DELAY-1];
    initial if(DATA_DELAY<2 || GAP_CYCLES<=DATA_DELAY+4)
        $error("Frame gap must exceed alignment pipeline; delay must be >=2");
endmodule
`default_nettype wire
