`timescale 1ns/1ps
`default_nettype none
// Four elastic stages: signed bias add, absolute value, shift-with-sticky,
// nearest-even/saturation/sign. Same arithmetic and interface as v1.
// The sticky barrel avoids a 64-bit remainder comparison and increment chain.
module requant_row4_dsp_v1(
 input wire clk,rst_n,cancel,
 input wire in_valid,output wire in_ready,
 input wire [191:0] in_sum,in_bias,
 input wire [23:0] in_shift,input wire [3:0] in_mask,
 input wire in_int8,in_last,input wire [31:0] in_tag,
 input wire [6:0] in_row,input wire[2:0] in_col_block,
 output wire out_valid,input wire out_ready,
 output wire [63:0] out_data,output wire [3:0] out_saturated,
 output wire out_last,output wire [31:0] out_tag,
 output wire [6:0] out_row,output wire[2:0] out_col_block,
 output reg [31:0] accepted_rows,emitted_rows,saturated_lanes
);
 reg [3:0] valid;wire advance=!valid[3]||out_ready;
 reg [23:0] shift0,shift1;
 reg [3:0] mask[0:3];reg mode8[0:3],last[0:3];reg[2:0]col[0:3];
 reg [31:0] tag[0:3];reg [6:0] row[0:3];
 wire [63:0] packed_result;wire [3:0] packed_sat;
 assign in_ready=rst_n&&!cancel&&advance;
 assign out_valid=rst_n&&!cancel&&valid[3];
 assign out_data=packed_result;assign out_saturated=packed_sat;
 assign out_last=last[3];assign out_tag=tag[3];assign out_row=row[3];assign out_col_block=col[3];
 genvar lane,j;
 generate for(lane=0;lane<4;lane=lane+1)begin:L
  (* use_dsp="yes" *) reg signed [47:0] low_sum0;
  reg sign_a0,sign_b0;
  // Equal operand signs determine the49bit sign. With different signs,
  // their sum fits signed48bit and its bit47 is the true result sign.
  wire sum_negative=(sign_a0&&sign_b0)||((sign_a0^sign_b0)&&low_sum0[47]);
  wire signed [48:0] sum0={sum_negative,low_sum0};
  reg [48:0] magnitude1;reg negative1,negative2;
  reg [15:0] low2,data3;reg high2,increment2,sat3;
  wire signed [47:0] a=in_sum[lane*48+:48],b=in_bias[lane*48+:48];
  wire [6*51+50:0] shifted;
  assign shifted[0+:51]={magnitude1,2'b00};
  for(j=0;j<6;j=j+1)begin:SHIFT
   localparam DIST=1<<j;
   wire [50:0] previous=shifted[j*51+:51];
   assign shifted[(j+1)*51+:51]=shift1[lane*6+j]?
       {{DIST{1'b0}},previous[50:DIST+1],(|previous[DIST:0])}:previous;
  end
  wire [50:0] result=shifted[6*51+:51];
  wire [16:0] rounded={1'b0,low2}+increment2;
  wire [15:0] limit=mode8[2]?16'd127:16'd32767;
  wire overflow=high2 || rounded>{1'b0,limit};
  wire [15:0] absolute=overflow?limit:rounded[15:0];
  assign packed_result[lane*16+:16]=data3;assign packed_sat[lane]=sat3;
  always @(posedge clk)begin
   if(!rst_n||cancel)begin data3<=0;sat3<=0;end
   else if(advance)begin
    if(in_valid)begin low_sum0<=a+b;sign_a0<=a[47];sign_b0<=b[47];end
    if(valid[0])begin magnitude1<=sum0[48]?-sum0:sum0;negative1<=sum0[48];end
    if(valid[1])begin
     low2<=result[17:2];high2<=|result[50:18];
     increment2<=result[1]&&(result[0]||result[2]);negative2<=negative1;
    end
    if(valid[2])begin
     data3<=!mask[2][lane]?16'b0:negative2?(~absolute+16'd1):absolute;
     sat3<=mask[2][lane]&&overflow;
    end
   end
  end
 end endgenerate
 integer k;reg [4:0] saturation_count;
 always @* begin saturation_count=0;for(k=0;k<4;k=k+1)saturation_count=saturation_count+packed_sat[k];end
 integer i;
 always @(posedge clk)begin
  if(!rst_n||cancel)begin
   valid<=0;shift0<=0;shift1<=0;accepted_rows<=0;emitted_rows<=0;saturated_lanes<=0;
   for(i=0;i<4;i=i+1)begin mask[i]<=0;mode8[i]<=0;last[i]<=0;col[i]<=0;tag[i]<=0;row[i]<=0;end
  end else begin
   if(in_valid&&in_ready)accepted_rows<=accepted_rows+1'b1;
   if(out_valid&&out_ready)begin emitted_rows<=emitted_rows+1'b1;saturated_lanes<=saturated_lanes+saturation_count;end
   if(advance)begin
    valid<={valid[2:0],in_valid};
    if(in_valid)begin shift0<=in_shift;mask[0]<=in_mask;mode8[0]<=in_int8;last[0]<=in_last;col[0]<=in_col_block;tag[0]<=in_tag;row[0]<=in_row;end
    if(valid[0])shift1<=shift0;
    for(i=1;i<4;i=i+1)if(valid[i-1])begin mask[i]<=mask[i-1];mode8[i]<=mode8[i-1];last[i]<=last[i-1];col[i]<=col[i-1];tag[i]<=tag[i-1];row[i]<=row[i-1];end
   end
  end
 end
endmodule
`default_nettype wire
