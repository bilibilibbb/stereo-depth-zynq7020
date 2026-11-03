// Derived from frozen retained_sgm_raw_stream_v1.v SHA256 43778e72fe0d7712b25a287dbb45a5d3d60be5cd66eb1afa4c5b272dfbcb0fc2.
// v225 exact pool and v226 pixel-result pipeline; summary and stream lease protocol retained.
`timescale 1ns/1ps
`default_nettype none
module fw_sgm_raw_stream_rtl #(parameter WIDTH=320,HEIGHT=240)(
 input wire clk,rst_n,cancel,clear,cmd_valid,output wire cmd_ready,input wire[31:0]cmd_tag,timeout_cycles,
 input wire in_valid,output wire in_ready,input wire[23:0]in_left,in_right,input wire in_left_support,in_right_support,in_last,
 output wire native_valid,input wire native_ready,output wire[63:0]native_data,output wire[15:0]native_x,native_y,output wire native_last,
 output wire curve_valid,input wire curve_ready,output wire[575:0]curve_data,output wire[15:0]curve_x,curve_y,output wire curve_last,
 output reg done,cancelled,output reg[15:0]fault,output wire busy,quiescent,output reg[31:0]elapsed_cycles,accepted_pixels,emitted_native,emitted_curves
);
 localparam IDLE=0,RUN=1,DRAIN=2,FAILED=3;
 reg[1:0]state;reg[31:0]tag,watchdog;reg core_seen,pool_seen,native_seen,user_cancel;
 wire cr,pr,cd,pd,cv,pv,sv,cp,sp;wire[7:0]cf,pf,sf;wire[63:0]cost;wire[31:0]raw,ct;wire[15:0]cx,cy;wire[3:0]cg;wire cl;
 wire[271:0]num,mass;wire[31:0]pt;
 wire stop=cancel || state==DRAIN || state==FAILED;
 wire start=cmd_valid&&cmd_ready;
 assign cmd_ready=rst_n && state==IDLE && cr && pr && !cancel && !fault;
 assign busy=state==RUN || state==DRAIN;assign quiescent=state==IDLE || state==FAILED;
 wire child_input_ready;
 assign in_ready=child_input_ready && state==RUN && !stop;
 fw_sgm_volume_rtl #(.WIDTH(WIDTH),.HEIGHT(HEIGHT)) volume(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.clear(1'b0),.cmd_valid(start),.cmd_ready(cr),.cmd_tag(cmd_tag),.timeout_cycles(timeout_cycles),
  .in_valid(in_valid&&state==RUN&&!stop),.in_ready(child_input_ready),.in_left(in_left),.in_right(in_right),.in_left_support(in_left_support),.in_right_support(in_right_support),.in_last(in_last),
  .out_valid(cv),.out_ready(cp&&sp),.out_cost(cost),.out_raw_cost(raw),.out_tag(ct),.out_x(cx),.out_y(cy),.out_group(cg),.out_last(cl),
  .pixel_done(),.disparity(),.score(),.done(cd),.cancelled(),.fault(cf),.busy(),.debug_state(),.accepted_pixels(),.emitted_words(),.elapsed_cycles());
 fw_sgm_curve_pool_rtl #(.WIDTH(WIDTH),.HEIGHT(HEIGHT)) pool(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.clear(1'b0),.cmd_valid(start),.cmd_ready(pr),.cmd_tag(cmd_tag),.timeout_cycles(timeout_cycles),
  .in_valid(cv&&sp),.in_ready(cp),.in_cost(cost),.in_x(cx),.in_y(cy),.in_group(cg),.in_tag(ct),.in_last(cl),
  .out_valid(pv),.out_ready(curve_ready&&state==RUN&&!stop),.out_num(num),.out_mass(mass),.out_x(curve_x),.out_y(curve_y),.out_tag(pt),.out_last(curve_last),
  .done(pd),.cancelled(),.fault(pf),.busy(),.debug_state(),.accepted_words(),.emitted_cells(),.elapsed_cycles());
 retained_sgm_summary_v1 #(.WIDTH(WIDTH),.HEIGHT(HEIGHT)) summary(
  .clk(clk),.rst_n(rst_n),.start(start),.cancel(stop),.cmd_tag(tag),
  .in_valid(cv&&cp),.in_ready(sp),.in_cost(cost),.in_raw(raw),.in_x(cx),.in_y(cy),.in_group(cg),.in_tag(ct),.in_last(cl),
  .out_valid(sv),.out_ready(native_ready&&state==RUN&&!stop),.out_data(native_data),.out_x(native_x),.out_y(native_y),.out_last(native_last),.fault(sf));
 assign curve_valid=pv&&state==RUN&&!stop;assign native_valid=sv&&state==RUN&&!stop;
 assign curve_data={32'd0,mass,num};
 always @(posedge clk)begin
  if(!rst_n)begin state<=IDLE;tag<=0;watchdog<=0;core_seen<=0;pool_seen<=0;native_seen<=0;user_cancel<=0;done<=0;cancelled<=0;fault<=0;elapsed_cycles<=0;accepted_pixels<=0;emitted_native<=0;emitted_curves<=0;end
  else begin
   done<=0;cancelled<=0;
   if(state==RUN)elapsed_cycles<=elapsed_cycles+1'b1;
   if(in_valid&&in_ready)accepted_pixels<=accepted_pixels+1'b1;
   if(native_valid&&native_ready)begin emitted_native<=emitted_native+1'b1;if(native_last)native_seen<=1;end
   if(curve_valid&&curve_ready)emitted_curves<=emitted_curves+1'b1;
   if(cancel && state!=DRAIN)begin state<=DRAIN;user_cancel<=1;end
   else case(state)
    IDLE:if(start)begin state<=RUN;tag<=cmd_tag;watchdog<=timeout_cycles;core_seen<=0;pool_seen<=0;native_seen<=0;user_cancel<=0;fault<=0;elapsed_cycles<=0;accepted_pixels<=0;emitted_native<=0;emitted_curves<=0;end
    RUN:begin
     if(cd)core_seen<=1;if(pd)pool_seen<=1;
     if(cf || pf || sf || elapsed_cycles>=watchdog)begin fault<=cf?{8'h01,cf}:(pf?{8'h02,pf}:(sf?{8'h03,sf}:16'h00c0));state<=DRAIN;user_cancel<=0;end
     else if((core_seen||cd)&&(pool_seen||pd)&&native_seen)begin done<=1;state<=IDLE;end
    end
    DRAIN:if(!cancel)begin if(user_cancel)cancelled<=1;state<=fault?FAILED:IDLE;end
    FAILED:if(clear)begin fault<=0;state<=IDLE;end
    default:state<=FAILED;
   endcase
  end
 end
endmodule
`default_nettype wire
