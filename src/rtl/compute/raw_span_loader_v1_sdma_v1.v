`timescale 1ns/1ps
`default_nettype none
// Load a horizontal NCHW span from every channel; plane/row pitches remain
// those of the whole source. Handle arbitrary pixel alignment and 1..320
// pixels. Whole64-bit physical reads are repacked into aligned raw SRAM words
// with exact tail masks. Padding never becomes a valid input pixel.
module raw_span_loader_v1_sdma_v1 #(
 parameter[31:0]ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000
)(
 input wire clk,rst_n,cancel,clear,cmd_valid,output wire cmd_ready,input wire cmd_weights,
 input wire[31:0]source_address,source_bytes,cmd_tag,timeout_cycles,
 input wire[8:0]source_width,source_height,channels,row_y,span_x,span_pixels,pitch,input wire[13:0]slot_base,
 input wire input_wide,input_relu,input wire[11:0]weight_words,input wire[5:0]source_fraction,target_fraction,
 input wire cmd_depthwise,output wire raw_valid,input wire raw_ready,output wire[13:0]raw_address,output wire[63:0]raw_data,output wire[3:0]raw_mask,
 output wire weight_valid,input wire weight_ready,output wire[11:0]weight_address,output wire[63:0]weight_data,
 output reg done,cancelled,output reg[7:0]fault,output wire busy,quiescent,needs_fabric_reset,
 output wire[3:0]debug_state,output reg[31:0]accepted_beats,written_words,saturated_values,elapsed_cycles,
 output wire[189:0]service_request,input wire[225:0]service_response
);
 localparam IDLE=0,ISSUE=1,READ=2,FLUSH=3,RETIRE=4,NEXT=5,FINISH=6,DRAIN=7,FAILED=8,PRODUCTS=9,ADDRESS=10,ROUND_END=11,CHECK=12;
 reg[3:0]state;reg weights,depthwise;reg[31:0]tag,watchdog,pointer,drain_cycles;
 reg[18:0]plane_bytes;reg[8:0]cn,channel,cpitch,length;reg[13:0]local_base;
 reg[11:0]wcount,index;reg[6:0]produced,written,out_words;reg[5:0]fin,fout;
 reg was_cancel,drain_stuck,dma_inflight,have_previous,pack_valid;
 reg[127:0]previous,pack;reg[1:0]have_count;reg wide,activation;reg[3:0]pack_mask;
 reg quant_valid;reg[63:0]quant_data;reg[13:0]quant_address;reg[3:0]quant_mask;
 reg[2:0]quant_sat;reg[6:0]convert_index;
 wire[63:0]converted_data;wire conversion_configured,conversion_ready,conversion_valid,conversion_busy;wire[3:0]converted_mask;
 wire stop=cancel || fault!=0 || state==DRAIN || state==FAILED;
 wire convert_take=pack_valid && conversion_ready && !weights &&
  (state==READ || state==FLUSH || state==RETIRE) && conversion_configured && !stop;
 wire dr,dd,dc,dq,dp,dv;wire[7:0]df;wire[31:0]dt;wire[63:0]data;
 reg command_bad;
 reg[31:0]source_base,allocation_bytes;
 reg[32:0]allocation_limit,last_address,rounded_limit;
 reg[27:0]tensor_extent,last_channel_offset;
 reg[18:0]row_offset;
 reg[10:0]span_begin_bytes;
 reg[11:0]span_end_bytes;
 wire[18:0]plane=(source_width*source_height)<<(input_wide?2:1);
 wire[32:0]allocation_end={1'b0,source_address}+{1'b0,source_bytes};
 wire[24:0]scratch_end={11'd0,slot_base}+(cmd_depthwise?pitch:channels*pitch);
 wire scalar_invalid=timeout_cycles==0 || source_address[2:0]!=0 || source_address<ADDRESS_MIN || source_bytes[2:0]!=0 ||
  allocation_end[32] || allocation_end>{1'b0,ADDRESS_END} ||
  (cmd_weights?(weight_words==0 || weight_words>3072 || {17'd0,weight_words,3'd0}>source_bytes):
   (source_width==0 || source_width>320 || source_height==0 || source_height>256 || channels==0 || channels>384 || row_y>=source_height ||
    span_pixels==0 || {1'b0,span_x}+{1'b0,span_pixels}>{1'b0,source_width} || pitch<span_pixels || pitch>320 || pitch[3:0]!=0 || slot_base[3:0]!=0 ||
    scratch_end>(cmd_depthwise?4096:16384) || (cmd_depthwise && channels>4) || source_fraction>30 || target_fraction>30 || (input_relu && (source_fraction<9 || source_fraction>16))));
 assign cmd_ready=rst_n && state==IDLE && !stop && dq;
 assign busy=state!=IDLE && state!=FAILED;
 assign quiescent=dq && (state==IDLE || state==FAILED);
 assign needs_fabric_reset=drain_stuck || df==4 || df==5;
 assign debug_state=state;
 assign raw_valid=rst_n && (state==READ || state==FLUSH || state==RETIRE) && !weights && quant_valid && !stop;
 assign weight_valid=rst_n && state==READ && weights && dv && !stop;
 assign raw_address=quant_address;assign raw_mask=quant_mask;assign raw_data=quant_data;
 assign weight_address=index;assign weight_data=data;
 wire raw_take=raw_valid && raw_ready;
 wire pack_ready=!pack_valid || convert_take;
 wire ready=weights?weight_ready:pack_ready;
 wire accept=dv && ready && state==READ && !stop;
 wire[1:0]offset=pointer[2:1];wire[11:0]read_bytes=({3'd0,length}<<(wide?2:1))+{9'd0,pointer[2:0]}+12'd7;
 wire[11:0]words=weights?wcount:(read_bytes>>3);
 wire[127:0]joined16={data,previous[127:64]};wire[191:0]joined32={data,previous};
 wire[3:0]next_mask=produced+1==out_words && length[1:0]!=0?((4'd1<<length[1:0])-1'b1):4'b1111;
 wire[3:0]sat;
 wire signed[7:0]input_scale=$signed({2'b0,fout})-$signed({2'b0,fin});
 wire[13:0]next_local_address=local_base+{5'd0,convert_index,2'b00};
 wire scale_take=conversion_valid && (!quant_valid || raw_ready) && !stop;
 r14_input_rescale32_pipe_v7 #(.LANES(4)) input_scale_unit(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.configure(state==PRODUCTS && !stop),.scale(input_scale),
  .relu(activation),.source_fraction(fin),.configured(conversion_configured),.in_valid(pack_valid && !weights && (state==READ || state==FLUSH || state==RETIRE) && !stop),
  .in_ready(conversion_ready),.out_valid(conversion_valid),.out_ready(!quant_valid || raw_ready),.busy(conversion_busy),
  .in_data(pack),.in_mask(pack_mask),.out_data(converted_data),.out_mask(converted_mask),.saturated(sat));
 function[127:0]extend16;input[63:0]value;integer lane;begin
  for(lane=0;lane<4;lane=lane+1)extend16[lane*32+:32]={{16{value[lane*16+15]}},value[lane*16+:16]};
 end endfunction
 wire[2:0]sat_count={2'd0,sat[0]}+{2'd0,sat[1]}+{2'd0,sat[2]}+{2'd0,sat[3]};
 shared_dma_client_v2 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) reader(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.cmd_valid(state==ISSUE && !stop),.cmd_ready(dr),.cmd_write(1'b0),.cmd_address({pointer[31:3],3'b000}),.cmd_beats({4'd0,words}),
  .wr_strb(8'hff),.wr_data(64'd0),.wr_valid(1'b0),.wr_ready(),.rd_data(data),.rd_valid(dv),.rd_ready(state==READ && ready && !stop),
  .done(dd),.cancelled(dc),.fault(df),.quiescent(dq),.progress(dp),.transferred(dt),
  .m_axi_araddr(),.m_axi_arlen(),.m_axi_arsize(),.m_axi_arburst(),.m_axi_arvalid(),.m_axi_arready(1'b0),.m_axi_rdata(64'd0),.m_axi_rresp(2'd0),.m_axi_rlast(1'b0),.m_axi_rvalid(1'b0),.m_axi_rready(),
  .m_axi_awaddr(),.m_axi_awlen(),.m_axi_awsize(),.m_axi_awburst(),.m_axi_awvalid(),.m_axi_awready(1'b0),.m_axi_wdata(),.m_axi_wstrb(),.m_axi_wlast(),.m_axi_wvalid(),.m_axi_wready(1'b0),.m_axi_bresp(2'd0),.m_axi_bvalid(1'b0),.m_axi_bready(),
  .service_request(service_request),.service_response(service_response));
 always @(posedge clk)begin
  if(!rst_n)begin
   quant_valid<=0;quant_data<=0;quant_address<=0;quant_mask<=0;quant_sat<=0;convert_index<=0;
   command_bad<=0;source_base<=0;allocation_bytes<=0;allocation_limit<=0;last_address<=0;rounded_limit<=0;tensor_extent<=0;last_channel_offset<=0;row_offset<=0;span_begin_bytes<=0;span_end_bytes<=0;
   state<=IDLE;weights<=0;depthwise<=0;tag<=0;watchdog<=0;pointer<=0;drain_cycles<=0;plane_bytes<=0;cn<=0;channel<=0;cpitch<=0;length<=0;local_base<=0;wcount<=0;index<=0;fin<=0;fout<=0;
   produced<=0;written<=0;out_words<=0;have_previous<=0;have_count<=0;wide<=0;activation<=0;pack_valid<=0;previous<=0;pack<=0;pack_mask<=0;
   was_cancel<=0;drain_stuck<=0;dma_inflight<=0;done<=0;cancelled<=0;fault<=0;accepted_beats<=0;written_words<=0;saturated_values<=0;elapsed_cycles<=0;
  end else begin
   done<=0;cancelled<=0;if(busy)elapsed_cycles<=elapsed_cycles+1'b1;
   if(stop)begin pack_valid<=0;quant_valid<=0;end
   if(raw_take)begin quant_valid<=0;written<=written+1'b1;written_words<=written_words+1'b1;saturated_values<=saturated_values+quant_sat;end
   if(convert_take)pack_valid<=0;
   if(scale_take)begin
    quant_valid<=1;quant_data<=converted_data;quant_mask<=converted_mask;quant_sat<=sat_count;
    quant_address<=depthwise?{next_local_address[11:4],channel[1:0],next_local_address[3:0]}:next_local_address;convert_index<=convert_index+1'b1;
   end
   if(accept)begin
    index<=index+1'b1;accepted_beats<=accepted_beats+1'b1;
    if(!weights)begin
     previous<={data,previous[127:64]};have_previous<=1;
     if(!wide)begin
      if(offset==0 || have_previous)begin pack<=extend16(offset==0?data:(joined16>>(offset*16)));pack_mask<=next_mask;pack_valid<=1;produced<=produced+1'b1;end
     end else begin
      if((!pointer[2] && have_count==1) || (pointer[2] && have_count==2))begin
       pack<=pointer[2]?(joined32>>32):{data,previous[127:64]};
       pack_mask<=next_mask;pack_valid<=1;produced<=produced+1'b1;have_count<=pointer[2]?1:0;
      end else have_count<=have_count+1'b1;
     end
    end
   end
   if(cancel && state!=DRAIN && state!=FAILED)begin state<=DRAIN;was_cancel<=1;drain_cycles<=0;end
   else if(busy && state<=FINISH && elapsed_cycles>=watchdog)begin fault<=8'hc0;state<=DRAIN;drain_cycles<=0;end
   else if(busy && state!=DRAIN && dma_inflight && df)begin fault<=8'ha0|df;state<=DRAIN;drain_cycles<=0;end
   else case(state)
    IDLE:if(cmd_valid && cmd_ready)begin
     weights<=cmd_weights;depthwise<=cmd_depthwise && !cmd_weights;tag<=cmd_tag;watchdog<=timeout_cycles;pointer<=source_address;plane_bytes<=plane;
     command_bad<=scalar_invalid;source_base<=source_address;allocation_bytes<=source_bytes;allocation_limit<=allocation_end;
     row_offset<=(row_y*source_width)<<(input_wide?2:1);span_begin_bytes<={2'd0,span_x}<<(input_wide?2:1);span_end_bytes<=({3'd0,span_x}+{3'd0,span_pixels})<<(input_wide?2:1);
     cn<=cmd_weights?9'd1:channels;channel<=0;cpitch<=pitch;length<=span_pixels;local_base<=slot_base;wcount<=weight_words;index<=0;fin<=source_fraction;fout<=target_fraction;
     wide<=input_wide && !cmd_weights;activation<=input_relu && !cmd_weights;have_count<=0;quant_valid<=0;convert_index<=0;out_words<=(span_pixels+9'd3)>>2;produced<=0;written<=0;have_previous<=0;pack_valid<=0;
     was_cancel<=0;drain_stuck<=0;drain_cycles<=0;dma_inflight<=0;elapsed_cycles<=0;accepted_beats<=0;written_words<=0;saturated_values<=0;
     state<=PRODUCTS;
    end
    PRODUCTS:begin
     tensor_extent<=plane_bytes*cn;
     last_channel_offset<=plane_bytes*(cn-9'd1);
     state<=ADDRESS;
    end
    ADDRESS:begin
     pointer<=weights?source_base:source_base+row_offset+span_begin_bytes;
     last_address<={1'b0,source_base}+{5'd0,last_channel_offset}+{14'd0,row_offset}+{21'd0,span_end_bytes};
     state<=ROUND_END;
    end
    ROUND_END:begin rounded_limit<=(last_address+33'd7)&33'h1fffffff8;state<=CHECK;end
    CHECK:begin
     if(command_bad || (!weights && (tensor_extent>allocation_bytes || rounded_limit>allocation_limit)))begin fault<=8'h90;state<=FAILED;end
     else state<=ISSUE;
    end
    ISSUE:if(dr)begin dma_inflight<=1;state<=READ;end
    READ:if(dd)begin
     dma_inflight<=0;
     if(dc || dt!={20'd0,words} || index!=words)begin fault<=8'hb0;state<=DRAIN;drain_cycles<=0;end
     else state<=weights?FINISH:FLUSH;
    end
    FLUSH:begin
     if(produced==out_words)state<=RETIRE;
     else if(pack_ready)begin pack<=wide?(!pointer[2]?{64'd0,previous[127:64]}:(have_count==1?(previous[127:64]>>32):(previous>>32))):extend16(previous[127:64]>>(offset*16));pack_mask<=next_mask;pack_valid<=1;produced<=produced+1'b1;state<=RETIRE;end
    end
    RETIRE:if(!pack_valid && !quant_valid && !conversion_busy)begin
     if(written!=out_words)begin fault<=8'hb1;state<=DRAIN;drain_cycles<=0;end
     else if(channel+1==cn)state<=FINISH;
     else begin have_count<=0;convert_index<=0;channel<=channel+1'b1;pointer<=pointer+plane_bytes;local_base<=depthwise?local_base:local_base+cpitch;index<=0;produced<=0;written<=0;have_previous<=0;state<=NEXT;end
    end
    NEXT:if(dq && dr)state<=ISSUE;
    FINISH:if(dq && dr)begin done<=1;state<=IDLE;end
    DRAIN:begin
     if(drain_cycles!=32'hffffffff)drain_cycles<=drain_cycles+1'b1;
     if(drain_cycles>1 && dq)begin state<=FAILED;cancelled<=was_cancel;dma_inflight<=0;end
     else if(drain_cycles>=(watchdog<32?32:watchdog))drain_stuck<=1;
    end
    FAILED:if(clear && dq && !needs_fabric_reset && !cancel)begin fault<=0;state<=IDLE;end
    default:begin fault<=8'h9f;state<=DRAIN;drain_cycles<=0;end
   endcase
  end
 end
endmodule
`default_nettype wire
