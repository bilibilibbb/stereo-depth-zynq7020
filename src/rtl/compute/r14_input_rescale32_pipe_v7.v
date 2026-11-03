`timescale 1ns/1ps
`default_nettype none
// Exact signed INT32 conversion, symmetric32767 clipping and ties to even.
// Three elastic math stages keep data, masks and saturation under one owner.
module r14_input_rescale32_pipe_v7 #(parameter LANES=4)(
 input wire clk,rst_n,cancel,configure,input wire signed[7:0]scale,
 input wire relu,input wire[5:0]source_fraction,output reg configured,
 input wire in_valid,output wire in_ready,output wire out_valid,
 input wire out_ready,output wire busy,
 input wire[LANES*32-1:0]in_data,input wire[LANES-1:0]in_mask,
 output wire[LANES*16-1:0]out_data,output wire[LANES-1:0]out_mask,saturated
);
 reg pending,left,big_left,big_right,activation;reg[4:0]amount;
 reg[31:0]left_limit,round_mask,round_half,ceiling,right_threshold;
 wire[7:0]absolute_scale=scale[7]?-scale:scale;
 reg v0,v1,v2;reg[LANES-1:0]mask0,mask1,mask2,sign0,sign1;
 reg[LANES-1:0]sat1,sat2;reg[LANES*16-1:0]data2;
 reg[31:0]magnitude0[0:LANES-1];reg[15:0]magnitude1[0:LANES-1];
 wire r2=!v2 || out_ready,r1=!v1 || r2,r0=!v0 || r1;
 assign in_ready=rst_n && configured && !configure && !cancel && r0;
 assign out_valid=rst_n && configured && !configure && !cancel && v2;
 assign busy=v0 || v1 || v2;
 assign out_data=data2;assign out_mask=mask2;assign saturated=sat2;
 always @(posedge clk)begin
  if(!rst_n || cancel)begin
   configured<=0;pending<=0;left<=0;big_left<=0;big_right<=0;amount<=0;
   left_limit<=0;round_mask<=0;round_half<=0;activation<=0;ceiling<=0;right_threshold<=0;
  end else begin
   if(pending)begin
    left_limit<=32'h7fff>>amount;round_mask<=(32'd1<<amount)-1'b1;
    round_half<=32'd1<<(amount-5'd1);
    right_threshold<=amount==0?32'h00008000:(32'h0000ffff<<(amount-5'd1));
    configured<=1;pending<=0;
   end
   if(configure)begin
    left<=scale>=0;big_left<=scale>=8'sd15;big_right<=scale<=-8'sd32;
    amount<=absolute_scale[4:0];pending<=1;configured<=0;
    activation<=relu;ceiling<=32'd6<<source_fraction;
   end
  end
 end
 always @(posedge clk)begin
  if(!rst_n || cancel || configure)begin
   v0<=0;v1<=0;v2<=0;mask0<=0;mask1<=0;mask2<=0;sat2<=0;
  end else if(configured)begin
   if(r0)begin v0<=in_valid && in_ready;if(in_valid && in_ready)mask0<=in_mask;end
   if(r1)begin v1<=v0;if(v0)mask1<=mask0;end
   if(r2)begin v2<=v1;if(v1)begin mask2<=mask1;sat2<=sat1;end end
  end
 end
 genvar lane;
 generate for(lane=0;lane<LANES;lane=lane+1)begin:CONVERT
  wire signed[31:0]source=in_data[lane*32+:32];
  wire signed[31:0]value=activation?(source<0?32'sd0:
    (source>$signed(ceiling)?$signed(ceiling):source)):source;
  wire[31:0]magnitude=value[31]?(~value+32'd1):value;
  wire[31:0]quotient=magnitude0[lane]>>amount;
  wire[31:0]remainder=magnitude0[lane]&round_mask;
  wire round_guard=|(magnitude0[lane]&round_half);
  wire round_sticky=|(magnitude0[lane]&{1'b0,round_mask[31:1]});
  wire increment=amount!=0 && round_guard && (round_sticky || quotient[0]);
  // Clipping is decided from high quotient bits and the32767 carry before
  // the bounded16-bit rounding adder. No32-bit rounded magnitude is needed.
  wire overflow=big_left?(magnitude0[lane]!=0):(magnitude0[lane]>left_limit);
  wire right_overflow=!big_right && amount<5'd17 && magnitude0[lane]>=right_threshold;
  wire[15:0]rounded={1'b0,quotient[14:0]}+{15'd0,increment};
  wire[15:0]left_data=magnitude0[lane][15:0]<<amount;
  wire[15:0]clipped=left?(overflow?16'd32767:left_data):
    (big_right?16'd0:(right_overflow?16'd32767:rounded));
  always @(posedge clk)begin
   if(!rst_n || cancel || configure)begin
    magnitude0[lane]<=0;magnitude1[lane]<=0;sign0[lane]<=0;sign1[lane]<=0;sat1[lane]<=0;data2[lane*16+:16]<=0;
   end else if(configured)begin
    if(r0 && in_valid && in_ready)begin magnitude0[lane]<=magnitude;sign0[lane]<=value[31];end
    if(r1 && v0)begin
     magnitude1[lane]<=clipped;sign1[lane]<=sign0[lane];
     sat1[lane]<=(left?overflow:right_overflow) && mask0[lane];
    end
    if(r2 && v1)data2[lane*16+:16]<=sign1[lane]?(~magnitude1[lane]+16'd1):magnitude1[lane];
   end
  end
 end endgenerate
endmodule
`default_nettype wire
