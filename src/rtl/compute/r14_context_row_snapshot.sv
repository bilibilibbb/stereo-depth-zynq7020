`timescale 1ns/1ps
/* Four signed16x18 products. A row cache replaces whole floating tensors.
 * Exact FP32 per-operation rounding is preserved on dyadic integer numerators.
 * This core has no AXI/MMIO/clock-crossing wrapper yet. */
module r14_context_row_v202(
 input wire clk,input wire rst_n,input wire clear,input wire cancel,
 input wire load_valid,output wire load_ready,input wire load_right,
 input wire load_group,input wire [1:0] load_lane,input wire [6:0] load_x,input wire [63:0] load_data,
 input wire start,output wire busy,output reg done,output reg fault,
 output reg [3:0] fault_code,
 output reg out_valid,input wire out_ready,output reg [10:0] out_index,
 output reg signed [15:0] out_data);

 localparam IDLE=0,READ0=1,PROD0=2,ROUND0=3,READ1=4,PROD1=5,
            ROUND1=6,ADD1=7,SUM01=8,SUM2=9,SUM3=10,QUANT=11,OUTPUT=12,FINISH0=13,FINISH1=14,ENCODE_ADD=15,FINISH_ADD=16,ENCODE_SUM=17,FINISH_SUM=18;
 reg [4:0] state;reg [4:0] sum_return;
 reg [10:0] loaded;
 reg [4:0] disparity;
 reg [6:0] x;
 wire read_enable=state==READ0 || state==READ1;
 wire read_group=state==READ1;
 wire [6:0] shift=disparity==16?7'd15:{2'b0,disparity};
 wire valid0=x>=shift;
 wire valid1=x>=16;
 wire [7:0] laddr=(read_group?8'd80:8'd0)+x;
 wire [7:0] raddr0=(read_group?8'd80:8'd0)+(valid0?x-shift:7'd0);
 wire [7:0] raddr1=(read_group?8'd80:8'd0)+(valid1?x-7'd16:7'd0);
 wire [7:0] waddr=(load_group?8'd80:8'd0)+load_x;
 // Source is CHW: one signed16 value, x then channel then side.
 // A64bit HP beat can be serialized into4words; no CPU transpose is needed.
 // Same CHW order, tracked incrementally to keep subtract/decode chains
 // out of the memory write enable. The original loaded counter still
 // enforces capacity and complete-row admission. No extra transfer stage.
 reg expected_right,expected_group;
 reg [1:0] expected_lane;
 reg [6:0] expected_x;
 wire ordered_load=(load_x<80) && (load_right==expected_right) &&
     (load_group==expected_group) && (load_lane==expected_lane) && (load_x==expected_x);
 assign load_ready=state==IDLE && !fault && loaded<1280;
 assign busy=state!=IDLE;
 wire accept_load=load_valid && load_ready && ordered_load;
 wire signed [15:0] left_q[0:3],right0_q[0:3],right1_q[0:3];
 reg signed [33:0] product[0:3];
 reg signed [39:0] partial[0:3],second[0:3];
 reg signed [39:0] sum;
 reg [30:0] round_code[0:3];
 (* use_dsp="yes" *) reg signed [39:0] addend[0:3];
 wire signed [39:0] product_wide[0:3];
 wire signed [17:0] weighted_right[0:3];
 wire signed [39:0] round_operand[0:3];

 // For signed16 CHW features, each interpolated integer product is
 // bounded by2^32. Two terms per part and four parts bound every FP24
 // operand by2^35 inclusive. RNE preserves these exactly representable
 // power-of-two bounds. Therefore the discarded span is at most12bits.
 // A25-bit rounded significand and explicit guard/sticky/tie bits replace
 // generic40-bit variable masks and comparisons; FP24 semantics are kept.
 // Encode the exact magnitude/significand and tie decision first; perform
 // significand carry, dyadic restoration and sign in the following stage.
 // All operands are within +/-2^35, proven from the eight signed16 inputs.
 function automatic [30:0] fp24_pack(input signed [39:0] value);
  reg signed [36:0] magnitude_signed;reg [35:0] a;reg [24:0] significand;
  reg [5:0] top;reg [3:0] shift_bits;reg increment;integer bitno;
  begin
   magnitude_signed=value[39]?-$signed(value[36:0]):$signed(value[36:0]);
   a=magnitude_signed[35:0];top=0;
   for(bitno=0;bitno<36;bitno=bitno+1)if(a[bitno])top=bitno;
   shift_bits=top<24?0:top-23;increment=0;
   case(shift_bits)
    4'd1:increment=a[0] && a[1];
    4'd2:increment=a[1] && (a[0] || a[2]);
    4'd3:increment=a[2] && ((|a[1:0]) || a[3]);
    4'd4:increment=a[3] && ((|a[2:0]) || a[4]);
    4'd5:increment=a[4] && ((|a[3:0]) || a[5]);
    4'd6:increment=a[5] && ((|a[4:0]) || a[6]);
    4'd7:increment=a[6] && ((|a[5:0]) || a[7]);
    4'd8:increment=a[7] && ((|a[6:0]) || a[8]);
    4'd9:increment=a[8] && ((|a[7:0]) || a[9]);
    4'd10:increment=a[9] && ((|a[8:0]) || a[10]);
    4'd11:increment=a[10] && ((|a[9:0]) || a[11]);
    4'd12:increment=a[11] && ((|a[10:0]) || a[12]);
    default:increment=0;
   endcase
   significand=a>>shift_bits;
   fp24_pack={value[39],shift_bits,increment,significand};
  end
 endfunction
 function automatic signed [39:0] fp24_restore(input [30:0] encoded);
  reg [24:0] significand;reg [36:0] expanded;reg signed [36:0] signed_value;
  begin
   significand=encoded[24:0]+encoded[25];
   expanded={12'd0,significand}<<encoded[29:26];
   signed_value=encoded[30]?-$signed(expanded):$signed(expanded);
   fp24_restore={{3{signed_value[36]}},signed_value};
  end
 endfunction
 function automatic signed [15:0] q11(input signed [39:0] value,input interp);
  reg [39:0] a,q,remainder,half,mask;integer s;
  begin
   s=interp?16:14;a=value<0?-value:value;
   q=a>>s;mask=(40'd1<<s)-1;half=40'd1<<(s-1);remainder=a&mask;
   if(remainder>half || (remainder==half && q[0]))q=q+1;
   if(q>32767)q=32767;
   q11=value<0?-$signed(q[15:0]):$signed(q[15:0]);
  end
 endfunction

 genvar lane;
 generate for(lane=0;lane<4;lane=lane+1)begin:lanes
  (* ram_style="block" *) reg signed [15:0] left_mem[0:159];
  (* ram_style="block" *) reg signed [15:0] right_mem[0:159];
  reg signed [15:0] lq,r0q,r1q;
  always @(posedge clk)begin
   if(accept_load && !load_right && load_lane==lane)left_mem[waddr]<=load_data[15:0];
   if(read_enable)lq<=left_mem[laddr];
   if(accept_load && load_right && load_lane==lane)right_mem[waddr]<=load_data[15:0];
   else if(read_enable)r0q<=right_mem[raddr0];
   if(read_enable)r1q<=right_mem[raddr1];
  end
  assign left_q[lane]=lq;
  assign right0_q[lane]=r0q;
  assign right1_q[lane]=r1q;
  wire signed [17:0] r0=valid0?{{2{r0q[15]}},r0q}:18'sd0;
  wire signed [17:0] r1=valid1?{{2{r1q[15]}},r1q}:18'sd0;
  assign weighted_right[lane]=disparity==16?r0+r1+(r1<<<1):r0;
  assign product_wide[lane]={{6{product[lane][33]}},product[lane]};
  // Same operand consumed by fp24_pack in this clock stage.
  assign round_operand[lane]=(state==ROUND0 || state==ROUND1)?product_wide[lane]:addend[lane];
 end endgenerate

 integer k;
 always @(posedge clk)begin
  if(!rst_n)begin
   state<=IDLE;loaded<=0;expected_right<=0;expected_group<=0;expected_lane<=0;expected_x<=0;disparity<=0;x<=0;done<=0;fault<=0;fault_code<=0;
   out_valid<=0;out_index<=0;out_data<=0;sum<=0;sum_return<=0;
   for(k=0;k<4;k=k+1)begin product[k]<=0;partial[k]<=0;second[k]<=0;round_code[k]<=0;addend[k]<=0;end
  end else if(cancel)begin
   state<=IDLE;loaded<=0;expected_right<=0;expected_group<=0;expected_lane<=0;expected_x<=0;out_valid<=0;done<=0;fault<=1;fault_code<=3;
  end else if(clear && state==IDLE)begin
   loaded<=0;expected_right<=0;expected_group<=0;expected_lane<=0;expected_x<=0;out_valid<=0;done<=0;fault<=0;fault_code<=0;
  end else begin
   done<=0;
   if(load_valid && load_ready)begin
    if(ordered_load)begin
     loaded<=loaded+1;
     if(expected_x==79)begin
      expected_x<=0;
      expected_lane<=expected_lane+1'b1;
      if(expected_lane==3)begin
       expected_group<=!expected_group;
       if(expected_group)expected_right<=!expected_right;
      end
     end else expected_x<=expected_x+1'b1;
    end
    else begin fault<=1;fault_code<=1;end
   end
   if(start && state!=IDLE)begin fault<=1;fault_code<=4;end
   case(state)
    IDLE:if(start)begin
     if(loaded!=1280 || fault || load_valid)begin fault<=1;fault_code<=2;end
     else begin state<=READ0;loaded<=0;expected_right<=0;expected_group<=0;expected_lane<=0;expected_x<=0;disparity<=0;x<=0;out_valid<=0;end
    end
    READ0:state<=PROD0;
    PROD0:begin
     for(k=0;k<4;k=k+1)product[k]<=$signed(left_q[k])*$signed(weighted_right[k]);
     state<=ROUND0;
    end
    ROUND0:begin
     for(k=0;k<4;k=k+1)round_code[k]<=fp24_pack(product_wide[k]);
     state<=FINISH0;
    end
    FINISH0:begin
     for(k=0;k<4;k=k+1)partial[k]<=fp24_restore(round_code[k]);
     state<=READ1;
    end
    READ1:state<=PROD1;
    PROD1:begin
     for(k=0;k<4;k=k+1)product[k]<=$signed(left_q[k])*$signed(weighted_right[k]);
     state<=ROUND1;
    end
    ROUND1:begin
     for(k=0;k<4;k=k+1)round_code[k]<=fp24_pack(product_wide[k]);
     state<=FINISH1;
    end
    FINISH1:begin
     for(k=0;k<4;k=k+1)second[k]<=fp24_restore(round_code[k]);
     state<=ADD1;
    end
    ADD1:begin
     for(k=0;k<4;k=k+1)addend[k]<=partial[k]+second[k];
     state<=ENCODE_ADD;
    end
    ENCODE_ADD:begin
     for(k=0;k<4;k=k+1)round_code[k]<=fp24_pack(addend[k]);
     state<=FINISH_ADD;
    end
    FINISH_ADD:begin
     for(k=0;k<4;k=k+1)partial[k]<=fp24_restore(round_code[k]);
     state<=SUM01;
    end
    SUM01:begin addend[0]<=partial[0]+partial[1];sum_return<=SUM2;state<=ENCODE_SUM;end
    SUM2:begin addend[0]<=sum+partial[2];sum_return<=SUM3;state<=ENCODE_SUM;end
    SUM3:begin addend[0]<=sum+partial[3];sum_return<=QUANT;state<=ENCODE_SUM;end
    ENCODE_SUM:begin round_code[0]<=fp24_pack(addend[0]);state<=FINISH_SUM;end
    FINISH_SUM:begin sum<=fp24_restore(round_code[0]);state<=sum_return;end
    QUANT:begin
     out_data<=q11(sum,disparity==16);out_index<=disparity*80+x;
     out_valid<=1;state<=OUTPUT;
    end
    OUTPUT:if(out_valid && out_ready)begin
     out_valid<=0;
     if(x==79 && disparity==16)begin state<=IDLE;done<=1;end
     else begin
      if(x==79)begin x<=0;disparity<=disparity+1;end else x<=x+1;
      state<=READ0;
     end
    end
    default:begin state<=IDLE;out_valid<=0;fault<=1;fault_code<=5;end
   endcase
  end
 end
endmodule
