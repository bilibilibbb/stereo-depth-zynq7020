`timescale 1ns/1ps
`default_nettype none
// Exact old 32-bit expressions, separated at clamp and quarter interpolation.
// Global elastic advance retains every stage when the output is backpressured.
module r14_tensor_alu_v1(
 input wire clk,rst_n,cancel,
 input wire in_valid,output wire in_ready,input wire[1:0]in_op,
 input wire[4:0]in_fraction_a,in_fraction_b,in_target_fraction,
 input wire signed[31:0]in_a,in_b,in_c,in_d,
 input wire[1:0]in_dx,in_dy,input wire in_relu_a,in_relu_b,
 output wire out_valid,input wire out_ready,output wire signed[31:0]out_numerator,
 output wire signed[15:0]out_quantized,output wire out_error
);
 function signed[31:0]six;input[4:0]fraction;reg[2:0]shift;
  begin shift=fraction-5'd9;six=32'sd3072<<<shift;end endfunction
 function signed[31:0]clamp6;
  input signed[31:0]v;input[4:0]fraction;input enabled;
  begin clamp6=!enabled?v:(v<0?32'sd0:(v>six(fraction)?six(fraction):v));end
 endfunction
 function signed[31:0]quarter_mix;
  input signed[31:0]a,b;input[1:0]q;
  reg signed[31:0]base,other,delta,weighted;
  begin
   base=q[1]?b:a;other=q[1]?a:b;delta=other-base;
   weighted=q[0]?delta:(q[1]?(delta<<<1):32'sd0);
   quarter_mix=(base<<<2)+weighted;
  end
 endfunction
 wire[4:0]maximum_fraction=in_fraction_a>=in_fraction_b?in_fraction_a:in_fraction_b;
 wire bad=(in_op==3) || in_fraction_a<9 || in_fraction_a>16 || in_target_fraction<11 || in_target_fraction>14 ||
          (in_op==1 && (in_fraction_b<9 || in_fraction_b>16)) || (in_op==2 && in_fraction_a>12);
 reg vp,vh,v0,v1,v2,ep,eh,error0,error1,error2,negative1;
 reg signed[31:0]ap,bp,cp,dp,limitp,limith,sumh,upperh,lowerh,plainh,n0,n1,n2;
 reg[2:0]align_ap,align_bp;
 reg[1:0]opp,dxp,dyp,oph,dyh;
 reg[4:0]fp,tp,fh,th,f0,target0;
 reg[31:0]quotient1;reg[5:0]remainder1,half1;reg round1;
 reg signed[15:0]q2;
 wire advance=!v2 || out_ready;
 wire signed[31:0]sum=(ap<<<align_ap)+(bp<<<align_bp);
 wire signed[31:0]resized=quarter_mix(upperh,lowerh,dyh);
 wire signed[31:0]number=oph==1?(sumh<0?32'sd0:(sumh>limith?limith:sumh)):(oph==2?resized:plainh);
 wire[31:0]magnitude=n0[31]?(~n0+32'd1):n0;
 wire right=f0>target0;wire[2:0]amount=right?f0-target0:target0-f0;
 wire left_overflow=magnitude>(32'd32767>>amount);
 wire[31:0]quotient=right?(magnitude>>amount):(left_overflow?32'd32768:(magnitude<<amount));
 wire[5:0]remainder=magnitude[5:0]&((6'd1<<amount)-6'd1);
 wire[5:0]half=6'd1<<(amount-3'd1);
 wire increment=round1 && (remainder1>half1 || (remainder1==half1 && quotient1[0]));
 wire[31:0]rounded=quotient1+{31'd0,increment};
 wire[15:0]clipped=rounded>32767?16'd32767:rounded[15:0];
 assign in_ready=rst_n && !cancel && advance;
 assign out_valid=v2 && rst_n && !cancel;
 assign out_numerator=n2;assign out_quantized=q2;assign out_error=error2;
 always @(posedge clk)begin
  if(!rst_n || cancel)begin
   vp<=0;vh<=0;v0<=0;v1<=0;v2<=0;ep<=0;eh<=0;error0<=0;error1<=0;error2<=0;
   ap<=0;bp<=0;cp<=0;dp<=0;limitp<=0;limith<=0;sumh<=0;upperh<=0;lowerh<=0;plainh<=0;
   align_ap<=0;align_bp<=0;opp<=0;dxp<=0;dyp<=0;oph<=0;dyh<=0;fp<=0;tp<=0;fh<=0;th<=0;
   n0<=0;n1<=0;n2<=0;f0<=0;target0<=0;negative1<=0;quotient1<=0;remainder1<=0;half1<=0;round1<=0;q2<=0;
  end else if(advance)begin
   v2<=v1;v1<=v0;v0<=vh;vh<=vp;vp<=in_valid;
   if(v1)begin n2<=n1;q2<=negative1?(~clipped+16'd1):clipped;error2<=error1;end
   if(v0)begin
    n1<=n0;negative1<=n0[31];quotient1<=quotient;remainder1<=remainder;half1<=half;
    round1<=right && amount!=0;error1<=error0;
   end
   if(vh)begin n0<=number;f0<=fh;target0<=th;error0<=eh;end
   if(vp)begin
    sumh<=sum;upperh<=quarter_mix(ap,bp,dxp);lowerh<=quarter_mix(cp,dp,dxp);plainh<=ap;
    oph<=opp;dyh<=dyp;limith<=limitp;fh<=fp;th<=tp;eh<=ep;
   end
   if(in_valid)begin
    ap<=clamp6(in_a,in_fraction_a,in_relu_a);
    bp<=clamp6(in_b,in_fraction_b,in_op==1?in_relu_b:in_relu_a);
    cp<=clamp6(in_c,in_fraction_a,in_relu_a);dp<=clamp6(in_d,in_fraction_a,in_relu_a);
    align_ap<=maximum_fraction-in_fraction_a;align_bp<=maximum_fraction-in_fraction_b;
    limitp<=six(maximum_fraction);opp<=in_op;dxp<=in_dx;dyp<=in_dy;
    fp<=in_op==1?maximum_fraction:(in_op==2?in_fraction_a+5'd4:in_fraction_a);
    tp<=in_target_fraction;ep<=bad;
   end
  end
 end
endmodule
`default_nettype wire
