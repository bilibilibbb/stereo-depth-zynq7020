// Derived from frozen retained_sgm_volume_v1.v SHA256 16522e5fb1085f32b36e41e01d090a81737ee420b21053833eaa6d50fb975b42.
// v226 pixel-result overlap; v227 streams ordered paths with separate reply identity.
`timescale 1ns/1ps
`default_nettype none
// Same D64/P1=4/P2=24 causal paths as the frozen model: left, up-right,
// up, up-left. One existing16-lane path unit is reused across all four.
// Output64-bit words contain four uint16 sums; invalid hypotheses retain
// the canonical4*255=1020. No whole cost volume is needed inside this core.
module fw_sgm_volume_rtl #(parameter WIDTH=320,HEIGHT=240)(
 input wire clk,rst_n,cancel,clear,cmd_valid,output wire cmd_ready,
 input wire[31:0]cmd_tag,timeout_cycles,
 input wire in_valid,output wire in_ready,input wire[23:0]in_left,in_right,
 input wire in_left_support,in_right_support,in_last,
 output wire out_valid,input wire out_ready,output wire[63:0]out_cost,output wire[31:0]out_raw_cost,
 output wire[31:0]out_tag,output wire[15:0]out_x,out_y,output wire[3:0]out_group,output wire out_last,
 output reg pixel_done,output reg[7:0]disparity,output reg[9:0]score,
 output reg done,cancelled,output reg[7:0]fault,output wire busy,output wire[3:0]debug_state,
 output reg[31:0]accepted_pixels,emitted_words,elapsed_cycles
);
 localparam IDLE=0,WAIT_PIXEL=1,COST0=2,COST1=3,COST_WAIT=4,PATH0=5,PATH1=6,PATH_WAIT=7,PUBLISH=8,PATH_LOAD=9,WAIT_OUTPUT=10,FAILED=15;
 reg[3:0]state;reg[15:0]x,y;reg[31:0]tag,watchdog;
 reg[23:0]left;reg left_support;
 reg[23:0]right_history[0:63];reg[63:0]support_history;
 wire[389:0]old_ur,old_up,old_ul_at_x;reg[389:0]old_ul_left,horizontal;
 reg[383:0]costs;reg[1:0]cost_issue_group,cost_reply_group;reg[1:0]path_index,path_reply_index;
 reg[383:0]previous_shift;reg[5:0]previous_min,previous_low;
 reg[287:0]first_path;reg[5:0]first_min;reg[1:0]path_issue_group,path_reply_group;
 reg[639:0]aggregate,emit_aggregate;reg[383:0]emit_costs;reg[15:0]emit_x,emit_y;reg emit_full;
 reg[3:0]word_index;reg[9:0]best;reg[5:0]best_d;
 wire stop=cancel || fault!=0 || state==FAILED;
 wire core_resetn=rst_n && !stop;
 wire take=in_valid && in_ready;
 wire cost_ready,cost_valid;wire[1:0]cost_result_group;wire[127:0]cost_result;
 wire cost_issue=(state==COST0 || state==COST1) && !stop;
 wire[1:0]cost_input_group=cost_issue_group;
 wire[383:0]rights;wire[15:0]supports;
 wire[95:0]cost_compact;genvar pc;generate for(pc=0;pc<16;pc=pc+1)begin:COMPACT
  assign cost_compact[pc*6+:6]=cost_result[pc*8+:8]==255?6'd63:cost_result[pc*8+:6];
 end endgenerate
 genvar g;generate for(g=0;g<16;g=g+1)begin:HISTORY
  assign rights[g*24+:24]=right_history[g+{cost_input_group,4'd0}];
  assign supports[g]=(x>=g+{cost_input_group,4'd0}) && support_history[g+{cost_input_group,4'd0}];
 end endgenerate
 census_cost_group #(.LANES(16),.TAG_WIDTH(2)) cost_engine(.clk(clk),.resetn(core_resetn),.in_valid(cost_issue),.in_ready(cost_ready),
  .in_left(left),.in_left_support(left_support),.in_right(rights),.in_right_support(supports),.in_tag(cost_input_group),
  .out_valid(cost_valid),.out_ready(!stop),.out_cost(cost_result),.out_tag(cost_result_group));
 // Reuse the one predecessor mux for initial path0 or the next issued path.
 // All row snapshots are already registered; no extra RAM read is needed.
 wire[1:0]preload_index=state==PATH_LOAD?2'd0:path_index+2'd1;
 wire[389:0]previous=preload_index==0?horizontal:(preload_index==1?old_ur:(preload_index==2?old_up:old_ul_left));
 wire restart=path_index==0?x==0:(path_index==1?(y==0 || x==WIDTH-1):(path_index==2?y==0:(y==0 || x==0)));
 wire path_ready,path_valid,path_error;wire[95:0]path_result;wire[5:0]path_min;wire[3:0]path_tag;
 wire path_issue=(state==PATH0 || state==PATH1) && !stop;wire[1:0]path_group=path_issue_group;
 wire path_take=path_valid && !stop;
 sgm_path6_group_v1 #(.LANES(16),.TAG_WIDTH(4)) path_engine(.clk(clk),.resetn(core_resetn),.in_valid(path_issue),.in_ready(path_ready),
  .in_cost(costs[95:0]),.in_previous(previous_shift[95:0]),
  .in_halo_low(previous_low),.in_halo_high(path_group==3?6'd63:previous_shift[101:96]),
  .in_previous_min(previous_min),.in_restart(restart),.in_tag({path_index,path_group}),
  .out_valid(path_valid),.out_ready(!stop),.out_state(path_result),.out_group_min(path_min),.out_error(path_error),.out_tag(path_tag));
 wire[389:0]complete_path={(first_min<path_min?first_min:path_min),path_result,first_path};
 wire[9:0]chunk[0:3];generate for(g=0;g<4;g=g+1)begin:PACK
  assign out_raw_cost[g*8+:8]=emit_costs[g*6+:6]==63?8'd255:{2'd0,emit_costs[g*6+:6]};
  assign chunk[g]=emit_aggregate[g*10+:10];assign out_cost[g*16+:16]={6'd0,chunk[g]};
 end endgenerate
 reg[9:0]next_best;reg[5:0]next_best_d;integer j;
 always @* begin
  next_best=best;next_best_d=best_d;
  for(j=0;j<4;j=j+1)if(chunk[j]<next_best)begin next_best=chunk[j];next_best_d=word_index*4+j;end
 end
 assign cmd_ready=rst_n && state==IDLE && !emit_full && !stop;
 assign in_ready=rst_n && state==WAIT_PIXEL && !stop;
 assign out_valid=rst_n && emit_full && !stop;
 wire output_take=out_valid && out_ready;
 wire slot_free=!emit_full || (output_take && word_index==15);
 assign out_tag=tag;assign out_x=emit_x;assign out_y=emit_y;assign out_group=word_index;
 assign out_last=emit_x==WIDTH-1 && emit_y==HEIGHT-1 && word_index==15;
 assign busy=state!=IDLE && state!=FAILED;assign debug_state=state;
 // Read all preceding-row states before writing the updated paths.
 wire row_write=path_take && path_tag[1:0]==3 && path_reply_group==3 && !path_error && path_tag[3:2]==path_reply_index;
 wire[8:0]ur_address=x<WIDTH-1?x[8:0]+9'd1:9'd0;
 sgm_row_ram_v1 #(.DATA_BITS(390),.DEPTH(WIDTH),.ADDR_BITS(9)) row_ur(.clk(clk),.read_enable(take),.read_address(ur_address),.read_data(old_ur),.write_enable(row_write && path_tag[3:2]==1),.write_address(x[8:0]),.write_data(complete_path));
 sgm_row_ram_v1 #(.DATA_BITS(390),.DEPTH(WIDTH),.ADDR_BITS(9)) row_up(.clk(clk),.read_enable(take),.read_address(x[8:0]),.read_data(old_up),.write_enable(row_write && path_tag[3:2]==2),.write_address(x[8:0]),.write_data(complete_path));
 sgm_row_ram_v1 #(.DATA_BITS(390),.DEPTH(WIDTH),.ADDR_BITS(9)) row_ul(.clk(clk),.read_enable(take),.read_address(x[8:0]),.read_data(old_ul_at_x),.write_enable(row_write && path_tag[3:2]==3),.write_address(x[8:0]),.write_data(complete_path));
 // synthesis translate_off
 integer audit_lane;
 always @(posedge clk)if(row_write)for(audit_lane=0;audit_lane<65;audit_lane=audit_lane+1)
  if(complete_path[audit_lane*6+:6]>48 && complete_path[audit_lane*6+:6]!=63)$fatal(1,"SGM six-bit state outside proven domain");
 // synthesis translate_on
 integer k;
 always @(posedge clk)begin
  if(!rst_n || cancel)begin
   state<=IDLE;x<=0;y<=0;tag<=0;watchdog<=0;left<=0;left_support<=0;support_history<=0;
   old_ul_left<={390{1'b1}};horizontal<={390{1'b1}};costs<=0;cost_issue_group<=0;cost_reply_group<=0;path_index<=0;path_reply_index<=0;
   previous_shift<=0;previous_min<=63;previous_low<=63;first_path<=0;first_min<=63;path_issue_group<=0;path_reply_group<=0;aggregate<=0;word_index<=0;best<=1023;best_d<=0;
   emit_aggregate<=0;emit_costs<=0;emit_x<=0;emit_y<=0;emit_full<=0;
   pixel_done<=0;disparity<=255;score<=1023;done<=0;cancelled<=cancel;fault<=0;accepted_pixels<=0;emitted_words<=0;elapsed_cycles<=0;
   for(k=0;k<64;k=k+1)right_history[k]<=0;
  end else begin
   done<=0;cancelled<=0;pixel_done<=0;if(busy)elapsed_cycles<=elapsed_cycles+1'b1;
   if(busy && elapsed_cycles>=watchdog)begin fault<=8'hc0;state<=FAILED;end
   else if(cost_valid && state!=COST1 && state!=COST_WAIT)begin fault<=8'hb0;state<=FAILED;end
   else if(path_valid && (state!=PATH1 && state!=PATH_WAIT || path_error || path_tag[3:2]!=path_reply_index))begin fault<=8'hb1;state<=FAILED;end
   else begin
    if(cost_valid)begin
     if(cost_result_group!=cost_reply_group)begin fault<=8'hb2;state<=FAILED;end
     else begin
      costs<={cost_compact,costs[383:96]};
      if(cost_result_group==3)begin path_index<=0;path_reply_index<=0;path_issue_group<=0;path_reply_group<=0;state<=PATH_LOAD;end
      else cost_reply_group<=cost_reply_group+1'b1;
     end
    end
    if(path_take)begin
     if(path_tag[1:0]!=path_reply_group)begin fault<=8'hb3;state<=FAILED;end
     else begin
      if(path_tag[1:0]==0)first_min<=path_min;else if(path_min<first_min)first_min<=path_min;
      if(path_tag[1:0]!=3)begin first_path<={path_result,first_path[287:96]};path_reply_group<=path_reply_group+1'b1;end
      else begin
       if(path_tag[3:2]==0)horizontal<=complete_path;
       if(path_tag[3:2]==3)begin old_ul_left<=old_ul_at_x;state<=PUBLISH;end
       else begin path_reply_index<=path_reply_index+1'b1;path_reply_group<=0;end
      end
      // Rotate one16-disparity group through a fixed add bank. Four
      // results restore canonical lane order, without variable-wide writes.
      aggregate[479:0]<=aggregate[639:160];
      for(k=0;k<16;k=k+1)begin
       if(path_tag[3:2]==0)aggregate[480+k*10+:10]<=(path_result[k*6+:6]==63?10'd255:{4'd0,path_result[k*6+:6]});
       else aggregate[480+k*10+:10]<=aggregate[k*10+:10]+(path_result[k*6+:6]==63?10'd255:{4'd0,path_result[k*6+:6]});
      end
     end
    end
    if(path_issue && path_ready)begin
     // The current request captures its final halo before the edge. Next
     // cycle can issue the next path while tagged old replies still drain.
     if(path_issue_group==3 && path_index!=3)begin previous_shift<=previous[383:0];previous_min<=previous[389:384];previous_low<=63;end
     else begin previous_shift<={96'd0,previous_shift[383:96]};previous_low<=previous_shift[95:90];end
     costs<={costs[95:0],costs[383:96]};
    end
    // Drain the previous pixel independently of the current compute state.
    // PUBLISH below has priority when replacing a just-consumed final word.
    if(output_take)begin
     emit_aggregate<={emit_aggregate[39:0],emit_aggregate[639:40]};
     emit_costs<={emit_costs[23:0],emit_costs[383:24]};
     emitted_words<=emitted_words+1'b1;best<=next_best;best_d<=next_best_d;
     if(word_index==15)begin
      emit_full<=0;
      disparity<=next_best>192?8'd255:{2'd0,next_best_d};score<=next_best>192?10'd1023:next_best;pixel_done<=1;
      if(out_last)begin done<=1;state<=IDLE;end
     end else word_index<=word_index+1'b1;
    end
    case(state)
     IDLE:if(cmd_valid && cmd_ready)begin
      if(timeout_cycles==0)begin fault<=8'h90;state<=FAILED;end
      else begin tag<=cmd_tag;watchdog<=timeout_cycles;x<=0;y<=0;support_history<=0;old_ul_left<={390{1'b1}};horizontal<={390{1'b1}};accepted_pixels<=0;emitted_words<=0;elapsed_cycles<=0;state<=WAIT_PIXEL;end
     end
     WAIT_PIXEL:if(take)begin
      if(in_last!=(x==WIDTH-1 && y==HEIGHT-1))begin fault<=8'h91;state<=FAILED;end
      else begin
       left<=in_left;left_support<=in_left_support;right_history[0]<=in_right;for(k=63;k>0;k=k-1)right_history[k]<=right_history[k-1];
       support_history<={support_history[62:0],in_right_support};cost_issue_group<=0;cost_reply_group<=0;path_issue_group<=0;path_reply_group<=0;accepted_pixels<=accepted_pixels+1'b1;state<=COST0;
      end
     end
     COST0:if(cost_ready)begin cost_issue_group<=1;state<=COST1;end
     COST1:if(cost_ready)begin if(cost_issue_group==3)state<=COST_WAIT;else cost_issue_group<=cost_issue_group+1'b1;end
     PATH_LOAD:begin previous_shift<=previous[383:0];previous_min<=previous[389:384];previous_low<=63;state<=PATH0;end
     PATH0:if(path_ready)begin path_issue_group<=1;state<=PATH1;end
     PATH1:if(path_ready)begin
      if(path_issue_group==3)begin
       if(path_index==3)state<=PATH_WAIT;
       else begin path_index<=path_index+1'b1;path_issue_group<=0;end
      end else path_issue_group<=path_issue_group+1'b1;
     end
     PUBLISH:if(slot_free)begin
      // The final path group updated aggregate on the preceding clock.
      // One finite slot protects completed data while compute admits the next
      // raster pixel. Four-path/horizontal/row feedback order is unchanged.
      emit_aggregate<=aggregate;emit_costs<=costs;emit_x<=x;emit_y<=y;emit_full<=1;
      word_index<=0;best<=1023;best_d<=0;
      if(x==WIDTH-1 && y==HEIGHT-1)state<=WAIT_OUTPUT;
      else begin if(x==WIDTH-1)begin x<=0;y<=y+1'b1;end else x<=x+1'b1;state<=WAIT_PIXEL;end
     end
     FAILED:if(clear)begin fault<=0;emit_full<=0;state<=IDLE;end
     default:begin end
    endcase
   end
  end
 end
endmodule
`default_nettype wire
