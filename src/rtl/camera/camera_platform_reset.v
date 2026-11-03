`timescale 1ns/1ps
// PS FCLK_RESET0_N is asserted for a coordinated platform reset. Release into
// the board-clock AXI domain is synchronized; sensor domains synchronize again.
// Software must cancel and drain before a planned reset. An unexpected PS
// system reset invalidates every descriptor and requires a fresh host session.
module camera_platform_reset #(
    parameter integer STARTUP_BITS=16
)(input wire clk,input wire ps_resetn,output wire resetn);
    reg [STARTUP_BITS-1:0] startup=0;
    always @(posedge clk)if(!(&startup))startup<=startup+1'b1;
    (* ASYNC_REG="TRUE" *) reg [2:0] release_sync=0;
    // Incoming reset asserts directly; local startup only delays clocked release.
    always @(posedge clk or negedge ps_resetn)
        if(!ps_resetn)release_sync<=0;
        else if(!(&startup))release_sync<=0;
        else release_sync<={release_sync[1:0],1'b1};
    assign resetn=release_sync[2];
endmodule
