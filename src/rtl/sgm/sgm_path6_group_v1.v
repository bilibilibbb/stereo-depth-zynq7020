`timescale 1ns/1ps
`default_nettype none
// Frozen Census24/P1=4/P2=24 recurrence. Valid normalized path states are
// 0..48: C + min(previous, neighbors+4, global_min+24) - global_min.
// Sentinel63 exclusively represents the canonical invalid255. Restart uses C.
// Keep the same two elastic stages as sgm_path_group.
module sgm_path6_group_v1 #(parameter LANES=16,TAG_WIDTH=4)(
 input wire clk,resetn,in_valid,output wire in_ready,
 input wire[LANES*6-1:0]in_cost,in_previous,
 input wire[5:0]in_halo_low,in_halo_high,in_previous_min,input wire in_restart,
 input wire[TAG_WIDTH-1:0]in_tag,
 output reg out_valid,input wire out_ready,output reg[LANES*6-1:0]out_state,
 output reg[5:0]out_group_min,output reg out_error,output reg[TAG_WIDTH-1:0]out_tag
);
 reg stage_valid;reg[LANES*6-1:0]stage_state;reg stage_error;reg[TAG_WIDTH-1:0]stage_tag;
 wire advance_output=!out_valid || out_ready;
 assign in_ready=!stage_valid || advance_output;
 wire[LANES*6-1:0]next_state;wire[LANES-1:0]lane_error;
 wire[5:0]minimum[1:2*LANES-1];genvar d,n;
 generate for(d=0;d<LANES;d=d+1)begin:lane
  wire[5:0]c=in_cost[d*6+:6],same=in_previous[d*6+:6];wire[5:0]lo,hi;
  if(d==0)assign lo=in_halo_low;else assign lo=in_previous[(d-1)*6+:6];
  if(d==LANES-1)assign hi=in_halo_high;else assign hi=in_previous[(d+1)*6+:6];
  wire[6:0]se=same==63?7'h7f:{1'b0,same};
  wire[6:0]le=lo==63?7'h7f:{1'b0,lo}+7'd4;
  wire[6:0]he=hi==63?7'h7f:{1'b0,hi}+7'd4;
  wire[6:0]jump={1'b0,in_previous_min}+7'd24;
  wire[6:0]a=se<le?se:le,b=he<jump?he:jump,best=a<b?a:b;
  wire[6:0]normalized={1'b0,c}+best-{1'b0,in_previous_min};
  assign lane_error[d]=c>24 && c!=63;
  assign next_state[d*6+:6]=c>24?6'd63:((in_restart || in_previous_min==63)?c:normalized[5:0]);
  assign minimum[LANES+d]=stage_state[d*6+:6];
 end
 for(n=1;n<LANES;n=n+1)begin:reduce_min
  assign minimum[n]=minimum[2*n]<minimum[2*n+1]?minimum[2*n]:minimum[2*n+1];
 end endgenerate
 always @(posedge clk)begin
  if(!resetn)begin
   stage_valid<=0;stage_state<={LANES{6'd63}};stage_error<=0;stage_tag<=0;
   out_valid<=0;out_state<={LANES{6'd63}};out_group_min<=63;out_error<=0;out_tag<=0;
  end else begin
   if(advance_output)begin out_valid<=stage_valid;if(stage_valid)begin out_state<=stage_state;out_group_min<=minimum[1];out_error<=stage_error;out_tag<=stage_tag;end end
   if(in_ready)begin stage_valid<=in_valid;if(in_valid)begin stage_state<=next_state;stage_error<=|lane_error;stage_tag<=in_tag;end end
  end
 end
endmodule
`default_nettype wire
