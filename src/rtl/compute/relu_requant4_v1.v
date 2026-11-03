`timescale 1ns/1ps
`default_nettype none
// Post-quantization fusion: retain the original convolution INT16 rounding,
// then apply the frozen second nearest-even conversion and ReLU/ReLU6.
// The83 single-consumer frozen fusions use only these five formats:
// ReLU6 Q7/Q8/Q9/Q10 -> Q11, ReLU Q9 -> Q10. A left conversion
// is exact; unsupported configurations are rejected instead of approximated.
(* use_dsp="no" *) module relu_requant4_v1(
 input wire clk,rst_n,cancel,
 input wire in_valid,output wire in_ready,input wire[63:0]in_data,
 input wire[1:0]in_mode,input wire[5:0]in_fraction,out_fraction,
 input wire[31:0]in_tag,input wire[6:0]in_row,input wire[4:0]in_column,input wire in_last,
 output wire out_valid,input wire out_ready,output reg[63:0]out_data,
 output reg[31:0]out_tag,output reg[6:0]out_row,output reg[4:0]out_column,output reg out_last,
 output reg[3:0]out_saturated,output reg out_error,output wire busy
);
 reg valid;wire advance=!valid || out_ready;
 assign in_ready=rst_n && !cancel && advance;assign out_valid=rst_n && !cancel && valid;assign busy=valid;
 function[16:0]activation;
  input[15:0]code;input[1:0]mode;input[5:0]fin,fout;
  reg[15:0]limit,value,threshold;reg[2:0]distance;reg sat;
  begin
   limit=mode==2?16'd12288:16'd32767;value=0;sat=0;
   distance=mode==1?3'd1:(fin==7?3'd4:(fin==8?3'd3:(fin==9?3'd2:3'd1)));
   threshold=limit>>distance;
   if(mode==0)value=code;
   else if(!code[15])begin
    sat=mode==1 && code>threshold;value=code>threshold?limit:(code<<distance);
   end
   activation={sat,value};
  end
 endfunction
 integer lane;reg[16:0]a;
 always @(posedge clk)begin
  if(!rst_n || cancel)begin valid<=0;out_data<=0;out_tag<=0;out_row<=0;out_column<=0;out_last<=0;out_saturated<=0;out_error<=0;end
  else if(advance)begin
   valid<=in_valid;
   if(in_valid)begin
    out_error<=!(in_mode==0 || (in_mode==1 && in_fraction==9 && out_fraction==10) ||
      (in_mode==2 && in_fraction>=7 && in_fraction<=10 && out_fraction==11));
    for(lane=0;lane<4;lane=lane+1)begin
     a=activation(in_data[lane*16+:16],in_mode,in_fraction,out_fraction);out_data[lane*16+:16]<=a[15:0];out_saturated[lane]<=a[16];
    end
    out_tag<=in_tag;out_row<=in_row;out_column<=in_column;out_last<=in_last;
   end
  end
 end
endmodule
`default_nettype wire
