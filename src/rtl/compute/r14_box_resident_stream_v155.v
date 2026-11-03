`timescale 1ns/1ps
`default_nettype none
// Fixed banks and metadata fill allow exact nine-tap SRL payload.
// One physical 40KiB ring/column/last-row set serves scalar vertical/BOX/
// prefix jobs and the four-plane resident colour job. No duplicate BOX RAM.
// Scalar planes use plane0 of the same layout. Q40/RNE and clipped boundaries
// retain the original math; four-plane commands are only supported for mode2.
module r14_box_resident_stream_v155(
 input wire clk,rst_n,start,cancel,input wire[6:0]height,input wire[1:0]cmd_mode,input wire planes_four,
 input wire[31:0]cycle_limit,input wire in_valid,output wire in_ready,
 input wire[63:0]in_data,input wire in_last,
 output wire out_valid,input wire out_ready,output wire[63:0]out_data,
 output wire out_last,output wire quiescent,output reg busy,done,cancelled,
 output reg[7:0]fault,output reg[31:0]cycles,accepted_values,output_values
);
 reg[6:0]h,fr,fx;reg[1:0]plane;reg[3:0]slot;reg[4:0]tail;
 reg[31:0]limit;reg tail_last;reg[1:0]mode;reg four;
 reg prefix_zero,prefix_operand_valid,prefix_operand_zero,prefix_operand_last;
 reg signed[63:0]prefix_sum,prefix_operand;
 (* ram_style="block" *)reg[63:0]ring[0:4095];
 (* ram_style="block" *)reg[63:0]column_sum[0:511];
 (* ram_style="block" *)reg[63:0]last_row[0:511];
 reg[63:0]old_q,base_q,last_q;
 reg s0v,s1v,s2v;reg[6:0]s0r,s0x,s1r,s1x,s2r,s2x;
 reg[1:0]s0p,s1p,s2p;reg[11:0]s0addr;reg s0real;
 reg signed[63:0]s0new,s1delta,s1base,s2sum;
 reg signed[63:0]hsum[0:3],last_vertical[0:3],first_vertical[0:3];
 reg[3:0]fresh_count[0:3];
 (* shreg_extract="yes" *)reg signed[63:0]hwindow[0:35];
 reg emit_valid,emit_last;reg signed[63:0]emit_data;
 wire q_valid,q_ready,q_error,q_last,q_in_ready,q_quiet,out_quiet;
 wire[63:0]q_data;
 wire[31:0]pixels=h==60?32'd4800:32'd5120;
 wire[31:0]source_limit=four?(pixels<<2):pixels;
 wire advance=!out_valid || out_ready;
 wire step=busy && advance && tail==0 && mode!=3;
 wire prefix_step=busy && advance && mode==3 && !prefix_zero;
 wire last_plane=plane==(four?2'd3:2'd0);
 assign in_ready=rst_n && !start && !cancel && busy && accepted_values<source_limit && q_in_ready;
 wire raw_take=in_valid && in_ready;
 assign q_ready=!start && !cancel && busy && fr<h && (step || prefix_step);
 wire input_take=q_valid && q_ready;
 wire bad_input=(raw_take && in_last!=(accepted_values==source_limit-1'b1)) || (input_take && q_error);
 wire timeout=busy && cycles>=limit;
 wire front_take=step && (fr<h?input_take:fr<h+4);
 wire[11:0]front_addr=((slot*12'd80)+fx)*12'd4+plane;
 wire[11:0]old_addr=(((fr<9?12'd0:slot*12'd80)+fx)*12'd4)+plane;
 wire[8:0]column_addr={fx,plane};
 wire signed[63:0]s0value=s0real?s0new:$signed(last_q);
 wire signed[63:0]v_result=s1base+s1delta;
 wire[1:0]tail_plane=four?(2'd0-tail[1:0]):2'd0;
 // The same modulo64 add/sub serves the real horizontal step and tail.
 // Each window tap has a constant physical address; only four64bit reads
 // are selected. No36way variable write or variable nineword shift network.
 wire[1:0]horizontal_plane=tail!=0?tail_plane:s2p;
 wire signed[63:0]horizontal_old[0:3];
 wire signed[63:0]horizontal_input=tail!=0?last_vertical[horizontal_plane]:s2sum;
 wire signed[63:0]horizontal_remove=(tail!=0 || s2x>=5)?horizontal_old[horizontal_plane]:64'sd0;
 (* use_dsp="no" *)wire signed[63:0]horizontal_next=hsum[horizontal_plane]+horizontal_input-horizontal_remove;
 // The old parallel nine-tap row fill is represented by first_vertical
 // until nine real shifts have replaced the previous row's payload. This
 // preserves clipped boundaries without reset/load muxes on all2304bits.
 wire horizontal_active=rst_n && !cancel && !start && !timeout && !bad_input && busy &&
                        !(out_valid && out_ready && out_last) && advance && mode!=3;
 genvar bank;
 generate for(bank=0;bank<4;bank=bank+1)begin: HORIZONTAL_BANK
  wire update_bank=horizontal_active &&
                   ((tail!=0 && tail_plane==bank) ||
                    (tail==0 && s2v && s2r>=4 && mode!=0 && s2p==bank));
  assign horizontal_old[bank]=fresh_count[bank]<9?first_vertical[bank]:hwindow[bank*9+8];
  integer tap;
  // Payload only shifts, with one input and one enable; never parallel fill.
  always @(posedge clk)begin
   if(update_bank)begin
    for(tap=8;tap>0;tap=tap-1)hwindow[bank*9+tap]<=hwindow[bank*9+tap-1];
    hwindow[bank*9]<=tail!=0?last_vertical[bank]:s2sum;
   end
  end
  always @(posedge clk)begin
   if(!rst_n || cancel || start)fresh_count[bank]<=0;
   else if(update_bank)begin
    if(tail==0 && s2x==0)begin
     hsum[bank]<=(s2sum<<<2)+s2sum;
     first_vertical[bank]<=s2sum;
     fresh_count[bank]<=1;
    end else begin
     hsum[bank]<=horizontal_next;
     if(fresh_count[bank]<9)fresh_count[bank]<=fresh_count[bank]+1'b1;
    end
    if(tail==0)last_vertical[bank]<=s2sum;
   end
  end
 end endgenerate
 wire cast_clear=start || cancel || timeout || bad_input || (out_valid && out_ready && out_last);
 assign quiescent=!busy && q_quiet && out_quiet;
 r14_f64_to_q40_pipe_v121 input_caster(.clk(clk),.rst_n(rst_n),.clear(cast_clear),
  .in_valid(in_valid && busy && accepted_values<source_limit && !start && !cancel),.in_ready(q_in_ready),
  .in_data(in_data),.in_last(in_last),.prefix_bound(mode==3),.out_valid(q_valid),.out_ready(q_ready),
  .out_data(q_data),.out_last(q_last),.out_error(q_error),.quiescent(q_quiet));
 r14_q40_to_f64_pipe_v121 output_caster(.clk(clk),.rst_n(rst_n),.clear(cast_clear),
  .in_valid(emit_valid),.in_ready(),.in_data(emit_data),.in_last(emit_last),
  .out_valid(out_valid),.out_ready(out_ready),.out_data(out_data),.out_last(out_last),.quiescent(out_quiet));
 // Payloads are not reset. Row0 initializes every column; ring words are
 // written before use. Cancellation revokes pipeline metadata.
 always @(posedge clk)begin
  if(step && !start && !cancel && !bad_input && !timeout)begin
   if(front_take)begin old_q<=ring[old_addr];base_q<=column_sum[column_addr];last_q<=last_row[column_addr];end
   if(s0v)begin ring[s0addr]<=s0value;if(s0real && s0r==h-1'b1)last_row[{s0x,s0p}]<=s0value;end
   if(s1v)column_sum[{s1x,s1p}]<=v_result;
  end
 end
 task flush;
  begin busy<=0;emit_valid<=0;s0v<=0;s1v<=0;s2v<=0;tail<=0;prefix_zero<=0;prefix_operand_valid<=0;end
 endtask
 always @(posedge clk)begin
  done<=0;cancelled<=0;
  if(!rst_n)begin
   flush;fault<=0;cycles<=0;accepted_values<=0;output_values<=0;
   h<=0;fr<=0;fx<=0;plane<=0;slot<=0;limit<=0;tail_last<=0;mode<=0;four<=0;
   prefix_sum<=0;prefix_operand<=0;prefix_operand_zero<=0;prefix_operand_last<=0;
   s0r<=0;s0x<=0;s0p<=0;s0addr<=0;s0real<=0;s0new<=0;
   s1r<=0;s1x<=0;s1p<=0;s1delta<=0;s1base<=0;
   s2r<=0;s2x<=0;s2p<=0;s2sum<=0;emit_data<=0;emit_last<=0;
  end else if(cancel)begin flush;fault<=0;cancelled<=busy;end
  else if(start)begin
   flush;fault<=0;
   if(busy)fault<=8'h11;
   else if(cmd_mode==1 || (planes_four && cmd_mode!=2))fault<=8'h12;
   else if(height!=60 && height!=64)fault<=8'h13;
   else if(!cycle_limit)fault<=8'h14;
   else begin
    busy<=1;h<=height;fr<=0;fx<=0;plane<=0;slot<=0;limit<=cycle_limit;mode<=cmd_mode;four<=planes_four;
    prefix_sum<=0;prefix_zero<=cmd_mode==3;
    cycles<=0;accepted_values<=0;output_values<=0;tail_last<=0;
   end
  end else if(timeout || bad_input)begin flush;fault<=timeout?8'h26:q_error?8'h25:8'h24;end
  else if(busy)begin
   cycles<=cycles+1'b1;
   if(raw_take)accepted_values<=accepted_values+1'b1;
   if(out_valid && out_ready)output_values<=output_values+1'b1;
   if(out_valid && out_ready && out_last)begin flush;done<=1;end
   else if(advance)begin
    emit_valid<=0;emit_last<=0;
    if(mode==3)begin
     prefix_operand_valid<=prefix_zero || input_take;
     if(prefix_zero)begin prefix_operand<=0;prefix_operand_zero<=1;prefix_operand_last<=0;prefix_zero<=0;end
     else if(input_take)begin
      prefix_operand<=q_data;prefix_operand_zero<=0;prefix_operand_last<=q_last;
      if(fx==79)begin fx<=0;fr<=fr+1'b1;prefix_zero<=fr<h-1'b1;end
      else fx<=fx+1'b1;
     end
     if(prefix_operand_valid)begin
      emit_valid<=1;emit_last<=prefix_operand_last;
      emit_data<=prefix_operand_zero?64'sd0:prefix_sum+prefix_operand;
      prefix_sum<=prefix_operand_zero?64'sd0:prefix_sum+prefix_operand;
     end
    end else if(tail!=0)begin
     emit_valid<=1;emit_data<=horizontal_next;emit_last<=tail==1 && tail_last;
     tail<=tail-1'b1;
    end else begin
     s0v<=front_take;s1v<=s0v;s2v<=s1v;
     if(front_take)begin
      s0r<=fr;s0x<=fx;s0p<=plane;s0addr<=front_addr;s0real<=fr<h;s0new<=q_data;
      plane<=four?plane+1'b1:2'd0;
      if(last_plane)begin
       if(fx==79)begin fx<=0;fr<=fr+1'b1;slot<=slot==8?0:slot+1'b1;end
       else fx<=fx+1'b1;
      end
     end
     if(s0v)begin
      s1r<=s0r;s1x<=s0x;s1p<=s0p;s1base<=s0r==0?64'sd0:$signed(base_q);
      if(s0r==0)s1delta<=(s0value<<<2)+s0value;
      else if(s0r<5)s1delta<=s0value;
      else s1delta<=s0value-$signed(old_q);
     end
     if(s1v)begin s2sum<=v_result;s2r<=s1r;s2x<=s1x;s2p<=s1p;end
     if(s2v && s2r>=4)begin
      if(mode==0)begin emit_valid<=1;emit_data<=s2sum;emit_last<=s2r==h+3 && s2x==79;end
      else begin
      if(s2x>=4)begin emit_valid<=1;emit_data<=horizontal_next;end
      if(s2x==79 && s2p==(four?2'd3:2'd0))begin tail<=four?16:4;tail_last<=s2r==h+3;end
      end
     end
    end
   end
  end
 end
endmodule
`default_nettype wire
