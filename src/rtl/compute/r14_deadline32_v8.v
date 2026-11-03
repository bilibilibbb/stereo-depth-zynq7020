`timescale 1ns/1ps
`default_nettype none
module r14_deadline32_v8(input wire clk,rst_n,busy,input wire[31:0]elapsed,limit,output reg expired);
 wire[31:0]next_elapsed=elapsed+32'd1;
 always @(posedge clk)begin
  if(!rst_n || !busy)expired<=0;
  else expired<=next_elapsed>=limit;
 end
endmodule
`default_nettype wire
