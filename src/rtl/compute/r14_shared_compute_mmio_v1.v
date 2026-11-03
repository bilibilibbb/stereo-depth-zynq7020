`timescale 1ns/1ps
`default_nettype none
// CP42: full 32-bit writes only; descriptors/configuration are write-only.
// Unsupported strobes/readback return SLVERR without modifying a command.
// Four pages: legacy NN, integer ADD/resize, binary64 field, binary64 BOX.
// Hold command/config valid through registered FP ownership acquisition.
// Accepted counters advance only on the actual compute handshake.
module r14_shared_compute_mmio_v1 #(parameter[31:0]ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000,TWO_HP=1,BUILD_ID=32'h00000000)(
input wire clk,rst_n,
 input wire[11:0]s_axi_awaddr,input wire s_axi_awvalid,output wire s_axi_awready,
 input wire[31:0]s_axi_wdata,input wire[3:0]s_axi_wstrb,input wire s_axi_wvalid,output wire s_axi_wready,
 output reg[1:0]s_axi_bresp,output reg s_axi_bvalid,input wire s_axi_bready,
 input wire[11:0]s_axi_araddr,input wire s_axi_arvalid,output wire s_axi_arready,
 output reg[31:0]s_axi_rdata,output reg[1:0]s_axi_rresp,output reg s_axi_rvalid,input wire s_axi_rready,
 output wire[31:0] m_axi_araddr,
 output wire[31:0] m_axi_awaddr,
 output wire[7:0] m_axi_arlen,
 output wire[7:0] m_axi_awlen,
 output wire[2:0] m_axi_arsize,
 output wire[2:0] m_axi_awsize,
 output wire[1:0] m_axi_arburst,
 output wire[1:0] m_axi_awburst,
 output wire m_axi_arvalid,
 input wire m_axi_arready,
 input wire[63:0] m_axi_rdata,
 input wire[1:0] m_axi_rresp,
 input wire m_axi_rlast,
 input wire m_axi_rvalid,
 output wire m_axi_rready,
 output wire m_axi_awvalid,
 input wire m_axi_awready,
 output wire[63:0] m_axi_wdata,
 output wire[7:0] m_axi_wstrb,
 output wire m_axi_wlast,
 output wire m_axi_wvalid,
 input wire m_axi_wready,
 input wire[1:0] m_axi_bresp,
 input wire m_axi_bvalid,
 output wire m_axi_bready,
output wire [31:0] fp_axi_araddr,output wire [7:0] fp_axi_arlen,
    output wire [2:0] fp_axi_arsize,output wire [1:0] fp_axi_arburst,
    output wire fp_axi_arvalid,input wire fp_axi_arready,
    input wire [63:0] fp_axi_rdata,input wire [1:0] fp_axi_rresp,
    input wire fp_axi_rlast,input wire fp_axi_rvalid,output wire fp_axi_rready,
    output wire [31:0] fp_axi_awaddr,output wire [7:0] fp_axi_awlen,
    output wire [2:0] fp_axi_awsize,output wire [1:0] fp_axi_awburst,
    output wire fp_axi_awvalid,input wire fp_axi_awready,
    output wire [63:0] fp_axi_wdata,output wire [7:0] fp_axi_wstrb,
    output wire fp_axi_wlast,output wire fp_axi_wvalid,input wire fp_axi_wready,
    input wire [1:0] fp_axi_bresp,input wire fp_axi_bvalid,output wire fp_axi_bready);
