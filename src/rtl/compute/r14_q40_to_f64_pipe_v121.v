`timescale 1ns/1ps
`default_nettype none
// Actual four-stage signed64 Q40 -> binary64 RNE converter, II1.
// All stages and last/valid advance together; output never changes on a hold.
module r14_q40_to_f64_pipe_v121(
 input wire clk,rst_n,clear,input wire in_valid,output wire in_ready,
 input wire[63:0]in_data,input wire in_last,
 output reg out_valid,input wire out_ready,output reg[63:0]out_data,output reg out_last,output wire quiescent
);
 reg v0,v1,v2,last0,last1,last2,sign0,sign1,sign2,zero2;
 reg[63:0]mag0,mag1,norm2;reg[5:0]k1;reg[10:0]exp2;
 assign quiescent=!(v0 || v1 || v2 || out_valid);
 wire advance=!out_valid || out_ready;
 assign in_ready=advance && !clear && rst_n;
 function[5:0]highest_bit;
  input[63:0]mag;reg[63:0]v;reg[5:0]k;
  begin
   v=mag;k=0;
   if(v[63:32]!=0)begin v=v>>32;k[5]=1;end
   if(v[31:16]!=0)begin v=v>>16;k[4]=1;end
   if(v[15:8]!=0)begin v=v>>8;k[3]=1;end
   if(v[7:4]!=0)begin v=v>>4;k[2]=1;end
   if(v[3:2]!=0)begin v=v>>2;k[1]=1;end
   if(v[1])k[0]=1;
   highest_bit=k;
  end
 endfunction
 wire round_up=norm2[10] && (|norm2[9:0] || norm2[11]);
 wire[53:0]rounded={1'b0,norm2[63:11]}+round_up;
 wire[10:0]final_exp=exp2+rounded[53];
 wire[51:0]fraction=rounded[53]?rounded[52:1]:rounded[51:0];
 always @(posedge clk)begin
  if(!rst_n || clear)begin
   v0<=0;v1<=0;v2<=0;out_valid<=0;out_last<=0;out_data<=0;
   last0<=0;last1<=0;last2<=0;sign0<=0;sign1<=0;sign2<=0;zero2<=0;
   mag0<=0;mag1<=0;norm2<=0;k1<=0;exp2<=0;
  end else if(advance)begin
   v0<=in_valid;v1<=v0;v2<=v1;out_valid<=v2;
   if(in_valid)begin mag0<=in_data[63]?(~in_data+1'b1):in_data;sign0<=in_data[63];last0<=in_last;end
   if(v0)begin mag1<=mag0;k1<=highest_bit(mag0);sign1<=sign0;last1<=last0;end
   if(v1)begin norm2<=mag1<<(6'd63-k1);exp2<=11'd983+{5'd0,k1};zero2<=mag1==0;sign2<=sign1;last2<=last1;end
   if(v2)begin out_data<=zero2?64'd0:{sign2,final_exp,fraction};out_last<=last2;end
  end
 end
endmodule
`default_nettype wire
