`timescale 1ns/1ps
`default_nettype none
// Full-K address stream for an8-output-pixel microtile. Raw row offsets are
// supplied by the row owner, one per kernel row, and are already in pixels.
// Layout within a cached row is [input_channel][pitch >= input_width].
// Polyphase lowering has removed transposed zeros before this interface.
module direct_row_window_v1(
 input wire clk,rst_n,cancel,clear,cmd_valid,output wire cmd_ready,
 input wire[15:0]cmd_channels,input wire[8:0]cmd_width,cmd_pitch,
 input wire cmd_depthwise,cmd_dw_pair,input wire[15:0]cmd_row_skews,
 input wire[2:0]cmd_kw,cmd_kh,input wire[1:0]cmd_dilation_shift,input wire cmd_stride2,cmd_replicate,
 input wire signed[10:0]cmd_first_x,input wire[6:0]cmd_pixels,
 input wire[55:0]cmd_row_offsets,input wire[3:0]cmd_row_valid,
 input wire[31:0]cmd_tag,
 output wire out_valid,input wire out_ready,output wire[167:0]out_addresses,
 output wire[11:0]out_mask,output wire[9:0]out_k,output wire out_last,output wire[31:0]out_tag,
 output reg done,output reg[7:0]fault,output reg[31:0]accepted_vectors
);
 reg active;reg[1:0]preparing;reg scalar_bad;reg[3:0]extent_bad_q;reg[18:0]terms_q,channel_kernel;reg[24:0]channel_extent_q;reg[15:0]channels,channel;reg[8:0]width,pitch;reg[2:0]kw,kh,kx,ky;
 reg dw_mode,dw_pair;reg[15:0]skews;reg[1:0]d2;reg stride2,replicate;reg signed[10:0]first_x;reg[6:0]pixels;reg[3:0]row_valid;
 reg[55:0]offsets;reg[13:0]channel_offset;reg[9:0]k,total;reg[31:0]tag;
 wire scalar_invalid=cmd_channels==0 || cmd_channels>384 || cmd_width==0 || cmd_width>320 || cmd_pitch<cmd_width || cmd_pitch>320 || cmd_pitch[3:0]!=0 ||
  cmd_kw==0 || cmd_kw>4 || cmd_kh==0 || cmd_kh>4 || cmd_pixels==0 || cmd_pixels>(cmd_depthwise?64:8) || cmd_first_x< -4 || cmd_first_x>640 || cmd_dilation_shift>2 || (cmd_depthwise && (cmd_channels>4 || cmd_kw!=3 || cmd_kh!=3));
 wire[13:0]row_base=offsets[ky*14+:14];
 wire[11:0]address_bad,calc_mask;wire[167:0]calc_addresses;
 reg packet_valid,packet_last;reg[167:0]packet_addresses;
 reg[11:0]packet_mask;reg[9:0]packet_k;reg[31:0]packet_tag;
 wire calc_take=active && !fault && !cancel && (!packet_valid || out_ready);
 wire calc_last=k+1==total;
 assign cmd_ready=rst_n && !active && preparing==0 && !packet_valid && !fault && !cancel;
 assign out_valid=rst_n && packet_valid && !fault && !cancel;
 assign out_addresses=packet_addresses;assign out_mask=packet_mask;
 assign out_tag=packet_tag;assign out_k=packet_k;assign out_last=out_valid && packet_last;
 genvar l,r;
 generate for(r=0;r<4;r=r+1)begin:EXTENT
  always @(posedge clk)begin
   if(!rst_n || cancel)extent_bad_q[r]<=0;
   else if(preparing==1)extent_bad_q[r]<=row_valid[r] && r<kh && ({11'd0,offsets[r*14+:14]}+channel_extent_q)>(dw_mode?4096:16384);
  end
 end endgenerate

 // DW uses four shared horizontal coordinates for all three kernel rows.
 // Fixed shifts replace inferred multiplication by conditional1/2 factors.
 wire[11:0]dw_step=(dw_pair && stride2)?{k,2'b00}:
                    (dw_pair || stride2)?{1'b0,k,1'b0}:{2'b0,k};
 wire signed[11:0]dw_origin=$signed({first_x[10],first_x})+$signed(dw_step);
 wire signed[11:0]normal_origin=$signed({first_x[10],first_x})+$signed({9'd0,kx}<<d2);
 wire signed[11:0]dw_position[0:3],normal_position[0:7];
 wire[8:0]dw_clamp[0:3],normal_clamp[0:7];
 wire[3:0]dw_inside;wire[7:0]normal_inside;
 wire[8:0]last_x=width-1'b1;
 wire[15:0]normal_base={2'd0,row_base}+{2'd0,channel_offset};
 wire[15:0]dw_base[0:2];
 generate for(r=0;r<3;r=r+1)begin:ROW_BASE
  assign dw_base[r]={2'd0,offsets[r*14+:14]}+{2'd0,channel_offset};
 end
 for(l=0;l<4;l=l+1)begin:DW_COORD
  localparam signed[11:0]COLUMN=l;
  assign dw_position[l]=dw_origin+(COLUMN<<<d2);
  assign dw_inside[l]=dw_position[l]>=0 && dw_position[l]<$signed({3'd0,width});
  assign dw_clamp[l]=dw_position[l]<0?9'd0:(dw_position[l]>=$signed({3'd0,width})?last_x:dw_position[l][8:0]);
 end
 for(l=0;l<8;l=l+1)begin:NORMAL_COORD
  localparam signed[11:0]COLUMN=l,TWO_COLUMN=2*l;
  assign normal_position[l]=normal_origin+(stride2?TWO_COLUMN:COLUMN);
  assign normal_inside[l]=normal_position[l]>=0 && normal_position[l]<$signed({3'd0,width});
  assign normal_clamp[l]=normal_position[l]<0?9'd0:(normal_position[l]>=$signed({3'd0,width})?last_x:normal_position[l][8:0]);
 end
 for(l=0;l<12;l=l+1)begin:LANE
  wire[8:0]clamped;
  wire[15:0]base=dw_mode?dw_base[l/4]:normal_base;
  if(l<8)begin
   assign clamped=dw_mode?dw_clamp[l%4]:normal_clamp[l];
   assign calc_mask[l]=dw_mode?(row_valid[l/4] && (l%4<3 || dw_pair) && dw_inside[l%4]):
    (row_valid[ky] && l<pixels && (replicate || normal_inside[l]));
  end else begin
   assign clamped=dw_clamp[l%4];
   assign calc_mask[l]=dw_mode && row_valid[l/4] && (l%4<3 || dw_pair) && dw_inside[l%4];
  end
  wire[15:0]address=base+{7'd0,clamped};
  wire[3:0]bank_skew=dw_mode?skews[(l/4)*4+:4]:skews[ky*4+:4];
  assign address_bad[l]=calc_mask[l] && address>=(dw_mode?4096:16384);
  assign calc_addresses[l*14+:14]=dw_mode?{address[11:4],2'b00,address[3:0]^bank_skew}:{address[13:4],address[3:0]^bank_skew};
 end endgenerate
 always @(posedge clk)begin
  if(!rst_n || cancel)begin
   packet_valid<=0;packet_last<=0;packet_addresses<=0;packet_mask<=0;packet_k<=0;packet_tag<=0;
   preparing<=0;scalar_bad<=0;terms_q<=0;channel_kernel<=0;channel_extent_q<=0;
   active<=0;channels<=0;channel<=0;width<=0;pitch<=0;kw<=0;kh<=0;kx<=0;ky<=0;d2<=0;dw_mode<=0;dw_pair<=0;skews<=0;stride2<=0;replicate<=0;first_x<=0;pixels<=0;row_valid<=0;
   offsets<=0;channel_offset<=0;k<=0;total<=0;tag<=0;done<=0;fault<=0;accepted_vectors<=0;
  end else begin
   done<=0;
   if(clear && !active && preparing==0)begin fault<=0;if(fault!=0)packet_valid<=0;end
   if(cmd_valid && cmd_ready)begin
    begin
     preparing<=1;scalar_bad<=scalar_invalid;channel_kernel<=cmd_channels*cmd_kw;dw_mode<=cmd_depthwise;dw_pair<=cmd_dw_pair;skews<=cmd_row_skews;channel_extent_q<=cmd_depthwise?cmd_pitch:cmd_channels*cmd_pitch;
     active<=0;channels<=cmd_channels;channel<=0;width<=cmd_width;pitch<=cmd_pitch;kw<=cmd_kw;kh<=cmd_kh;kx<=0;ky<=0;
     stride2<=cmd_stride2;replicate<=cmd_replicate;d2<=cmd_dilation_shift;first_x<=cmd_first_x;pixels<=cmd_pixels;row_valid<=cmd_row_valid;
     offsets<=cmd_row_offsets;channel_offset<=0;k<=0;total<=0;tag<=cmd_tag;accepted_vectors<=0;
    end
   end
   if(preparing==1)begin terms_q<=dw_mode?(dw_pair?(pixels>>1):pixels):channel_kernel*kh;preparing<=2;end
   else if(preparing==2)begin
    preparing<=0;
    if(scalar_bad || |extent_bad_q || terms_q>768)fault<=8'h90;
    else begin total<=terms_q[9:0];active<=1;end
   end
   if(out_valid && out_ready)begin
    packet_valid<=0;accepted_vectors<=accepted_vectors+1'b1;
    if(packet_last)done<=1;
   end
   if(calc_take)begin
    packet_valid<=1;packet_addresses<=calc_addresses;packet_mask<=calc_mask;
    packet_k<=k;packet_last<=calc_last;packet_tag<=tag;
    if(calc_last)active<=0;
    else begin
     k<=k+1'b1;
     if(dw_mode)begin end
     else if(kx+1==kw)begin kx<=0;
      if(ky+1==kh)begin ky<=0;channel<=channel+1'b1;channel_offset<=channel_offset+pitch;end
      else ky<=ky+1'b1;
     end else kx<=kx+1'b1;
    end
   end
   // Admission checks the entire row extent before active can rise.
   // Counter bounds and clamp<width<=pitch make address_bad unreachable.
   // Keep that invariant executable in simulation, off the hardware feedback.
   // synthesis translate_off
   if(active && |address_bad)$fatal(1,"Admitted window address invariant failed");
   // synthesis translate_on
  end
 end
endmodule
`default_nettype wire
