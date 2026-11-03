`timescale 1ns/1ps
`default_nettype none
// Explicit one synchronous read + one synchronous write template.
// Frame protocols initialize rows as consumed; no reset clears large RAMs.
module sgm_row_ram_v1 #(parameter DATA_BITS=520,DEPTH=320,ADDR_BITS=9)(
 input wire clk,read_enable,write_enable,input wire[ADDR_BITS-1:0]read_address,write_address,
 input wire[DATA_BITS-1:0]write_data,output reg[DATA_BITS-1:0]read_data
);
 (* ram_style="block" *)reg[DATA_BITS-1:0]memory[0:DEPTH-1];
 always @(posedge clk)begin
  if(write_enable)memory[write_address]<=write_data;
  if(read_enable)read_data<=memory[read_address];
 end
endmodule
`default_nettype wire
