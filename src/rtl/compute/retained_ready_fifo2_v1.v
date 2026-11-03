`timescale 1ns/1ps
`default_nettype none
// Two registered slots cut the backward ready path. Full-pop deliberately
// does not bypass admission; at most one recovery bubble after a full stall.
module retained_ready_fifo2_v1 #(parameter WIDTH=64)(
 input wire clk,rst_n,cancel,in_valid,output wire in_ready,input wire[WIDTH-1:0]in_data,
 output wire out_valid,input wire out_ready,output wire[WIDTH-1:0]out_data,output wire busy
);
 (* ram_style="registers" *)reg[WIDTH-1:0]storage[0:1];reg head,tail;reg[1:0]fill;
 assign in_ready=rst_n && !cancel && fill<2;
 assign out_valid=rst_n && !cancel && fill!=0;
 assign out_data=out_valid?storage[head]:{WIDTH{1'b0}};assign busy=fill!=0;
 wire push=in_valid && in_ready,pop=out_valid && out_ready;
 always @(posedge clk)begin
  if(!rst_n || cancel)begin head<=0;tail<=0;fill<=0;end
  else begin
   if(push)begin storage[tail]<=in_data;tail<=!tail;end
   if(pop)head<=!head;
   case({push,pop})2'b10:fill<=fill+1'b1;2'b01:fill<=fill-1'b1;default:begin end endcase
  end
 end
endmodule
`default_nettype wire
