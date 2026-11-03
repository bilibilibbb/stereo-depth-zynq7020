`timescale 1ns/1ps
`default_nettype none
// Full layer, CO<=16/K<=32. Independent read and write DMA clients surround
// a raw-row resident MAC; owner tags prohibit overwrite of any live row.
// Weights are read once. Next-row reads, MAC work and complete-row writes
// may overlap. No result is committed as a layer until every DMA is quiet.
module retained_conv_layer_dma_v1 #(
 parameter[31:0]ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000
)(
 input wire clk,rst_n,cancel,clear,cmd_valid,output wire cmd_ready,
 input wire[31:0]source_address,source_bytes,destination_address,destination_bytes,weight_address,weight_bytes,cmd_tag,timeout_cycles,
 input wire[8:0]input_width,input_height,input_channels,output_width,output_height,
 input wire[4:0]output_channels,input wire[2:0]kernel_width,kernel_height,padding_x,padding_y,
 input wire cmd_depthwise,input wire[575:0]dw_weights,input wire[1:0]dilation_shift,
 input wire stride2,replicate,input wire source_wide,source_relu,input wire[5:0]source_fraction,target_fraction,
 input wire[8:0]destination_width,destination_height,destination_channels,channel_start,
 input wire lattice_two,phase_x,phase_y,input wire[8:0]stripe_pixels,
 input wire[767:0]bias,input wire[95:0]shift,input wire[1:0]activation_mode,input wire[5:0]activation_fraction_in,activation_fraction_out,
 output reg done,cancelled,output reg[7:0]fault,output wire busy,quiescent,needs_fabric_reset,
 output wire[7:0]debug_state,output reg[31:0]elapsed_cycles,read_beats,core_feed_cycles,microtiles,overlap_read_compute,overlap_write_compute,
 output wire[31:0]write_values,output wire[8:0]debug_x,debug_y,debug_fetch_y,output wire[4:0]debug_row_valid,
 output wire[379:0]service_request,input wire[451:0]service_response
);
 localparam IDLE=0,INIT=1,WEIGHTS=2,ACTIVE=3,DRAIN=4,FAILED=5,RECOVER=6,PREP0=7,PREP1=8,PREP2=9,VALIDATE=10;
 reg[3:0]state;reg[31:0]src,src_bytes,dst,dst_bytes,wb,wb_bytes,tag,watchdog,drain_cycles;
 reg[8:0]iw,ih,ci,ow,oh,pitch,x,y,fetch_y;reg[4:0]co;
 reg[8:0]stripe_width,stripe_begin,fetch_x;reg striped;reg[15:0]current_stripe,fetch_stripe;
 reg[8:0]dw,dh,dcn,cstart;reg lat,phx,phy;
 reg[2:0]kw,kh,px,py;reg[3:0]ring,fetch_slot;reg[1:0]d2;reg is_dw;reg[575:0]dw_taps;reg st2,rep;
 reg source32,source_activation;reg[5:0]fin,fout,af,ao;reg[1:0]am,chunk;reg[767:0]biases;reg[95:0]shifts;
 reg[14:0]row_span;reg[13:0]fetch_base;reg[11:0]bwords;
 reg[9:0]row_valid;reg[15:0]row_tags[0:9];reg[3:0]row_skew[0:9];reg[1:0]fetch_phase;reg loading,tile_active,compute_done,mac_done_seen,writer_done_seen;
 reg was_cancel,drain_stuck;
 reg[31:0]core_completed_tiles;
 wire stop=cancel || state==DRAIN || state==FAILED;
 wire child_clear=state==RECOVER;
 wire lr,ld,lc,lb,lq,lp;wire[7:0]lf;wire[3:0]ls;wire[31:0]lbeats;
 wire rr,rd,rc,rb,rq,rp,pr;wire[7:0]rf,rs;
 wire cr,cd,cb,cv,cl,corer,raw_ready,raw_valid,weight_ready,weight_valid;wire[7:0]cf;
 wire[63:0]raw_data,weight_data;wire[13:0]raw_address;wire[3:0]raw_mask;
 wire[11:0]weight_index;wire[767:0]sums;wire[6:0]out_row;wire[31:0]out_tag;
 wire[15:0]cterms,ccommitted;
 wire qr,qv,ql,post_ready,pv,pl,pe,pbusy;wire[63:0]qd,pd;wire[31:0]qt,pt;wire[6:0]qy,post_row;wire[2:0]qc;wire[4:0]pc;wire[3:0]qm;
 wire fifo_ready,fifo_valid,fifo_busy,fifo_last;wire[63:0]fifo_data;wire[31:0]fifo_tag;wire[6:0]fifo_row;wire[4:0]fifo_column;
 reg[18:0]in_plane,out_plane;reg[11:0]kernel_channels;
 reg[27:0]in_extent,out_extent;
 reg[15:0]input_k;
 wire[18:0]weight_extent=input_k*32;
 wire input_striped=stripe_width!=0;
 wire[8:0]input_pitch=input_striped?stripe_width:((iw+9'd15)&9'h1f0);
 wire[4:0]nominal_ring=(({2'd0,kh}-5'd1)<<d2)+(st2?5'd2:5'd1)+5'd1;
 wire[3:0]input_ring=input_striped?4'd2:(ih<nominal_ring?ih[3:0]:nominal_ring[3:0]);
 reg[18:0]input_row_span;
 reg[21:0]scratch_span;
 wire[32:0]source_end={1'b0,src}+src_bytes,dest_end={1'b0,dst}+dst_bytes,weights_end={1'b0,wb}+wb_bytes;
 reg[32:0]actual_source_end,actual_dest_end,actual_weight_end;
 wire overlap_sd={1'b0,src}<actual_dest_end && {1'b0,dst}<actual_source_end;
 wire overlap_wd={1'b0,wb}<actual_dest_end && {1'b0,dst}<actual_weight_end;
 reg signed[11:0]wextent,hextent;reg[11:0]expected_w,expected_h;
 wire act_bad=!(am==0 || (am==1 && af==9 && ao==10) ||
  (am==2 && af>=7 && af<=10 && ao==11));
 wire invalid=(input_striped && (stripe_width>iw || stripe_width[3:0]!=0 || kw!=1 || kh!=1 || st2 || px || py || ow!=iw || oh!=ih)) ||
  (is_dw && (iw<4 || iw[1:0] || ih<2 || ih[0] || ci!=co || co>4 || kw!=3 || kh!=3 || rep || lat || input_striped || (st2 && d2!=0) || ow<2 || ow[0] || px!=(1<<d2) || py!=(1<<d2))) ||
  watchdog==0 || iw==0 || iw>320 || ih==0 || ih>256 || ci==0 || ci>384 ||
  ow==0 || ow>320 || oh==0 || oh>256 || co==0 || co>16 ||
  kw==0 || kw>3 || kh==0 || kh>3 || px>(d2==0?2:4) || py>(d2==0?2:4) || (!is_dw && input_k>32) || input_ring==0 || input_ring>10 || scratch_span>(is_dw?4096:16384) ||
  d2>2 || (d2!=0 && (!is_dw || ci!=co || co>4 || kw!=3 || kh!=3 || st2 || rep || lat || input_striped || px!=(1<<d2) || py!=(1<<d2))) ||
  wextent<0 || hextent<0 ||
  (lat?({3'd0,ow}+1<expected_w || {3'd0,ow}>expected_w+1 || {3'd0,oh}+1<expected_h || {3'd0,oh}>expected_h+1):
   ({3'd0,ow}!=expected_w || {3'd0,oh}!=expected_h)) || fin>30 || fout>30 || (source_activation && (fin<9 || fin>16)) || act_bad ||
  dw==0 || dw>320 || dh==0 || dh>256 || dcn==0 || dcn>384 ||
  {1'b0,cstart}+co>{1'b0,dcn} ||
  src[2:0]!=0 || dst[2:0]!=0 || (!is_dw && wb[2:0]!=0) || src_bytes[2:0]!=0 || dst_bytes[2:0]!=0 || (!is_dw && wb_bytes[2:0]!=0) ||
  src<ADDRESS_MIN || dst<ADDRESS_MIN || (!is_dw && wb<ADDRESS_MIN) || source_end>{1'b0,ADDRESS_END} || dest_end>{1'b0,ADDRESS_END} || (!is_dw && weights_end>{1'b0,ADDRESS_END}) ||
  src_bytes<in_extent || dst_bytes<out_extent || (!is_dw && wb_bytes<weight_extent) || overlap_sd || (!is_dw && overlap_wd);
 assign cmd_ready=rst_n && state==IDLE && !cancel && !fault && lr && rr && cr;
 assign busy=state!=IDLE && state!=FAILED;
 assign quiescent=(state==IDLE || state==FAILED) && lq && rq && !cb && !pbusy && !qv && !fifo_busy;
 assign needs_fabric_reset=lp || rp || drain_stuck;
 assign debug_state={ls,state};assign debug_x=x;assign debug_y=y;assign debug_fetch_y=fetch_y;assign debug_row_valid=row_valid[4:0];
 wire start_children=state==INIT && !stop && lr && rr;
 wire dw_pair=is_dw && d2==0 && !st2;
 wire[3:0]fetch_skew=!is_dw?4'd0:
  d2==2?(fetch_phase==0?4'd0:fetch_phase==1?4'd1:4'd2):
  d2==1?(fetch_phase==0?4'd0:fetch_phase==1?4'd1:4'd8):
         (fetch_phase==0?4'd0:fetch_phase==1?4'd4:4'd8);
 reg[55:0]row_offsets;reg[15:0]row_skews;reg[3:0]row_mask;reg rows_ready,old_row_needed;
 integer j,s;reg found;
 reg signed[11:0]first_y_base,prefetch_last;reg geometry_valid;
 reg[8:0]needed_y[0:3];reg[3:0]needed_valid;
 reg signed[11:0]prepare_y[0:3],prepare_prefetch;
 integer gj;
 always @*begin
  prepare_prefetch=first_y_base+(st2?12'sd2:12'sd1)+(($signed({9'd0,kh})-12'sd1)<<<d2);
  if(rep && prepare_prefetch<0)prepare_prefetch=0;
  for(gj=0;gj<4;gj=gj+1)begin
   prepare_y[gj]=first_y_base+(gj<<d2);
   if(rep)begin if(prepare_y[gj]<0)prepare_y[gj]=0;if(prepare_y[gj]>=$signed({3'd0,ih}))prepare_y[gj]=$signed({3'd0,ih})-12'sd1;end
  end
 end
 always @*begin
  row_offsets=0;row_skews=0;row_mask=0;rows_ready=1;old_row_needed=0;found=0;
  if(striped)begin
   rows_ready=0;
   for(s=0;s<10;s=s+1)if(row_valid[s] && row_tags[s]==current_stripe)begin row_offsets[13:0]=s*row_span;row_mask[0]=1;rows_ready=1;end
   old_row_needed=row_valid[fetch_slot] && row_tags[fetch_slot]==current_stripe;
  end else begin
   for(j=0;j<4;j=j+1)begin
    found=0;
    if(needed_valid[j])begin
     for(s=0;s<10;s=s+1)if(row_valid[s] && row_tags[s]=={7'd0,needed_y[j]})begin row_offsets[j*14+:14]=s*row_span;row_skews[j*4+:4]=row_skew[s];found=1;end
     row_mask[j]=found;if(!found)rows_ready=0;
     if(row_valid[fetch_slot] && row_tags[fetch_slot]=={7'd0,needed_y[j]})old_row_needed=1;
    end
   end
  end
 end
 wire[8:0]active_width=striped?(iw-stripe_begin<stripe_width?iw-stripe_begin:stripe_width):iw;
 wire[8:0]fetch_width=striped?(iw-fetch_x<stripe_width?iw-fetch_x:stripe_width):iw;
 wire fetch=state==ACTIVE && geometry_valid && !stop && !loading && fetch_y<ih &&
  (striped?(fetch_stripe<=current_stripe+1'b1):($signed({3'd0,fetch_y})<=prefetch_last)) && !old_row_needed && lr;
 wire launch_core=state==ACTIVE && geometry_valid && !stop && !compute_done && !tile_active && rows_ready && cr;
 localparam DW_BLOCK_PIXELS=64;
 wire[8:0]block_pixels=is_dw?9'd64:9'd8;
 wire[6:0]pixels=ow-x>=block_pixels?block_pixels[6:0]:ow-x;
 wire signed[10:0]first_x=striped?$signed({2'd0,x-stripe_begin}):($signed({2'd0,x})*(st2?2:1)-$signed({8'd0,px}));
 wire[31:0]tile_tag={7'd0,y,7'd0,x};
 wire end_chunk={1'b0,chunk,2'b00}+5'd4>=co;
 assign corer=qr && end_chunk;
 wire retire_core=cv && corer && cl;
 wire[15:0]expected_core_terms=is_dw?{9'd0,(dw_pair?(pixels>>1):pixels)}:{4'd0,bwords[11:2]};
 genvar g;generate for(g=0;g<4;g=g+1)begin:MASK assign qm[g]=chunk*4+g<co;end endgenerate
 raw_span_loader_v1_sdma_v1 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) loader(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.clear(child_clear),.cmd_valid((start_children && !is_dw) || fetch),.cmd_ready(lr),.cmd_weights(state==INIT),.cmd_depthwise(is_dw),
  .source_address(state==INIT?wb:src),.source_bytes(state==INIT?wb_bytes:src_bytes),.cmd_tag(tag),.timeout_cycles(watchdog),
  .source_width(iw),.source_height(ih),.channels(ci),.row_y(fetch_y),.span_x(fetch_x),.span_pixels(fetch_width),.pitch(pitch),.slot_base(fetch_base),.weight_words(bwords),.input_wide(source32),.input_relu(source_activation),.source_fraction(fin),.target_fraction(fout),
  .raw_valid(raw_valid),.raw_ready(raw_ready),.raw_address(raw_address),.raw_data(raw_data),.raw_mask(raw_mask),
  .weight_valid(weight_valid),.weight_ready(weight_ready),.weight_address(weight_index),.weight_data(weight_data),
  .done(ld),.cancelled(lc),.fault(lf),.busy(lb),.quiescent(lq),.needs_fabric_reset(lp),.debug_state(ls),.accepted_beats(lbeats),.written_words(),.saturated_values(),.elapsed_cycles(),
  .service_request(service_request[189:0]),.service_response(service_response[225:0]));
 retained_conv_mac128_v1 core(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.clear(child_clear),.raw_valid(raw_valid),.raw_ready(raw_ready),.raw_address(raw_address),.raw_data(raw_data),.raw_mask(raw_mask),
  .weight_valid(weight_valid),.weight_ready(weight_ready),.weight_clear(start_children),.weight_address(weight_index),.weight_data(weight_data),
  .raw_skew(fetch_skew),.cmd_depthwise(is_dw),.cmd_dw_pair(dw_pair),.cmd_dw_weights(dw_taps),.cmd_row_skews(row_skews),
  .cmd_valid(launch_core),.cmd_ready(cr),.cmd_channels({7'd0,ci}),.cmd_width(active_width),.cmd_pitch(pitch),.cmd_kw(kw),.cmd_kh(kh),.cmd_stride2(st2),.cmd_replicate(rep),.cmd_dilation_shift(d2),
  .cmd_first_x(first_x),.cmd_pixels(pixels),.cmd_row_offsets(row_offsets),.cmd_row_valid(row_mask),.cmd_tag(tile_tag),.timeout_cycles(watchdog),
  .out_valid(cv),.out_ready(corer),.out_data(sums),.out_row(out_row),.out_last(cl),.out_tag(out_tag),.busy(cb),.done(cd),.fault(cf),.elapsed_cycles(),
  .accepted_terms(cterms),.committed_terms(ccommitted),.read_vectors(),.raw_written_words(),.loaded_weight_words());
 requant_row4_dsp_v1 quant(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.in_valid(cv),.in_ready(qr),.in_sum(sums[chunk*192+:192]),.in_bias(biases[chunk*192+:192]),.in_shift(shifts[chunk*24+:24]),.in_mask(qm),.in_int8(1'b0),
  .in_last(cl && end_chunk),.in_tag(out_tag),.in_row(out_row),.in_col_block({1'b0,chunk}),.out_valid(qv),.out_ready(post_ready),.out_data(qd),.out_saturated(),
  .out_last(ql),.out_tag(qt),.out_row(qy),.out_col_block(qc),.accepted_rows(),.emitted_rows(),.saturated_lanes());
 relu_requant4_v1 post(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.in_valid(qv),.in_ready(post_ready),.in_data(qd),.in_mode(am),.in_fraction(af),.out_fraction(ao),.in_tag(qt),.in_row(qy),.in_column({qc,2'b00}),.in_last(ql),
  .out_valid(pv),.out_ready(fifo_ready),.out_data(pd),.out_tag(pt),.out_row(post_row),.out_column(pc),.out_last(pl),.out_saturated(),.out_error(pe),.busy(pbusy));
 // Normalize the writer's eight-pixel ABI before the existing FIFO.
 // This register boundary separates tag addition from writer validation.
 wire[6:0]enqueue_row={4'd0,post_row[2:0]};
 wire[31:0]enqueue_tag={pt[31:16],pt[15:0]+{9'd0,post_row[6:3],3'b000}};
 wire enqueue_last=pl || (post_row[2:0]==3'd7 && {1'b0,pc}+6'd4>={1'b0,co});
 retained_ready_fifo2_v1 #(.WIDTH(109)) output_cut(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.in_valid(pv && !pe),.in_ready(fifo_ready),.in_data({enqueue_tag,enqueue_row,pc,enqueue_last,pd}),
  .out_valid(fifo_valid),.out_ready(pr),.out_data({fifo_tag,fifo_row,fifo_column,fifo_last,fifo_data}),.busy(fifo_busy));
 // Preserve the existing writer's eight-pixel packet ABI. Core/post receipts
 // still belong to the complete64pixel DW block; only writer tags are split.
 wire[6:0]writer_packet_row=fifo_row;
 wire[31:0]writer_packet_tag=fifo_tag;
 wire writer_packet_last=fifo_last;
 row_output_lattice_dma_v2_sdma_v1 #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) writer(
  .clk(clk),.rst_n(rst_n),.cancel(stop),.clear(child_clear),.cmd_valid(start_children),.cmd_ready(rr),.destination_address(dst),.destination_bytes(dst_bytes),.cmd_tag(tag),.timeout_cycles(watchdog),
  .width(ow),.height(oh),.channels(co),.destination_width(dw),.destination_height(dh),.destination_channels(dcn),.channel_start(cstart),.lattice_two(lat),.phase_x(phx),.phase_y(phy),.in_valid(fifo_valid),.in_ready(pr),.in_data(fifo_data),.in_row(writer_packet_row),.in_column(fifo_column),.in_tag(writer_packet_tag),.in_frame(tag),.in_last(writer_packet_last),
  .done(rd),.cancelled(rc),.fault(rf),.busy(rb),.quiescent(rq),.needs_fabric_reset(rp),.debug_state(rs),.accepted_beats(),.committed_values(write_values),.elapsed_cycles(),
  .service_request(service_request[379:190]),.service_response(service_response[451:226]));
 integer r;
 always @(posedge clk)begin
  if(!rst_n)begin
   first_y_base<=0;prefetch_last<=0;geometry_valid<=0;needed_valid<=0;wextent<=0;hextent<=0;expected_w<=0;expected_h<=0;
   for(r=0;r<4;r=r+1)needed_y[r]<=0;
   in_plane<=0;out_plane<=0;kernel_channels<=0;in_extent<=0;out_extent<=0;input_k<=0;input_row_span<=0;scratch_span<=0;actual_source_end<=0;actual_dest_end<=0;actual_weight_end<=0;
   stripe_width<=0;stripe_begin<=0;fetch_x<=0;striped<=0;current_stripe<=0;fetch_stripe<=0;
   dw<=0;dh<=0;dcn<=0;cstart<=0;lat<=0;phx<=0;phy<=0;
   state<=IDLE;src<=0;src_bytes<=0;dst<=0;dst_bytes<=0;wb<=0;wb_bytes<=0;tag<=0;watchdog<=0;drain_cycles<=0;
   iw<=0;ih<=0;ci<=0;ow<=0;oh<=0;pitch<=0;x<=0;y<=0;fetch_y<=0;co<=0;kw<=0;kh<=0;px<=0;py<=0;d2<=0;is_dw<=0;dw_taps<=0;fetch_phase<=0;ring<=0;fetch_slot<=0;st2<=0;rep<=0;
   source32<=0;source_activation<=0;fin<=0;fout<=0;af<=0;ao<=0;am<=0;chunk<=0;biases<=0;shifts<=0;row_span<=0;fetch_base<=0;bwords<=0;row_valid<=0;
   for(r=0;r<10;r=r+1)begin row_tags[r]<=0;row_skew[r]<=0;end
   loading<=0;tile_active<=0;compute_done<=0;mac_done_seen<=0;writer_done_seen<=0;was_cancel<=0;drain_stuck<=0;
   done<=0;cancelled<=0;fault<=0;elapsed_cycles<=0;read_beats<=0;core_feed_cycles<=0;microtiles<=0;core_completed_tiles<=0;overlap_read_compute<=0;overlap_write_compute<=0;
  end else begin
   done<=0;cancelled<=0;
   if(busy)elapsed_cycles<=elapsed_cycles+1'b1;
   if(state==PREP1 || (state==ACTIVE && !geometry_valid))begin
    prefetch_last<=prepare_prefetch;geometry_valid<=1;
    for(r=0;r<4;r=r+1)begin
     needed_y[r]<=prepare_y[r][8:0];needed_valid[r]<=r<kh && prepare_y[r]>=0 && prepare_y[r]<$signed({3'd0,ih});
    end
   end
   if(stop || child_clear)chunk<=0;
   else if(cv && qr)chunk<=end_chunk?0:chunk+1'b1;
   if(cb && raw_valid && raw_ready)overlap_read_compute<=overlap_read_compute+1'b1;
   if(cb && service_response[451])overlap_write_compute<=overlap_write_compute+1'b1;
   if(cancel && state!=DRAIN && state!=FAILED)begin state<=DRAIN;was_cancel<=1;drain_cycles<=0;row_valid<=0;end
   else if((state==INIT || state==WEIGHTS || state==ACTIVE) && elapsed_cycles>=watchdog)begin fault<=8'hc0;state<=DRAIN;drain_cycles<=0;row_valid<=0;end
   else if((state==INIT || state==WEIGHTS || state==ACTIVE || state>=PREP0) && (lf || rf || cf || pe))begin
    fault<=lf?lf:(rf?rf:(cf?cf:8'hb2));state<=DRAIN;drain_cycles<=0;row_valid<=0;
   end else case(state)
    IDLE:if(cmd_valid && cmd_ready)begin
     stripe_width<=stripe_pixels;striped<=stripe_pixels!=0;stripe_begin<=0;fetch_x<=0;current_stripe<=0;fetch_stripe<=0;
     dw<=destination_width;dh<=destination_height;dcn<=destination_channels;cstart<=channel_start;lat<=lattice_two;phx<=phase_x;phy<=phase_y;
     src<=source_address;src_bytes<=source_bytes;dst<=destination_address;dst_bytes<=destination_bytes;wb<=weight_address;wb_bytes<=weight_bytes;tag<=cmd_tag;watchdog<=timeout_cycles;
     iw<=input_width;ih<=input_height;ci<=input_channels;ow<=output_width;oh<=output_height;co<=output_channels;
     kw<=kernel_width;kh<=kernel_height;px<=padding_x;py<=padding_y;d2<=dilation_shift;is_dw<=cmd_depthwise;dw_taps<=dw_weights;fetch_phase<=0;st2<=stride2;rep<=replicate;
     source32<=source_wide;source_activation<=source_relu;fin<=source_fraction;fout<=target_fraction;af<=activation_fraction_in;ao<=activation_fraction_out;am<=activation_mode;biases<=bias;shifts<=shift;
     fetch_base<=0;fetch_slot<=0;fetch_y<=0;x<=0;y<=0;row_valid<=0;chunk<=0;
     loading<=0;tile_active<=0;compute_done<=0;mac_done_seen<=0;writer_done_seen<=0;was_cancel<=0;drain_stuck<=0;drain_cycles<=0;
     elapsed_cycles<=0;read_beats<=0;core_feed_cycles<=0;microtiles<=0;core_completed_tiles<=0;overlap_read_compute<=0;overlap_write_compute<=0;
     state<=PREP0;
    end
    PREP0:begin
     first_y_base<=-$signed({9'd0,py});geometry_valid<=0;
     wextent<=$signed({3'd0,iw})+px*2-((($signed({9'd0,kw})-1)<<<d2)+1);hextent<=$signed({3'd0,ih})+py*2-((($signed({9'd0,kh})-1)<<<d2)+1);
     in_plane<=iw*ih*(source32?4:2);out_plane<=dw*dh*2;kernel_channels<=ci*kw;
     pitch<=input_pitch;ring<=input_ring;state<=PREP1;
    end
    PREP1:begin
     expected_w<=(wextent>>(st2?1:0))+1'b1;expected_h<=(hextent>>(st2?1:0))+1'b1;
     in_extent<=in_plane*ci;out_extent<=out_plane*dcn;input_k<=kernel_channels*kh;
     input_row_span<=is_dw?pitch:ci*pitch;state<=PREP2;
    end
    PREP2:begin
     scratch_span<=input_row_span*ring;row_span<=input_row_span[14:0];bwords<=input_k*4;
     actual_source_end<={1'b0,src}+in_extent;actual_dest_end<={1'b0,dst}+out_extent;actual_weight_end<={1'b0,wb}+weight_extent;
     state<=VALIDATE;
    end
    VALIDATE:begin
     if(invalid)begin fault<=8'h90;state<=FAILED;end else state<=INIT;
    end
    INIT:if(start_children)state<=is_dw?ACTIVE:WEIGHTS;
    WEIGHTS:if(ld)begin read_beats<=read_beats+lbeats;state<=ACTIVE;end
    ACTIVE:begin
     if(fetch)begin loading<=1;row_valid[fetch_slot]<=0;end
     if(ld && loading)begin
      loading<=0;row_valid[fetch_slot]<=1;row_tags[fetch_slot]<=striped?fetch_stripe:{7'd0,fetch_y};row_skew[fetch_slot]<=fetch_skew;fetch_phase<=fetch_phase==2?0:fetch_phase+1'b1;read_beats<=read_beats+lbeats;
      if(striped)begin
       fetch_stripe<=fetch_stripe+1'b1;
       if(fetch_x+stripe_width>=iw)begin fetch_x<=0;fetch_y<=fetch_y+1'b1;end else fetch_x<=fetch_x+stripe_width;
      end else fetch_y<=fetch_y+1'b1;
      if(fetch_slot+1==ring)begin fetch_slot<=0;fetch_base<=0;end
      else begin fetch_slot<=fetch_slot+1'b1;fetch_base<=fetch_base+row_span;end
     end
     if(launch_core)begin tile_active<=1;mac_done_seen<=0;end
     if(retire_core)begin mac_done_seen<=1;core_feed_cycles<=core_feed_cycles+cterms;core_completed_tiles<=core_completed_tiles+1'b1;
      if(cterms!=expected_core_terms || ccommitted!=cterms)begin fault<=8'hb3;state<=DRAIN;drain_cycles<=0;row_valid<=0;end
     end
     // The requantizer latches sums and the complete tile identity.
     // Retire the core when its LAST row is accepted, then begin the next
     // core tile while the previous tagged post/write pipeline drains.
     // Final frame completion still requires the writer's full receipt and
     // every arithmetic/output FIFO to be empty. Cancellation still drains.
     if(pv && pr && pl)begin
      if(microtiles>=core_completed_tiles && !retire_core)begin fault<=8'hb4;state<=DRAIN;drain_cycles<=0;row_valid<=0;end
      else microtiles<=microtiles+1'b1;
     end
     if(retire_core && cterms==expected_core_terms && ccommitted==cterms)begin
      if(!tile_active)begin fault<=8'hb4;state<=DRAIN;drain_cycles<=0;row_valid<=0;end
      else begin
       tile_active<=0;
       if(striped && x+block_pixels>=stripe_begin+active_width)begin
        current_stripe<=current_stripe+1'b1;
        if(stripe_begin+active_width==iw)stripe_begin<=0;else stripe_begin<=stripe_begin+stripe_width;
       end
       if(x+block_pixels>=ow)begin x<=0;if(y+1==oh)compute_done<=1;else begin y<=y+1'b1;first_y_base<=first_y_base+(st2?12'sd2:12'sd1);geometry_valid<=0;end end
       else x<=x+block_pixels;
      end
     end
     if(rd)writer_done_seen<=1;
     if(compute_done && microtiles==core_completed_tiles && (writer_done_seen || rd) && !lb && lq && rq && !cb && !pbusy && !qv && !fifo_busy)begin done<=1;state<=IDLE;end
    end
    DRAIN:begin
     if(drain_cycles!=32'hffffffff)drain_cycles<=drain_cycles+1'b1;
     if(drain_cycles>3 && lq && rq && !cb && !pbusy && !qv && !fifo_busy)begin state<=FAILED;cancelled<=was_cancel;end
     else if(drain_cycles>=(watchdog<32?32:watchdog))drain_stuck<=1;
    end
    FAILED:if(clear && !cancel && lq && rq && !needs_fabric_reset)state<=RECOVER;
    // Raw-loader ready includes the external DMA grant. During a fault
    // in ANOTHER engine this client deliberately has no grant. Recovery
    // waits for drained, fault-free child state, never for bus ownership.
    // child_clear is asserted throughout RECOVER, including its final edge.
    RECOVER:if(lq && rq && !lb && !rb && !lf && !rf && cr)begin fault<=0;row_valid<=0;loading<=0;tile_active<=0;state<=IDLE;end
    default:begin fault<=8'h9f;state<=DRAIN;drain_cycles<=0;row_valid<=0;end
   endcase
  end
 end
 // synthesis translate_off
 always @(posedge clk)if(rst_n && !cancel && cmd_valid && cmd_ready && fifo_busy)
  $fatal(1,"NN command changed while output FIFO remained owned");
 // synthesis translate_on
endmodule
`default_nettype wire