wire nn_cmd_ready;
wire nn_done;
wire nn_cancelled;
wire[7:0] nn_fault;
wire nn_busy;
wire nn_quiescent;
wire nn_needs_fabric_reset;
wire[7:0] nn_debug_state;
wire[31:0] nn_elapsed_cycles;
wire[31:0] nn_read_beats;
wire[31:0] nn_core_feed_cycles;
wire[31:0] nn_microtiles;
wire[31:0] nn_overlap_read_compute;
wire[31:0] nn_overlap_write_compute;
wire[31:0] nn_write_values;
wire[8:0] nn_debug_x;
wire[8:0] nn_debug_y;
wire[8:0] nn_debug_fetch_y;
wire[4:0] nn_debug_row_valid;
wire integer_cmd_ready;
wire integer_done;
wire integer_cancelled;
wire[15:0] integer_fault;
wire integer_busy;
wire integer_quiescent;
wire integer_needs_fabric_reset;
wire[31:0] integer_elapsed_cycles;
wire[31:0] integer_read_beats;
wire[31:0] integer_write_beats;
wire[31:0] integer_output_values;
wire fp_field_cfg_ready;
wire fp_field_cmd_ready;
wire fp_field_done;
wire fp_field_cancelled;
wire fp_field_busy;
wire fp_field_quiescent;
wire fp_field_needs_fabric_reset;
wire[15:0] fp_field_fault;
wire[15:0] fp_field_accepted_tag;
wire[15:0] fp_field_completed_tag;
wire[31:0] fp_field_elapsed_cycles;
wire[31:0] fp_field_read_beats;
wire[31:0] fp_field_write_beats;
wire[31:0] fp_field_output_values;
wire fp_box_cmd_ready;
wire fp_box_done;
wire fp_box_cancelled;
wire fp_box_busy;
wire fp_box_quiescent;
wire fp_box_needs_fabric_reset;
wire[15:0] fp_box_fault;
wire[15:0] fp_box_accepted_tag;
wire[15:0] fp_box_completed_tag;
wire[31:0] fp_box_elapsed_cycles;
wire[31:0] fp_box_read_beats;
wire[31:0] fp_box_write_beats;
wire[31:0] fp_box_output_values;
wire[1:0] fp_owner;
wire fp_group_quiescent;
wire fp_needs_fabric_reset;
reg[31:0]nn_descriptor[0:80];
reg[31:0]integer_descriptor[0:9];
reg[31:0]field_descriptor[0:3];
reg[31:0]box_descriptor[0:6];
reg[31:0]field_config[0:2];
 reg[3:0]pending,cancel,clear,clear_wait,done_sticky,cancel_sticky;
 reg config_pending;reg[31:0]config_accepted;
 reg[31:0]accepted_tag[0:3],completed_tag[0:3],accepted_count[0:3],completed_count[0:3],control_errors[0:3];
 reg aw_hold,w_hold;reg[11:0]aw_address;reg[31:0]w_data;reg[3:0]w_strobe;
 assign s_axi_awready=rst_n && !aw_hold && !s_axi_bvalid;
 assign s_axi_wready=rst_n && !w_hold && !s_axi_bvalid;
 assign s_axi_arready=rst_n && !s_axi_rvalid;
 wire[3:0]busy={fp_box_busy,fp_field_busy,integer_busy,nn_busy};
 wire[3:0]quiet={fp_box_quiescent,fp_field_quiescent,integer_quiescent,nn_quiescent};
 wire[3:0]reset_needed={fp_box_needs_fabric_reset|fp_needs_fabric_reset,fp_field_needs_fabric_reset|fp_needs_fabric_reset,integer_needs_fabric_reset,nn_needs_fabric_reset};
 wire[3:0]command_ready={fp_box_cmd_ready,fp_field_cmd_ready,integer_cmd_ready,nn_cmd_ready};
 wire[3:0]done={fp_box_done,fp_field_done,integer_done,nn_done};
 wire[3:0]cancelled={fp_box_cancelled,fp_field_cancelled,integer_cancelled,nn_cancelled};
 wire[15:0]fault[0:3];
 assign fault[0]={8'd0,nn_fault};assign fault[1]=integer_fault;
 assign fault[2]=fp_field_fault;assign fault[3]=fp_box_fault;
 wire[31:0]cycles[0:3],read_beats[0:3],write_values[0:3];
 assign cycles[0]=nn_elapsed_cycles;assign cycles[1]=integer_elapsed_cycles;
 assign cycles[2]=fp_field_elapsed_cycles;assign cycles[3]=fp_box_elapsed_cycles;
 assign read_beats[0]=nn_read_beats;assign read_beats[1]=integer_read_beats;
 assign read_beats[2]=fp_field_read_beats;assign read_beats[3]=fp_box_read_beats;
 assign write_values[0]=nn_write_values;assign write_values[1]=integer_output_values;
 assign write_values[2]=fp_field_output_values;assign write_values[3]=fp_box_output_values;
 wire[3:0]owner_ok=4'b1111;
 wire[3:0]editable;
 genvar g;generate for(g=0;g<4;g=g+1)begin:status_flags
  assign editable[g]=quiet[g] && !busy[g] && !pending[g] && !cancel[g] && !clear[g] &&
                      !clear_wait[g] && !reset_needed[g] && fault[g]==0 && owner_ok[g] && !(g==2 && config_pending);
 end endgenerate
 wire[1:0]write_page=aw_address[11:10],read_page=s_axi_araddr[11:10];
 wire[9:0]write_offset=aw_address[9:0],read_offset=s_axi_araddr[9:0];
 integer i;
 always @(posedge clk)begin
  if(!rst_n)begin
   aw_hold<=0;w_hold<=0;aw_address<=0;w_data<=0;w_strobe<=0;
   s_axi_bresp<=0;s_axi_bvalid<=0;s_axi_rdata<=0;s_axi_rresp<=0;s_axi_rvalid<=0;
   pending<=0;cancel<=0;clear<=0;clear_wait<=0;done_sticky<=0;cancel_sticky<=0;
   config_pending<=0;config_accepted<=0;
   for(i=0;i<4;i=i+1)begin accepted_tag[i]<=0;completed_tag[i]<=0;accepted_count[i]<=0;completed_count[i]<=0;control_errors[i]<=0;end
   for(i=0;i<81;i=i+1)nn_descriptor[i]<=0;
   for(i=0;i<10;i=i+1)integer_descriptor[i]<=0;
   for(i=0;i<4;i=i+1)field_descriptor[i]<=0;
   for(i=0;i<7;i=i+1)box_descriptor[i]<=0;
   for(i=0;i<3;i=i+1)field_config[i]<=0;
  end else begin
   cancel<=0;clear<=0;
   for(i=0;i<4;i=i+1)begin
    if(pending[i] && command_ready[i])begin pending[i]<=0;accepted_count[i]<=accepted_count[i]+1'b1;end
    if(done[i])begin done_sticky[i]<=1;completed_tag[i]<=accepted_tag[i];completed_count[i]<=completed_count[i]+1'b1;end
    if(cancelled[i])cancel_sticky[i]<=1;
    if(clear_wait[i] && !clear[i] && !busy[i] && quiet[i] && fault[i]==0 && !reset_needed[i] &&
        (i<2 ? command_ready[i] : !(fp_owner & (i==2?2'b01:2'b10))))clear_wait[i]<=0;
   end
   if(pending[0] && nn_cmd_ready)accepted_tag[0]<=nn_descriptor[7];
   if(pending[1] && integer_cmd_ready)accepted_tag[1]<=integer_descriptor[9];
   if(pending[2] && fp_field_cmd_ready)accepted_tag[2]<={16'd0,field_descriptor[2][15:0]};
   if(pending[3] && fp_box_cmd_ready)accepted_tag[3]<={16'd0,box_descriptor[1][15:0]};
   if(config_pending && fp_field_cfg_ready)begin config_pending<=0;config_accepted<=config_accepted+1'b1;end
   if(s_axi_bvalid && s_axi_bready)s_axi_bvalid<=0;
   if(s_axi_rvalid && s_axi_rready)s_axi_rvalid<=0;
   if(s_axi_awvalid && s_axi_awready)begin aw_hold<=1;aw_address<=s_axi_awaddr;end
   if(s_axi_wvalid && s_axi_wready)begin w_hold<=1;w_data<=s_axi_wdata;w_strobe<=s_axi_wstrb;end
   if(aw_hold && w_hold && !s_axi_bvalid)begin
    aw_hold<=0;w_hold<=0;s_axi_bvalid<=1;s_axi_bresp<=0;
    if(write_offset[1:0]!=0)begin s_axi_bresp<=2;control_errors[write_page]<=control_errors[write_page]+1'b1;end
    else if(write_offset==10'h010 && w_strobe==4'hf)begin
     if(w_data==1 && editable[write_page])begin pending[write_page]<=1;done_sticky[write_page]<=0;cancel_sticky[write_page]<=0;end
     else if(w_data==2 && (busy[write_page] || pending[write_page] || (write_page==2 && config_pending)))begin
      cancel[write_page]<=1;pending[write_page]<=0;cancel_sticky[write_page]<=1;
      if(write_page==2)config_pending<=0;
     end
     else if(w_data==4 && quiet[write_page] && !busy[write_page] && !pending[write_page] &&
             !reset_needed[write_page] && owner_ok[write_page] && !clear_wait[write_page] && !(write_page==2 && config_pending))begin
      clear[write_page]<=1;clear_wait[write_page]<=1;done_sticky[write_page]<=0;cancel_sticky[write_page]<=0;
     end
     else if(w_data==8 && write_page==2 && editable[2])config_pending<=1;
     else begin s_axi_bresp<=2;control_errors[write_page]<=control_errors[write_page]+1'b1;end
    end else begin
     case(write_page)
      2'd0:begin
       if(write_offset>=10'h100 && write_offset<10'h244 && w_strobe==4'hf && editable[0])
        nn_descriptor[(write_offset-10'h100)>>2]<=w_data;
       else begin s_axi_bresp<=2;control_errors[0]<=control_errors[0]+1'b1;end
      end
      2'd1:begin
       if(write_offset>=10'h100 && write_offset<10'h128 && w_strobe==4'hf && editable[1])
        integer_descriptor[(write_offset-10'h100)>>2]<=w_data;
       else begin s_axi_bresp<=2;control_errors[1]<=control_errors[1]+1'b1;end
      end
      2'd2:begin
       if(write_offset>=10'h100 && write_offset<10'h110 && w_strobe==4'hf && editable[2])
        field_descriptor[(write_offset-10'h100)>>2]<=w_data;
       else if(write_offset>=10'h080 && write_offset<10'h08c && w_strobe==4'hf && editable[2])
        field_config[(write_offset-10'h080)>>2]<=w_data;
       else begin s_axi_bresp<=2;control_errors[2]<=control_errors[2]+1'b1;end
      end
      2'd3:begin
       if(write_offset>=10'h100 && write_offset<10'h11c && w_strobe==4'hf && editable[3])
        box_descriptor[(write_offset-10'h100)>>2]<=w_data;
       else begin s_axi_bresp<=2;control_errors[3]<=control_errors[3]+1'b1;end
      end
     endcase
    end
   end
   if(s_axi_arvalid && s_axi_arready)begin
    s_axi_rvalid<=1;s_axi_rresp<=0;s_axi_rdata<=0;
    if(read_offset[1:0]!=0)s_axi_rresp<=2;
    else case(read_offset)
     10'h000:case(read_page)
      0:s_axi_rdata<=32'h524e3134;1:s_axi_rdata<=32'h52493134;
      2:s_axi_rdata<=32'h52463134;3:s_axi_rdata<=32'h52423134;
     endcase
     10'h004:s_axi_rdata<=read_page==0?32'h00010003:read_page==2?32'h00010004:read_page==3?32'h00010002:32'h00010002;
     10'h008:s_axi_rdata<=BUILD_ID;
     10'h00c:s_axi_rdata<=32'hed2076d1;
     10'h010:s_axi_rdata<=0;
     10'h014:s_axi_rdata<={fault[read_page],7'd0,(read_page==2 && config_pending),pending[read_page],
        clear_wait[read_page],reset_needed[read_page],cancel_sticky[read_page],done_sticky[read_page],editable[read_page],quiet[read_page],busy[read_page]};
     10'h018:s_axi_rdata<=accepted_tag[read_page];
     10'h01c:s_axi_rdata<=completed_tag[read_page];
     10'h020:s_axi_rdata<=write_values[read_page];
     10'h024:s_axi_rdata<=cycles[read_page];
     10'h028:s_axi_rdata<=read_beats[read_page];
     10'h02c:s_axi_rdata<=read_page==0?{24'd0,nn_debug_state}:32'd0;
     10'h030:s_axi_rdata<=accepted_count[read_page];
     10'h034:s_axi_rdata<=completed_count[read_page];
     10'h038:s_axi_rdata<=control_errors[read_page];
     10'h03c:s_axi_rdata<=32'hd14cebbd;
     10'h040:s_axi_rdata<=32'h43503433;
     10'h044:if(read_page==2)s_axi_rdata<=config_accepted;else s_axi_rresp<=2;
     10'h048:begin
      if(read_page==2)s_axi_rdata<={28'd0,fp_needs_fabric_reset,fp_field_quiescent,1'b0,fp_owner[0]};
      else if(read_page==3)s_axi_rdata<={28'd0,fp_needs_fabric_reset,fp_box_quiescent,fp_owner[1],1'b0};
      else s_axi_rdata<={28'd0,fp_needs_fabric_reset,fp_group_quiescent,fp_owner};
     end
     default:s_axi_rresp<=2;
    endcase
   end
  end
 end
r14_shared_compute_v2 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END),.TWO_HP(TWO_HP)) engine(
.clk(clk),
.rst_n(rst_n),
.nn_cancel(cancel[0]),
.nn_clear(clear[0]),
.nn_cmd_valid(pending[0]),
.nn_cmd_ready(nn_cmd_ready),
.nn_source_address(nn_descriptor[1]),
.nn_source_bytes(nn_descriptor[2]),
.nn_destination_address(nn_descriptor[3]),
.nn_destination_bytes(nn_descriptor[4]),
.nn_weight_address(nn_descriptor[5]),
.nn_weight_bytes(nn_descriptor[6]),
.nn_cmd_tag(nn_descriptor[7]),
.nn_timeout_cycles(nn_descriptor[8]),
.nn_input_width(nn_descriptor[9][8:0]),
.nn_input_height(nn_descriptor[10][8:0]),
.nn_input_channels(nn_descriptor[11][8:0]),
.nn_output_width(nn_descriptor[12][8:0]),
.nn_output_height(nn_descriptor[13][8:0]),
.nn_output_channels(nn_descriptor[14][4:0]),
.nn_kernel_width(nn_descriptor[15][2:0]),
.nn_kernel_height(nn_descriptor[16][2:0]),
.nn_padding_x(nn_descriptor[17][2:0]),
.nn_padding_y(nn_descriptor[18][2:0]),
.nn_stride2(nn_descriptor[19][0]),
.nn_replicate(nn_descriptor[20][0]),
.nn_source_wide(nn_descriptor[21][8]),
.nn_source_relu(nn_descriptor[21][9]),
.nn_source_fraction(nn_descriptor[21][5:0]),
.nn_target_fraction(nn_descriptor[22][5:0]),
.nn_destination_width(nn_descriptor[23][8:0]),
.nn_destination_height(nn_descriptor[24][8:0]),
.nn_destination_channels(nn_descriptor[25][8:0]),
.nn_channel_start(nn_descriptor[26][8:0]),
.nn_lattice_two(nn_descriptor[27][0]),
.nn_phase_x(nn_descriptor[28][0]),
.nn_phase_y(nn_descriptor[29][0]),
.nn_stripe_pixels(nn_descriptor[30][8:0]),
.nn_bias({nn_descriptor[63],nn_descriptor[62],nn_descriptor[61],nn_descriptor[60],nn_descriptor[59],nn_descriptor[58],nn_descriptor[57],nn_descriptor[56],nn_descriptor[55],nn_descriptor[54],nn_descriptor[53],nn_descriptor[52],nn_descriptor[51],nn_descriptor[50],nn_descriptor[49],nn_descriptor[48],nn_descriptor[47],nn_descriptor[46],nn_descriptor[45],nn_descriptor[44],nn_descriptor[43],nn_descriptor[42],nn_descriptor[41],nn_descriptor[40]}),
.nn_shift({nn_descriptor[66],nn_descriptor[65],nn_descriptor[64]}),
.nn_activation_mode(nn_descriptor[31][1:0]),
.nn_activation_fraction_in(nn_descriptor[32][5:0]),
.nn_activation_fraction_out(nn_descriptor[33][5:0]),
.nn_done(nn_done),
.nn_cancelled(nn_cancelled),
.nn_fault(nn_fault),
.nn_busy(nn_busy),
.nn_quiescent(nn_quiescent),
.nn_needs_fabric_reset(nn_needs_fabric_reset),
.nn_debug_state(nn_debug_state),
.nn_elapsed_cycles(nn_elapsed_cycles),
.nn_read_beats(nn_read_beats),
.nn_core_feed_cycles(nn_core_feed_cycles),
.nn_microtiles(nn_microtiles),
.nn_overlap_read_compute(nn_overlap_read_compute),
.nn_overlap_write_compute(nn_overlap_write_compute),
.nn_write_values(nn_write_values),
.nn_debug_x(nn_debug_x),
.nn_debug_y(nn_debug_y),
.nn_debug_fetch_y(nn_debug_fetch_y),
.nn_debug_row_valid(nn_debug_row_valid),
.nn_cmd_depthwise(nn_descriptor[0][0]),
.nn_dilation_shift(nn_descriptor[34][1:0]),
.nn_dw_weights({nn_descriptor[80][15:0],nn_descriptor[79],nn_descriptor[78],nn_descriptor[77],nn_descriptor[76],nn_descriptor[75],nn_descriptor[74],nn_descriptor[73],nn_descriptor[72],nn_descriptor[71],nn_descriptor[70],nn_descriptor[69],nn_descriptor[68],nn_descriptor[67],nn_descriptor[39][15:0],nn_descriptor[38],nn_descriptor[37],nn_descriptor[36],nn_descriptor[35]}),
.integer_cancel(cancel[1]),
.integer_clear(clear[1]),
.integer_cmd_valid(pending[1]),
.integer_cmd_ready(integer_cmd_ready),
.integer_cmd_op(integer_descriptor[0][1:0]),
.integer_cmd_width(integer_descriptor[1][9:0]),
.integer_cmd_height(integer_descriptor[1][19:10]),
.integer_cmd_channels(integer_descriptor[1][27:20]),
.integer_cmd_a_wide(integer_descriptor[0][8]),
.integer_cmd_b_wide(integer_descriptor[0][9]),
.integer_cmd_destination_wide(integer_descriptor[0][10]),
.integer_cmd_relu_a(integer_descriptor[0][11]),
.integer_cmd_relu_b(integer_descriptor[0][12]),
.integer_cmd_fraction_a(integer_descriptor[0][20:16]),
.integer_cmd_fraction_b(integer_descriptor[0][25:21]),
.integer_cmd_target_fraction(integer_descriptor[0][30:26]),
.integer_prior_address(integer_descriptor[1]),
.integer_source_a(integer_descriptor[2]),
.integer_source_b(integer_descriptor[3]),
.integer_destination(integer_descriptor[4]),
.integer_source_a_bytes(integer_descriptor[5]),
.integer_source_b_bytes(integer_descriptor[6]),
.integer_destination_bytes(integer_descriptor[7]),
.integer_timeout_cycles(integer_descriptor[8]),
.integer_done(integer_done),
.integer_cancelled(integer_cancelled),
.integer_fault(integer_fault),
.integer_busy(integer_busy),
.integer_quiescent(integer_quiescent),
.integer_needs_fabric_reset(integer_needs_fabric_reset),
.integer_elapsed_cycles(integer_elapsed_cycles),
.integer_read_beats(integer_read_beats),
.integer_write_beats(integer_write_beats),
.integer_output_values(integer_output_values),
.fp_field_cancel(cancel[2]),
.fp_field_clear(clear[2]),
.fp_field_cfg_valid(config_pending),
.fp_field_cfg_ready(fp_field_cfg_ready),
.fp_field_cfg_kind(field_config[0][1:0]),
.fp_field_cfg_index(field_config[0][9:2]),
.fp_field_cfg_bank(field_config[0][12:10]),
.fp_field_cfg_slot(field_config[0][16:13]),
.fp_field_cfg_scalar(field_config[0][17]),
.fp_field_cfg_wide(field_config[0][18]),
.fp_field_cfg_data({field_config[2],field_config[1]}),
.fp_field_cmd_valid(pending[2]),
.fp_field_cmd_ready(fp_field_cmd_ready),
.fp_field_cmd_values(field_descriptor[0][16:0]),
.fp_field_cmd_inputs(field_descriptor[1][5:0]),
.fp_field_cmd_outputs(field_descriptor[1][11:8]),
.fp_field_cmd_length(field_descriptor[1][24:16]),
.fp_field_cmd_tag(field_descriptor[2][15:0]),
.fp_field_timeout_cycles(field_descriptor[3]),
.fp_field_done(fp_field_done),
.fp_field_cancelled(fp_field_cancelled),
.fp_field_busy(fp_field_busy),
.fp_field_quiescent(fp_field_quiescent),
.fp_field_needs_fabric_reset(fp_field_needs_fabric_reset),
.fp_field_fault(fp_field_fault),
.fp_field_accepted_tag(fp_field_accepted_tag),
.fp_field_completed_tag(fp_field_completed_tag),
.fp_field_elapsed_cycles(fp_field_elapsed_cycles),
.fp_field_read_beats(fp_field_read_beats),
.fp_field_write_beats(fp_field_write_beats),
.fp_field_output_values(fp_field_output_values),
.fp_box_cancel(cancel[3]),
.fp_box_clear(clear[3]),
.fp_box_cmd_valid(pending[3]),
.fp_box_cmd_ready(fp_box_cmd_ready),
.fp_box_cmd_mode(box_descriptor[0][1:0]),
.fp_box_cmd_width(box_descriptor[0][14:8]),
.fp_box_cmd_height(box_descriptor[0][22:16]),
.fp_box_cmd_tag(box_descriptor[1][15:0]),
.fp_box_source(box_descriptor[2]),
.fp_box_destination(box_descriptor[3]),
.fp_box_source_bytes(box_descriptor[4]),
.fp_box_destination_bytes(box_descriptor[5]),
.fp_box_timeout_cycles(box_descriptor[6]),
.fp_box_done(fp_box_done),
.fp_box_cancelled(fp_box_cancelled),
.fp_box_busy(fp_box_busy),
.fp_box_quiescent(fp_box_quiescent),
.fp_box_needs_fabric_reset(fp_box_needs_fabric_reset),
.fp_box_fault(fp_box_fault),
.fp_box_accepted_tag(fp_box_accepted_tag),
.fp_box_completed_tag(fp_box_completed_tag),
.fp_box_elapsed_cycles(fp_box_elapsed_cycles),
.fp_box_read_beats(fp_box_read_beats),
.fp_box_write_beats(fp_box_write_beats),
.fp_box_output_values(fp_box_output_values),
.fp_owner(fp_owner),
.fp_group_quiescent(fp_group_quiescent),
.fp_needs_fabric_reset(fp_needs_fabric_reset),
.m_axi_araddr(m_axi_araddr),
.m_axi_awaddr(m_axi_awaddr),
.m_axi_arlen(m_axi_arlen),
.m_axi_awlen(m_axi_awlen),
.m_axi_arsize(m_axi_arsize),
.m_axi_awsize(m_axi_awsize),
.m_axi_arburst(m_axi_arburst),
.m_axi_awburst(m_axi_awburst),
.m_axi_arvalid(m_axi_arvalid),
.m_axi_arready(m_axi_arready),
.m_axi_rdata(m_axi_rdata),
.m_axi_rresp(m_axi_rresp),
.m_axi_rlast(m_axi_rlast),
.m_axi_rvalid(m_axi_rvalid),
.m_axi_rready(m_axi_rready),
.m_axi_awvalid(m_axi_awvalid),
.m_axi_awready(m_axi_awready),
.m_axi_wdata(m_axi_wdata),
.m_axi_wstrb(m_axi_wstrb),
.m_axi_wlast(m_axi_wlast),
.m_axi_wvalid(m_axi_wvalid),
.m_axi_wready(m_axi_wready),
.m_axi_bresp(m_axi_bresp),
.m_axi_bvalid(m_axi_bvalid),
.m_axi_bready(m_axi_bready),
.fp_axi_araddr(fp_axi_araddr),.fp_axi_arlen(fp_axi_arlen),.fp_axi_arsize(fp_axi_arsize),.fp_axi_arburst(fp_axi_arburst),.fp_axi_arvalid(fp_axi_arvalid),.fp_axi_arready(fp_axi_arready),.fp_axi_rdata(fp_axi_rdata),.fp_axi_rresp(fp_axi_rresp),.fp_axi_rlast(fp_axi_rlast),.fp_axi_rvalid(fp_axi_rvalid),.fp_axi_rready(fp_axi_rready),.fp_axi_awaddr(fp_axi_awaddr),.fp_axi_awlen(fp_axi_awlen),.fp_axi_awsize(fp_axi_awsize),.fp_axi_awburst(fp_axi_awburst),.fp_axi_awvalid(fp_axi_awvalid),.fp_axi_awready(fp_axi_awready),.fp_axi_wdata(fp_axi_wdata),.fp_axi_wstrb(fp_axi_wstrb),.fp_axi_wlast(fp_axi_wlast),.fp_axi_wvalid(fp_axi_wvalid),.fp_axi_wready(fp_axi_wready),.fp_axi_bresp(fp_axi_bresp),.fp_axi_bvalid(fp_axi_bvalid),.fp_axi_bready(fp_axi_bready));
endmodule
`default_nettype wire
