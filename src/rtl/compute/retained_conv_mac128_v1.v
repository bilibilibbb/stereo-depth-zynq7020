`timescale 1ns/1ps
`default_nettype none
// Current11459 dense convolution K<=32; four32x64 distributed weight banks,1KiB total.
// Raw-row -> exact full-K INT48 accumulation. No expanded A tile and no K256
// partial-sum roundtrip. This core has no DDR controller or output quantizer.
// Both remain explicit integration work; output rows retain their full48bits.
module retained_conv_mac128_v1(
 input wire clk,rst_n,cancel,clear,
 input wire raw_valid,output wire raw_ready,input wire[13:0]raw_address,
 input wire[63:0]raw_data,input wire[3:0]raw_mask,input wire[3:0]raw_skew,
 input wire cmd_depthwise,cmd_dw_pair,input wire[575:0]cmd_dw_weights,input wire[15:0]cmd_row_skews,
 input wire weight_valid,output wire weight_ready,input wire weight_clear,
 input wire[11:0]weight_address,input wire[63:0]weight_data,
 input wire cmd_valid,output wire cmd_ready,
 input wire[15:0]cmd_channels,input wire[8:0]cmd_width,cmd_pitch,
 input wire[2:0]cmd_kw,cmd_kh,input wire[1:0]cmd_dilation_shift,input wire cmd_stride2,cmd_replicate,
 input wire signed[10:0]cmd_first_x,input wire[6:0]cmd_pixels,
 input wire[55:0]cmd_row_offsets,input wire[3:0]cmd_row_valid,
 input wire[31:0]cmd_tag,timeout_cycles,
 output wire out_valid,input wire out_ready,output wire[767:0]out_data,
 output wire[6:0]out_row,output wire out_last,output wire[31:0]out_tag,
 output reg busy,done,output reg[7:0]fault,
 output reg[31:0]elapsed_cycles,output wire[15:0]accepted_terms,committed_terms,
 output wire[31:0]read_vectors,raw_written_words,output wire[12:0]loaded_weight_words
);
 // Command payload is captured at acceptance. Two preparation stages
 // isolate K arithmetic and legality from the physical MAC/window start.
 // Arithmetic lanes and steady-state feed handshakes are unchanged.
 reg[1:0]command_phase;
 reg[18:0]channel_kernel,terms;
 reg[15:0]saved_channels,saved_row_skews;
 reg[8:0]saved_width,saved_pitch;
 reg[2:0]saved_kw,saved_kh;
 reg[1:0]saved_dilation_shift;
 reg saved_stride2,saved_replicate;
 reg signed[10:0]saved_first_x;
 reg[55:0]saved_row_offsets;
 reg[3:0]saved_row_valid;
 reg dw_mode,dw_pair;reg[575:0]dw_coeff;
 reg[12:0]weight_count;reg[31:0]tag,watchdog;reg[6:0]row,pixels;
 wire stop=cancel || fault!=0;
 wire wready,wvalid,wlast,wdone;wire[7:0]wf,raw_fault;
 wire[167:0]addresses;wire[11:0]mask;wire[9:0]wk;wire[31:0]wtag;
 wire bank_ready,bank_valid;wire[767:0]bank_data;wire[31:0]bank_tag;
 wire mac_start_ready,mac_feed_ready,mac_result,mac_bad,mac_row_fault;
 wire[255:0]weights;wire[63:0]unused_data;wire unused_fault;
 wire take=cmd_valid && cmd_ready;
 wire invalid=watchdog==0 || terms==0 || terms>(dw_mode?64:32) || (!dw_mode && weight_count!=terms*4);
 wire launch=command_phase==3 && busy && !stop && !wf && !raw_fault;
 assign cmd_ready=rst_n && !stop && !wf && !raw_fault && !busy && wready && mac_start_ready && !weight_valid && !weight_clear;
 assign weight_ready=rst_n && !stop && !busy;
 assign loaded_weight_words=weight_count;
 wire read_take=wvalid && bank_ready;
 genvar b;
 generate for(b=0;b<4;b=b+1)begin:B
  (* ram_style="distributed" *)reg[63:0]memory[0:31];reg[63:0]q;
  always @(posedge clk)if(weight_valid && weight_ready && weight_address==weight_count && weight_count<128 && weight_address[1:0]==b)
    memory[weight_address[6:2]]<=weight_data;
  always @(posedge clk)if(read_take)q<=memory[wk[4:0]];
  assign weights[b*64+:64]=q;
 end endgenerate
 direct_row_window_v1 window(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.clear(clear),.cmd_valid(launch),.cmd_ready(wready),
  .cmd_depthwise(dw_mode),.cmd_dw_pair(dw_pair),.cmd_row_skews(saved_row_skews),
  .cmd_channels(saved_channels),.cmd_width(saved_width),.cmd_pitch(saved_pitch),.cmd_kw(saved_kw),.cmd_kh(saved_kh),
  .cmd_stride2(saved_stride2),.cmd_replicate(saved_replicate),.cmd_dilation_shift(saved_dilation_shift),.cmd_first_x(saved_first_x),.cmd_pixels(pixels),
  .cmd_row_offsets(saved_row_offsets),.cmd_row_valid(saved_row_valid),.cmd_tag(tag),
  .out_valid(wvalid),.out_ready(bank_ready),.out_addresses(addresses),.out_mask(mask),.out_k(wk),.out_last(wlast),.out_tag(wtag),
  .done(wdone),.fault(wf),.accepted_vectors());
 row_operand_scratch_dw4_v145 scratch(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.clear(clear),.wr_valid(raw_valid),.wr_ready(raw_ready),.wr_pixel_address(raw_address),.wr_data(raw_data),.wr_mask(raw_mask),.wr_skew(raw_skew),
  .rd_valid(wvalid),.rd_ready(bank_ready),.rd_depthwise(dw_mode),.rd_groups(saved_channels[2:0]),.rd_pixel_addresses(addresses),.rd_mask(mask),.rd_tag({22'd0,wk}),
  .out_valid(bank_valid),.out_ready(mac_feed_ready),.out_data(bank_data),.out_tag(bank_tag),.fault(raw_fault),.written_words(raw_written_words),.read_vectors(read_vectors));
 wire[575:0]dw_pixels0,dw_pixels1;
 genvar dg,dy,dx;
 generate for(dg=0;dg<4;dg=dg+1)begin:DW_CHANNEL
  for(dy=0;dy<3;dy=dy+1)begin:DW_ROW
   for(dx=0;dx<3;dx=dx+1)begin:DW_TAP
    assign dw_pixels0[(dg*9+dy*3+dx)*16+:16]=bank_data[(dg*12+dy*4+dx)*16+:16];
    assign dw_pixels1[(dg*9+dy*3+dx)*16+:16]=dw_pair?bank_data[(dg*12+dy*4+dx+1)*16+:16]:16'd0;
   end
  end
 end endgenerate
 tile_mac_dw4_shared_v144 mac(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.start(launch),.terms(terms[15:0]),.start_ready(mac_start_ready),
  .feed_valid(bank_valid && !stop),.feed_ready(mac_feed_ready),.feed_a(bank_data[127:0]),.feed_b(weights),.start_dw(dw_mode),.start_dw_pair(dw_pair),
  .start_dw_groups(saved_channels[2:0]),.feed_dw_weights(dw_coeff),.feed_dw_pixels0(dw_pixels0),.feed_dw_pixels1(dw_pixels1),
  .result_valid(mac_result),.result_ready(out_valid && out_ready && out_last),
  .result_index(8'd0),.result_data(unused_data),.result_index_fault(unused_fault),
  .result_row(row),.row_advance(out_valid && out_ready && !out_last),.row_data(out_data),.result_row_fault(mac_row_fault),
  .bad_config(mac_bad),.accepted_terms(accepted_terms),.committed_terms(committed_terms));
 assign out_valid=rst_n && busy && mac_result && !stop && !mac_row_fault;
 assign out_row=row;assign out_last=row+1==pixels;assign out_tag=tag;
 always @(posedge clk)begin
  if(!rst_n || cancel)begin
   command_phase<=0;channel_kernel<=0;terms<=0;
   saved_channels<=0;saved_width<=0;saved_pitch<=0;saved_kw<=0;saved_kh<=0;
   saved_dilation_shift<=0;saved_stride2<=0;saved_replicate<=0;saved_first_x<=0;
   saved_row_offsets<=0;saved_row_valid<=0;saved_row_skews<=0;
   dw_mode<=0;dw_pair<=0;dw_coeff<=0;weight_count<=0;tag<=0;watchdog<=0;row<=0;pixels<=0;busy<=0;done<=0;fault<=0;elapsed_cycles<=0;end
  else begin
   done<=0;
   if(clear && !busy)begin fault<=0;weight_count<=0;end
   if(weight_clear && weight_ready)weight_count<=0;
   if(weight_valid && weight_ready)begin
    if(weight_clear || weight_address!=weight_count || weight_count>=128)fault<=8'h95;
    else weight_count<=weight_count+1'b1;
   end
   if(take)begin
    channel_kernel<=cmd_channels*cmd_kw;command_phase<=1;
    saved_channels<=cmd_channels;saved_width<=cmd_width;saved_pitch<=cmd_pitch;
    saved_kw<=cmd_kw;saved_kh<=cmd_kh;saved_dilation_shift<=cmd_dilation_shift;
    saved_stride2<=cmd_stride2;saved_replicate<=cmd_replicate;saved_first_x<=cmd_first_x;
    saved_row_offsets<=cmd_row_offsets;saved_row_valid<=cmd_row_valid;saved_row_skews<=cmd_row_skews;
    dw_mode<=cmd_depthwise;dw_pair<=cmd_dw_pair;dw_coeff<=cmd_dw_weights;
    tag<=cmd_tag;watchdog<=timeout_cycles;row<=0;pixels<=cmd_pixels;busy<=1;elapsed_cycles<=0;
   end
   if(!busy && (wf || raw_fault))begin fault<=wf?wf:raw_fault;end
   if(busy)begin
    elapsed_cycles<=elapsed_cycles+1'b1;
    if(command_phase==2 && invalid)begin fault<=8'h90;busy<=0;command_phase<=0;end
    else if(wf || raw_fault || mac_bad || mac_row_fault)begin fault<=wf?wf:(raw_fault?raw_fault:8'h96);busy<=0;command_phase<=0;end
    else if(watchdog!=0 && elapsed_cycles>=watchdog)begin fault<=8'hc0;busy<=0;command_phase<=0;end
    else if(command_phase==1)begin
     // Preserve the original unsigned19-bit K arithmetic, including truncation.
     terms<=dw_mode?{12'd0,(dw_pair?(pixels>>1):pixels)}:channel_kernel*saved_kh;
     command_phase<=2;
    end
    else if(command_phase==2)command_phase<=3;
    else if(command_phase==3)command_phase<=0;
    else if(bank_valid && mac_feed_ready && bank_tag!=accepted_terms)begin fault<=8'h97;busy<=0;end
    else if(out_valid && out_ready)begin
     if(out_last)begin done<=1;busy<=0;end
     else row<=row+1'b1;
    end
   end
  end
 end
endmodule
`default_nettype wire
