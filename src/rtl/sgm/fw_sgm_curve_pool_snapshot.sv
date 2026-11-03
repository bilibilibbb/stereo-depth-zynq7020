`timescale 1ns/1ps
`default_nettype none
// Exact triangular17-anchor/4x4 pool derived from the immutable serial_v2.
// Adjacent groups share one carried anchor, so each anchor is stored once.
// One pending update overlaps the next synchronous read; each RAM still has
// one read port and one write port. No read-during-write forwarding assumption.
(* use_dsp="no" *) module fw_sgm_curve_pool_rtl #(parameter WIDTH=320,HEIGHT=240)(
 input wire clk,rst_n,cancel,clear,cmd_valid,output wire cmd_ready,
 input wire[31:0]cmd_tag,timeout_cycles,
 input wire in_valid,output wire in_ready,input wire[63:0]in_cost,
 input wire[15:0]in_x,in_y,input wire[3:0]in_group,input wire[31:0]in_tag,input wire in_last,
 output wire out_valid,input wire out_ready,output reg[271:0]out_num,out_mass,
 output reg[15:0]out_x,out_y,output wire[31:0]out_tag,output wire out_last,
 output reg done,cancelled,output reg[7:0]fault,output wire busy,output wire[3:0]debug_state,
 output reg[31:0]accepted_words,emitted_cells,elapsed_cycles
);
 localparam COLS=WIDTH/4,ROWS=HEIGHT/4+4;
 localparam IDLE=0,PAD=1,ACCUM=2,FLUSH=3,LOAD_CELL=4,CELL_STREAM=5,EMIT=6,FAILED=15;
 reg[3:0]state,group;reg[15:0]x,y;reg[31:0]tag,watchdog;
 wire[24:0]even_q,odd_q;reg[15:0]delta_lo,delta_hi;reg[4:0]mass_lo,mass_hi;
 reg[9:0]addr_even,addr_odd;reg pending,pending_first;reg[3:0]pending_group;
 reg[24:0]carry_anchor;
 reg[4:0]anchor_issue,anchor_return;reg cell_pending;
 wire stop=cancel || fault!=0;
 wire take=in_valid&&in_ready;
 wire first_pixel=(x[1:0]==0 && y[1:0]==0);
 wire[15:0]read_even=(group[0]?((group+1)>>1):(group>>1))*COLS+(x>>2);
 wire[15:0]read_odd=(group>>1)*COLS+(x>>2);
 wire[15:0]cell_address=(anchor_issue>>1)*COLS+(x>>2);
 wire tag_bad=in_x!=x || in_y!=y || in_group!=group || in_tag!=tag || in_last!=(x==WIDTH-1 && y==HEIGHT-1 && group==15);
 reg[15:0]nlo,nhi;reg[4:0]mlo,mhi;reg lane_bad;integer k;
 reg[15:0]v;reg[2:0]wl,wh;
 always @* begin
  nlo=0;nhi=0;mlo=0;mhi=0;lane_bad=0;v=0;wl=0;wh=0;
  for(k=0;k<4;k=k+1)begin
   v=in_cost[k*16+:16];wl=4-k;wh=k;
   if(v!=1020)begin
    if(v>192 || group*4+k>x)lane_bad=1;
    nlo=nlo+v*wl;nhi=nhi+v*wh;mlo=mlo+wl;mhi=mhi+wh;
   end
  end
 end
 // Only group0 needs the old low anchor from RAM. For all later groups,
 // the previous high contribution is the same anchor, already in carry.
 wire[24:0]base_lo=pending_group==0?(pending_first?25'd0:even_q):carry_anchor;
 wire[24:0]base_hi=pending_first?25'd0:(pending_group[0]?even_q:odd_q);
 wire[16:0]new_num_lo={1'b0,base_lo[15:0]}+{1'b0,delta_lo};
 wire[16:0]new_num_hi={1'b0,base_hi[15:0]}+{1'b0,delta_hi};
 wire[9:0]new_mass_lo={1'b0,base_lo[24:16]}+mass_lo;
 wire[9:0]new_mass_hi={1'b0,base_hi[24:16]}+mass_hi;
 wire overflow=new_num_lo>49152 || new_num_hi>49152 || new_mass_lo>256 || new_mass_hi>256;
 wire[24:0]word_lo={new_mass_lo[8:0],new_num_lo[15:0]},word_hi={new_mass_hi[8:0],new_num_hi[15:0]};
 wire[24:0]cell_word=anchor_return[0]?odd_q:even_q;
 assign cmd_ready=rst_n && state==IDLE && !stop;
 // Keep the overflow detector local to the accumulator commit/fault.
 // On its fault edge one offered input may handshake and is discarded;
 // no RAM write or output publication commits, and FAILED blocks further
 // input. The parent drains/quarantines the failed frame. Legal inputs never
 // exceed num49152/mass256, so their cycle/handshake sequence is unchanged.
 assign in_ready=rst_n && state==ACCUM && !stop;
 assign out_valid=rst_n && (state==PAD || state==EMIT) && !stop;
 assign out_tag=tag;assign out_last=out_x==COLS-1 && out_y==ROWS-1;
 assign busy=state!=IDLE && state!=FAILED;assign debug_state=state;
 wire valid_input=take && !tag_bad && !lane_bad;
 wire cell_issue=(state==LOAD_CELL || state==CELL_STREAM) && anchor_issue<17 && !stop;
 // First pixel initializes the whole4x4 cell, without reading stale RAM.
 // Thereafter group0 reads two anchors, later groups only the new high one.
 wire even_read=(valid_input && !first_pixel && (group==0 || group[0])) || (cell_issue && !anchor_issue[0]);
 wire odd_read=(valid_input && !first_pixel && !group[0]) || (cell_issue && anchor_issue[0]);
 wire[9:0]even_address=cell_issue?cell_address[9:0]:read_even[9:0];
 wire[9:0]odd_address=cell_issue?cell_address[9:0]:read_odd[9:0];
 wire write_row=pending && !stop && !overflow;
 wire even_write=write_row && (!pending_group[0] || pending_group==15);
 wire odd_write=write_row && pending_group[0];
 sgm_row_ram_v1 #(.DATA_BITS(25),.DEPTH(9*COLS),.ADDR_BITS(10)) even_bank(.clk(clk),.read_enable(even_read),.read_address(even_address),.read_data(even_q),.write_enable(even_write),.write_address(addr_even),.write_data(pending_group[0]?word_hi:word_lo));
 sgm_row_ram_v1 #(.DATA_BITS(25),.DEPTH(8*COLS),.ADDR_BITS(10)) odd_bank(.clk(clk),.read_enable(odd_read),.read_address(odd_address),.read_data(odd_q),.write_enable(odd_write),.write_address(addr_odd),.write_data(word_lo));
 always @(posedge clk)begin
  if(!rst_n || cancel)begin
   state<=IDLE;group<=0;x<=0;y<=0;tag<=0;watchdog<=0;delta_lo<=0;delta_hi<=0;mass_lo<=0;mass_hi<=0;
   addr_even<=0;addr_odd<=0;pending<=0;pending_first<=0;pending_group<=0;carry_anchor<=0;
   anchor_issue<=0;anchor_return<=0;cell_pending<=0;
   out_num<=0;out_mass<=0;out_x<=0;out_y<=0;done<=0;cancelled<=cancel;fault<=0;accepted_words<=0;emitted_cells<=0;elapsed_cycles<=0;
  end else begin
   done<=0;cancelled<=0;if(busy)elapsed_cycles<=elapsed_cycles+1'b1;
   if(busy && elapsed_cycles>=watchdog)begin fault<=8'hc0;state<=FAILED;pending<=0;cell_pending<=0;end
   else if(pending && overflow)begin fault<=8'h93;state<=FAILED;pending<=0;cell_pending<=0;end
   else if(take && (tag_bad || lane_bad))begin fault<=tag_bad?8'h91:8'h92;state<=FAILED;pending<=0;end
   else begin
    pending<=valid_input;cell_pending<=cell_issue;
    if(pending)carry_anchor<=word_hi;
    if(valid_input)begin
     delta_lo<=nlo;delta_hi<=nhi;mass_lo<=mlo;mass_hi<=mhi;
     addr_even<=read_even[9:0];addr_odd<=read_odd[9:0];pending_first<=first_pixel;pending_group<=group;
     accepted_words<=accepted_words+1'b1;
     if(group==15)begin group<=0;state<=FLUSH;end else group<=group+1'b1;
    end
    if(cell_issue)begin anchor_return<=anchor_issue;anchor_issue<=anchor_issue+1'b1;end
    if(cell_pending)begin
     out_num<={cell_word[15:0],out_num[271:16]};out_mass<={7'd0,cell_word[24:16],out_mass[271:16]};
     if(anchor_return==16)state<=EMIT;
    end
    case(state)
     IDLE:if(cmd_valid && cmd_ready)begin
      if(timeout_cycles==0)begin fault<=8'h90;state<=FAILED;end
      else begin
       x<=0;y<=0;group<=0;tag<=cmd_tag;watchdog<=timeout_cycles;out_x<=0;out_y<=0;out_num<=0;out_mass<=0;
       pending<=0;cell_pending<=0;carry_anchor<=0;accepted_words<=0;emitted_cells<=0;elapsed_cycles<=0;state<=PAD;
      end
     end
     PAD:if(out_ready)begin
      emitted_cells<=emitted_cells+1'b1;
      if(out_x==COLS-1)begin out_x<=0;if(out_y==3)state<=ACCUM;else out_y<=out_y+1'b1;end
      else out_x<=out_x+1'b1;
     end
     FLUSH:if(pending)begin
      // Finish group15 before another pixel or any cell read is admitted.
      if(x[1:0]==3 && y[1:0]==3)begin anchor_issue<=0;anchor_return<=0;cell_pending<=0;out_num<=0;out_mass<=0;out_x<=x>>2;out_y<=(y>>2)+4;state<=LOAD_CELL;end
      else begin if(x==WIDTH-1)begin x<=0;y<=y+1'b1;end else x<=x+1'b1;state<=ACCUM;end
     end
     LOAD_CELL:state<=CELL_STREAM;
     EMIT:if(out_ready)begin
      emitted_cells<=emitted_cells+1'b1;
      if(out_last)begin done<=1;state<=IDLE;end
      else begin if(x==WIDTH-1)begin x<=0;y<=y+1'b1;end else x<=x+1'b1;state<=ACCUM;end
     end
     FAILED:if(clear)begin fault<=0;pending<=0;cell_pending<=0;state<=IDLE;end
     default:begin end
    endcase
   end
  end
 end
 // synthesis translate_off
 initial if(WIDTH<4 || HEIGHT<4 || WIDTH%4 || HEIGHT%4 || WIDTH>320 || HEIGHT>240)$fatal(1,"Unsupported pool dimensions");
 always @(posedge clk)if(rst_n && !stop)begin
  if(even_read && even_write && even_address==addr_even)$fatal(1,"Pool even RAM read/write collision");
  if(odd_read && odd_write && odd_address==addr_odd)$fatal(1,"Pool odd RAM read/write collision");
 end
 // synthesis translate_on
endmodule
`default_nettype wire
