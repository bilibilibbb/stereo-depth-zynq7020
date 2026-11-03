`timescale 1ns/1ps
`default_nettype none
// Exact top-four merge by parallel ranks. Equal costs retain earlier disparity
// order: the four retained entries precede the four new, increasing labels.
// The final packet uses registered winners, avoiding merge+gap+pack in one edge.
module retained_sgm_summary_v1 #(parameter WIDTH=320,HEIGHT=240)(
 input wire clk,rst_n,start,cancel,
 input wire in_valid,output wire in_ready,input wire[63:0]in_cost,input wire[31:0]in_raw,
 input wire[15:0]in_x,in_y,input wire[3:0]in_group,input wire[31:0]in_tag,cmd_tag,input wire in_last,
 output reg out_valid,input wire out_ready,output reg[63:0]out_data,output reg[15:0]out_x,out_y,output reg out_last,
 output reg[7:0]fault
);
 reg[7:0]best[0:3],label[0:3],raw[0:3];
 reg[15:0]x,y;reg[3:0]group;reg finish_pending;
 wire[7:0]cost[0:7],disparity[0:7],raw_cost[0:7];
 wire[3:0]bad_lane;
 genvar g,h;
 generate for(g=0;g<4;g=g+1)begin:INPUTS
  assign cost[g]=group==0?8'hff:best[g];
  assign disparity[g]=group==0?8'hff:label[g];
  assign raw_cost[g]=group==0?8'hff:raw[g];
  assign cost[g+4]=in_cost[g*16+:16]<=192?in_cost[g*16+:8]:8'hff;
  assign disparity[g+4]=in_cost[g*16+:16]<=192?{2'b0,group,2'b0}+g:8'hff;
  assign raw_cost[g+4]=in_cost[g*16+:16]<=192?in_raw[g*8+:8]:8'hff;
  assign bad_lane[g]=(in_cost[g*16+:16]>192 && in_cost[g*16+:16]!=1020) ||
    ((in_cost[g*16+:16]==1020)!=(in_raw[g*8+:8]==255)) ||
    (in_raw[g*8+:8]>24 && in_raw[g*8+:8]!=255);
 end endgenerate
 wire[7:0]less[0:7];wire[3:0]rank[0:7];
 generate for(g=0;g<8;g=g+1)begin:RANK
  for(h=0;h<8;h=h+1)begin:COMPARE
   if(h==g)assign less[g][h]=1'b0;
   else if(h<g)assign less[g][h]=cost[h]<=cost[g];
   else assign less[g][h]=cost[h]<cost[g];
  end
  wire[1:0]p0={1'b0,less[g][0]}+{1'b0,less[g][1]};
  wire[1:0]p1={1'b0,less[g][2]}+{1'b0,less[g][3]};
  wire[1:0]p2={1'b0,less[g][4]}+{1'b0,less[g][5]};
  wire[1:0]p3={1'b0,less[g][6]}+{1'b0,less[g][7]};
  wire[2:0]s0={1'b0,p0}+{1'b0,p1};
  wire[2:0]s1={1'b0,p2}+{1'b0,p3};
  assign rank[g]={1'b0,s0}+{1'b0,s1};
 end endgenerate
 wire[7:0]next_best[0:3],next_label[0:3],next_raw[0:3];
 generate for(g=0;g<4;g=g+1)begin:SELECT
  wire[7:0]c[0:7],d[0:7],r[0:7];
  for(h=0;h<8;h=h+1)begin:MASK
   assign c[h]=cost[h]&{8{rank[h]==g}};
   assign d[h]=disparity[h]&{8{rank[h]==g}};
   assign r[h]=raw_cost[h]&{8{rank[h]==g}};
  end
  assign next_best[g]=(c[0]|c[1])|(c[2]|c[3])|(c[4]|c[5])|(c[6]|c[7]);
  assign next_label[g]=(d[0]|d[1])|(d[2]|d[3])|(d[4]|d[5])|(d[6]|d[7]);
  assign next_raw[g]=(r[0]|r[1])|(r[2]|r[3])|(r[4]|r[5])|(r[6]|r[7]);
 end endgenerate
 reg[15:0]second;integer i,k;
 always @* begin
  second=16'hffff;
  for(i=3;i>=1;i=i-1)if(best[i]<=192 &&
   (({1'b0,label[i]}>{1'b0,label[0]}+9'd1)||({1'b0,label[0]}>{1'b0,label[i]}+9'd1)))second={8'd0,best[i]};
 end
 assign in_ready=rst_n && !cancel && !fault && !finish_pending && (!out_valid || out_ready);
 always @(posedge clk)begin
  if(!rst_n || start || cancel)begin
   x<=0;y<=0;group<=0;finish_pending<=0;out_valid<=0;out_data<=0;out_x<=0;out_y<=0;out_last<=0;fault<=0;
   for(k=0;k<4;k=k+1)begin best[k]<=255;label[k]<=255;raw[k]<=255;end
  end else begin
   if(out_valid && out_ready)out_valid<=0;
   if(finish_pending)begin
    finish_pending<=0;out_valid<=1;
    out_data<=best[0]<=192?{15'd0,1'b1,second,8'd0,best[0],raw[0],label[0]}:{16'd0,16'hffff,16'd1020,8'hff,8'hff};
   end
   if(in_valid && in_ready)begin
    if(in_x!=x || in_y!=y || in_group!=group || in_tag!=cmd_tag || in_last!=(x==WIDTH-1 && y==HEIGHT-1 && group==15) || |bad_lane)fault<=8'h91;
    else begin
     for(k=0;k<4;k=k+1)begin best[k]<=next_best[k];label[k]<=next_label[k];raw[k]<=next_raw[k];end
     if(group==15)begin
      finish_pending<=1;out_x<=x;out_y<=y;out_last<=x==WIDTH-1 && y==HEIGHT-1;
      group<=0;if(x==WIDTH-1)begin x<=0;y<=y+1'b1;end else x<=x+1'b1;
     end else group<=group+1'b1;
    end
   end
  end
 end
endmodule
`default_nettype wire
