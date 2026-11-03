`timescale 1ns/1ps
// Complete frozen29x64x80 Q11 context, directly from CHW neural features.
// One outstanding64-bit beat. AW and W may handshake independently.
// Cancel/timeout stop new commands and drain every already offered command.
module r14_context_dma_v208 #(
 parameter [31:0] ADDRESS_MIN=32'h18000000,
 parameter [31:0] ADDRESS_END=32'h20000000,
 // The default preserves standalone restart detection. Only the parent
 // whose command-ready structurally implies !busy may specialize this.
 parameter CHECK_BUSY_START=1
)(
 input wire clk,rst_n,clear,cancel,start,
 input wire [31:0] left_address,right_address,prior_address,output_address,cycle_limit,
 output wire busy,quiescent,output reg done,fault,output reg [3:0] fault_code,
 output wire [31:0] m_axi_araddr,output wire [7:0] m_axi_arlen,
 output wire [2:0] m_axi_arsize,output wire [1:0] m_axi_arburst,
 output wire m_axi_arvalid,input wire m_axi_arready,
 input wire [63:0] m_axi_rdata,input wire [1:0] m_axi_rresp,
 input wire m_axi_rlast,m_axi_rvalid,output wire m_axi_rready,
 output wire [31:0] m_axi_awaddr,output wire [7:0] m_axi_awlen,
 output wire [2:0] m_axi_awsize,output wire [1:0] m_axi_awburst,
 output wire m_axi_awvalid,input wire m_axi_awready,
 output wire [63:0] m_axi_wdata,output wire [7:0] m_axi_wstrb,
 output wire m_axi_wlast,m_axi_wvalid,input wire m_axi_wready,
 input wire [1:0] m_axi_bresp,input wire m_axi_bvalid,output wire m_axi_bready
);
 localparam IDLE=0,ROW_CLEAR=1,READ_REQUEST=2,READ_RESPONSE=3,SERIALIZE=4,
            ROW_START=5,CORRELATE=6,WRITE_REQUEST=7,WRITE_RESPONSE=8,
            PRIOR_NEXT=9,FAILED=10,PRIOR_LOW=11,PRIOR_HIGH=12;
 reg [3:0] state,write_resume;
 reg [31:0] left_base,right_base,prior_base,dest_base,limit,elapsed;
 reg [5:0] row;
 reg side,prior_mode,prior_half,row_done,cancel_pending,aw_done,w_done;
 reg [3:0] pending_reason;
 reg [2:0] channel;
 reg [4:0] beat,out_disparity;
 reg [6:0] out_x;
 reg [1:0] word,pack_count;
 reg [13:0] prior_beat;
 reg [63:0] read_word,pack_data,write_data;
 reg [31:0] pack_address,write_address;
 wire core_busy,core_done,core_fault,core_load_ready,core_valid;
 wire [3:0] core_fault_code;
 wire [10:0] core_index;
 wire signed [15:0] core_data;
 assign busy=state!=IDLE && state!=FAILED;
 assign quiescent=!busy;
 wire expired=busy && elapsed>=limit;
 wire stop_requested=cancel_pending || (busy && (cancel || clear || (CHECK_BUSY_START && start))) ||
                     expired || (busy && core_fault && state!=ROW_CLEAR);
 wire [3:0] stop_reason=pending_reason!=0?pending_reason:
                 cancel?4'd3:(clear || (CHECK_BUSY_START && start))?4'd4:expired?4'd7:4'd9;
 wire core_ready=state==CORRELATE && !stop_requested;
 wire core_load=state==SERIALIZE && !stop_requested;
 wire [15:0] load_word=read_word[word*16+:16];
 r14_context_row_v202 core(
  .clk(clk),.rst_n(rst_n),.clear(state==ROW_CLEAR || (clear && !busy)),
  .cancel(busy && stop_requested),.load_valid(core_load),.load_ready(core_load_ready),
  .load_right(side),.load_group(channel[2]),.load_lane(channel[1:0]),
  .load_x({beat,word}),.load_data({48'hbadf00d01234,load_word}),
  .start(state==ROW_START && !stop_requested),.busy(core_busy),.done(core_done),
  .fault(core_fault),.fault_code(core_fault_code),.out_valid(core_valid),
  .out_ready(core_ready),.out_index(core_index),.out_data(core_data));

 assign m_axi_araddr=prior_mode?prior_base+prior_beat*8:
                         (side?right_base:left_base)+channel*10240+row*160+beat*8;
 assign m_axi_arlen=0;assign m_axi_arsize=3;assign m_axi_arburst=1;
 assign m_axi_arvalid=state==READ_REQUEST;
 assign m_axi_rready=state==READ_RESPONSE;
 assign m_axi_awaddr=write_address;assign m_axi_awlen=0;
 assign m_axi_awsize=3;assign m_axi_awburst=1;
 assign m_axi_awvalid=state==WRITE_REQUEST && !aw_done;
 assign m_axi_wdata=write_data;assign m_axi_wstrb=8'hff;assign m_axi_wlast=1;
 assign m_axi_wvalid=state==WRITE_REQUEST && !w_done;
 assign m_axi_bready=state==WRITE_RESPONSE;

 function automatic span_ok(input [31:0] base,input [31:0] bytes);
  reg [32:0] end_address;
  begin
   end_address={1'b0,base}+bytes;
   span_ok=base[2:0]==0 && base>=ADDRESS_MIN && end_address<={1'b0,ADDRESS_END};
  end
 endfunction
 function automatic overlaps(input [31:0] a,input [31:0] an,
                            input [31:0] b,input [31:0] bn);
  begin overlaps=({1'b0,a}<{1'b0,b}+bn) && ({1'b0,b}<{1'b0,a}+an);end
 endfunction
 wire config_ok=cycle_limit!=0 && span_ok(left_address,81920) &&
    span_ok(right_address,81920) && span_ok(prior_address,81920) &&
    span_ok(output_address,296960) &&
    !overlaps(output_address,296960,left_address,81920) &&
    !overlaps(output_address,296960,right_address,81920) &&
    !overlaps(output_address,296960,prior_address,81920);
 // Equivalent to frozen quantize_scaled(float,2048): exact dyadic scaling,
 // nearest even, symmetric32767 clamp. Subnormal inputs round to zero.
 function automatic [15:0] prior_q11(input [31:0] value);
  reg [47:0] scaled;
  reg [15:0] q;
  reg [4:0] shift;
  reg increment;
  begin
   q=0;scaled=0;shift=0;increment=0;
   if(value[30:23]>=131)q=32767;
   else if(value[30:23]>=115)begin
    // Here shift is exactly9..24. Integer and fractional bits share one
    // barrel shift; bit23 is half and lower bits encode nonzero remainder.
    shift=8'd139-value[30:23];
    scaled={1'b1,value[22:0],24'd0}>>shift;
    q=scaled[39:24];
    increment=scaled[23] && ((|scaled[22:0]) || q[0]) && q!=16'd32767;
   end
   // Positive:q+increment. Negative:~q+(1-increment)=-q-increment.
   // Clamp at32767 before this single16bit increment; negative zero stays0.
   prior_q11=(q^{16{value[31]}})+{15'd0,(value[31]^increment)};
  end
 endfunction
 wire prior_nan=(m_axi_rdata[30:23]==255 && m_axi_rdata[22:0]!=0) ||
                (m_axi_rdata[62:55]==255 && m_axi_rdata[54:32]!=0);
 // One exact quantizer processes the accepted beat in two local steps.
 // Reuse read_word; the low result replaces only its consumed low16.
 wire [31:0] prior_float=state==PRIOR_LOW?read_word[31:0]:read_word[63:32];
 wire [15:0] prior_value=prior_q11(prior_float);
 wire [31:0] prior_pair={prior_value,read_word[15:0]};
 // Frozen neural outputs already use symmetric saturation. Preserve the
 // original dequantize/requantize result at the extra signed-pressure limit.
 function automatic [63:0] left_q11_word(input [63:0] value);
  integer lane;
  begin
   for(lane=0;lane<4;lane=lane+1)
    left_q11_word[lane*16+:16]=value[lane*16+:16] == 16'h8000 ? 16'h8001 : value[lane*16+:16];
  end
 endfunction
 wire [3:0] next_load_state=(side && channel==7 && beat==19)?ROW_START:READ_REQUEST;

 task write_beat(input [31:0] address,input [63:0] data,input [3:0] resume);
  begin
   write_address<=address;write_data<=data;write_resume<=resume;
   aw_done<=0;w_done<=0;state<=WRITE_REQUEST;
  end
 endtask
 task advance_load;
  begin
   word<=0;
   if(beat==19)begin
    beat<=0;
    if(channel==7)begin channel<=0;side<=1;end
    else channel<=channel+1;
   end else beat<=beat+1;
   state<=next_load_state;
  end
 endtask
 task finish_fault(input [3:0] reason);
  begin state<=FAILED;fault<=1;fault_code<=reason;done<=0;end
 endtask
 always @(posedge clk)begin
  if(!rst_n)begin
   state<=IDLE;write_resume<=IDLE;done<=0;fault<=0;fault_code<=0;
   left_base<=0;right_base<=0;prior_base<=0;dest_base<=0;limit<=0;elapsed<=0;
   row<=0;side<=0;channel<=0;beat<=0;word<=0;prior_mode<=0;prior_half<=0;
   row_done<=0;out_disparity<=0;out_x<=0;pack_count<=0;prior_beat<=0;
   read_word<=0;pack_data<=0;write_data<=0;pack_address<=0;write_address<=0;
   cancel_pending<=0;pending_reason<=0;aw_done<=0;w_done<=0;
  end else begin
   done<=0;
   if(busy)begin
    if(elapsed!=32'hffffffff)elapsed<=elapsed+1;
    if(core_done)row_done<=1;
    if(stop_requested && !cancel_pending)begin
     cancel_pending<=1;pending_reason<=stop_reason;
    end
   end
   case(state)
    IDLE:begin
     if(clear)begin fault<=0;fault_code<=0;cancel_pending<=0;pending_reason<=0;end
     else if(start)begin
      if(fault || !config_ok)finish_fault(1);
      else begin
       left_base<=left_address;right_base<=right_address;prior_base<=prior_address;
       dest_base<=output_address;limit<=cycle_limit;elapsed<=0;row<=0;
       cancel_pending<=0;pending_reason<=0;prior_mode<=0;prior_half<=0;prior_beat<=0;
       state<=ROW_CLEAR;
      end
     end
    end
    FAILED:if(clear)begin
     fault<=0;fault_code<=0;cancel_pending<=0;pending_reason<=0;state<=IDLE;
    end
    ROW_CLEAR:if(stop_requested)finish_fault(stop_reason);
     else begin
      side<=0;channel<=0;beat<=0;word<=0;row_done<=0;
      out_disparity<=0;out_x<=0;pack_count<=0;state<=READ_REQUEST;
     end
    // Once ARVALID is offered, cancellation must not retract it.
    READ_REQUEST:if(m_axi_arready)state<=READ_RESPONSE;
    READ_RESPONSE:if(m_axi_rvalid)begin
     if(m_axi_rresp!=0 || !m_axi_rlast)begin
      cancel_pending<=1;pending_reason<=6;
      if(m_axi_rlast)finish_fault(6);
     end else if(stop_requested)finish_fault(stop_reason);
     else if(prior_mode)begin
      if(prior_nan)finish_fault(8);
      else begin read_word<=m_axi_rdata;state<=PRIOR_LOW;end
     end else begin read_word<=m_axi_rdata;word<=0;state<=SERIALIZE;end
    end
    PRIOR_LOW:if(stop_requested)finish_fault(stop_reason);
     else begin read_word[15:0]<=prior_value;state<=PRIOR_HIGH;end
    PRIOR_HIGH:if(stop_requested)finish_fault(stop_reason);
     else if(!prior_half)begin
      pack_data[31:0]<=prior_pair;prior_half<=1;prior_beat<=prior_beat+1;
      state<=READ_REQUEST;
     end else begin
      prior_half<=0;prior_beat<=prior_beat+1;
      write_beat(dest_base+256000+(prior_beat-1)*4,{prior_pair,pack_data[31:0]},PRIOR_NEXT);
     end
    SERIALIZE:if(stop_requested)finish_fault(stop_reason);
     else if(core_load_ready)begin
      if(word==3)begin
       advance_load();
       if(!side)write_beat(dest_base+(17+channel)*10240+row*160+beat*8,
                           left_q11_word(read_word),next_load_state);
      end else word<=word+1;
     end
    ROW_START:if(stop_requested)finish_fault(stop_reason);else state<=CORRELATE;
    CORRELATE:if(stop_requested)finish_fault(stop_reason);
     else if(core_valid)begin
      pack_data[pack_count*16+:16]<=core_data;
      if(pack_count==0)pack_address<=dest_base+out_disparity*10240+row*160+out_x*2;
      if(out_x==79)begin out_x<=0;out_disparity<=out_disparity+1;end
      else out_x<=out_x+1;
      if(pack_count==3)begin
       pack_count<=0;write_beat(pack_address,{core_data,pack_data[47:0]},CORRELATE);
      end else pack_count<=pack_count+1;
     end else if(row_done && pack_count==0)begin
      if(row==63)begin prior_mode<=1;prior_beat<=0;prior_half<=0;state<=READ_REQUEST;end
      else begin row<=row+1;state<=ROW_CLEAR;end
     end
    // The same held address/data complete on AW-first or W-first traffic.
    WRITE_REQUEST:begin
     if(m_axi_awready)aw_done<=1;
     if(m_axi_wready)w_done<=1;
     if((aw_done || m_axi_awready) && (w_done || m_axi_wready))state<=WRITE_RESPONSE;
    end
    WRITE_RESPONSE:if(m_axi_bvalid)begin
     if(m_axi_bresp!=0)finish_fault(6);
     else if(stop_requested)finish_fault(stop_reason);
     else state<=write_resume;
    end
    PRIOR_NEXT:if(stop_requested)finish_fault(stop_reason);
     else if(prior_beat==10240)begin state<=IDLE;done<=1;end
     else state<=READ_REQUEST;
    default:finish_fault(9);
   endcase
  end
 end
endmodule
