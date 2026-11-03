`timescale 1ns/1ps
`default_nettype none
// Three real register stages: raw format, unsigned floor/RNE metadata,
// signed rounded value. Bounds and last travel with the same held token.
module r14_f64_to_q40_pipe_v121(
 input wire clk,rst_n,clear,in_valid,input wire[63:0]in_data,
 input wire in_last,prefix_bound,output wire in_ready,
 output reg out_valid,input wire out_ready,output reg[63:0]out_data,
 output reg out_last,out_error,output wire quiescent
);
 reg v0,v1,last0,last1,bound0,sign1,round1,error1;
 reg[63:0]raw0,floor1;
 wire advance=!out_valid || out_ready;
 // Clear is synchronous and discards all tokens; callers gate admission.
 // Ready has no clear feedback from an invalid accepted last marker.
 assign in_ready=advance && rst_n;
 assign quiescent=!(v0 || v1 || out_valid);
 function[66:0]parts;
  input[63:0]b;input prefix;
  reg[63:0]whole,mag,significand,remnant,half;
  reg roundup,error;integer shift;
  begin
   mag={1'b0,b[62:0]};whole=0;remnant=0;half=0;roundup=0;shift=0;
   significand={11'd0,1'b1,b[51:0]};
   error=b[62:52]==11'h7ff || mag>(prefix?64'h40d0000000000000:64'h40f0000000000000);
   if(!error && b[62:52]!=0)begin
    if(b[62:52]>=1035)whole=significand<<(b[62:52]-1035);
    else begin
     shift=1035-b[62:52];
     if(shift<54)begin
      whole=significand>>shift;
      remnant=significand&((64'd1<<shift)-1'b1);half=64'd1<<(shift-1);
      roundup=remnant>half || (remnant==half && whole[0]);
     end
    end
   end
   parts={error,b[63],roundup,whole};
  end
 endfunction
 wire[66:0]prepared=parts(raw0,bound0);
 wire[63:0]rounded=(sign1?~floor1:floor1)+(sign1?{63'd0,!round1}:{63'd0,round1});
 always @(posedge clk)begin
  if(!rst_n || clear)begin
   v0<=0;v1<=0;out_valid<=0;raw0<=0;floor1<=0;
   last0<=0;last1<=0;bound0<=0;sign1<=0;round1<=0;error1<=0;
   out_data<=0;out_last<=0;out_error<=0;
  end else if(advance)begin
   v0<=in_valid;v1<=v0;out_valid<=v1;
   if(in_valid)begin raw0<=in_data;last0<=in_last;bound0<=prefix_bound;end
   if(v0)begin {error1,sign1,round1,floor1}<=prepared;last1<=last0;end
   if(v1)begin out_data<=error1?64'd0:rounded;out_error<=error1;out_last<=last1;end
  end
 end
endmodule
`default_nettype wire
