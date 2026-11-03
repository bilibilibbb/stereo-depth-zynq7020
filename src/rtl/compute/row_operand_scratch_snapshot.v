`timescale 1ns/1ps
`default_nettype none
// Six coherent copies of the original 32KiB operand store. Each copy
// provides two synchronous read ports for a fixed pair of lanes. The four
// subbanks replace each lane's old sixteen-way, 64bit output crossbar.
// Read vectors keep the original one-cycle latency and twelve-lane width.
// A simultaneous legal write is backpressured while a read issues. The
// production loader and gatherer use separate epochs; integration must
// verify that this does not add service cycles. Original illegal-bank and
// read/write-collision faults are retained, even where the new RAM could
// otherwise serve the request. Payload and RAM output registers are not reset.
module row_operand_scratch_dw4_v145(
 input wire clk,rst_n,cancel,clear,
 input wire wr_valid,output wire wr_ready,input wire[13:0]wr_pixel_address,
 input wire[63:0]wr_data,input wire[3:0]wr_mask,input wire[3:0]wr_skew,
 input wire rd_valid,output wire rd_ready,input wire[167:0]rd_pixel_addresses,
 input wire[11:0]rd_mask,input wire[31:0]rd_tag,
 input wire rd_depthwise,input wire[2:0]rd_groups,
 output wire out_valid,input wire out_ready,output wire[767:0]out_data,
 output wire[31:0]out_tag,output reg[7:0]fault,
 output reg[31:0]written_words,read_vectors
);
 wire[13:0]pixel[0:11];wire[3:0]selected_bank[0:11];
 wire[11:0]bank_conflict,collision;
 wire[63:0]even_q[0:23],odd_q[0:23];
 wire bad_write=wr_valid && wr_pixel_address[1:0]!=0;
 wire bad_groups=rd_depthwise && (rd_groups==0 || rd_groups>4);
 reg valid,packet_dw;reg[2:0]packet_groups;reg[31:0]tag;reg[11:0]mask;
 reg[1:0]bank_for_lane[0:11],group_for_lane[0:11];
 wire advance=!valid || out_ready;
 wire issue=rd_valid && rd_ready,write_take=wr_valid && wr_ready;
 // wr_valid deliberately preserves the original collision fault, even
 // though the write port is now backpressured during issue.
 wire bad_access=bad_write || (issue && (bad_groups || |bank_conflict)) ||
                 (issue && wr_valid && |collision);
 assign rd_ready=rst_n && !cancel && !fault && advance;
 assign wr_ready=rst_n && !cancel && !fault && !issue;
 assign out_valid=rst_n && valid && !cancel && !fault;assign out_tag=tag;
 genvar b,l,g,j,c;
 generate for(l=0;l<12;l=l+1)begin:LANE
  assign pixel[l]=rd_pixel_addresses[l*14+:14];
  assign selected_bank[l]=pixel[l][3:0];
  wire[11:0]pair_conflict;
  for(j=0;j<12;j=j+1)begin:PAIR
   if(j<l)assign pair_conflict[j]=rd_mask[l] && rd_mask[j] &&
     selected_bank[l]==selected_bank[j] && pixel[l][13:6]!=pixel[j][13:6];
   else assign pair_conflict[j]=1'b0;
  end
  assign bank_conflict[l]=|pair_conflict;
  assign collision[l]=rd_mask[l] && pixel[l][13:6]==wr_pixel_address[13:6] &&
   (rd_depthwise?({1'b0,wr_pixel_address[5:4]}<rd_groups):pixel[l][5:4]==wr_pixel_address[5:4]) &&
   ((selected_bank[l]^wr_skew)>>2)==wr_pixel_address[3:2] && wr_mask[(selected_bank[l]^wr_skew)&3];
  always @(posedge clk)if(issue && !bad_access)begin
   bank_for_lane[l]<=pixel[l][1:0];group_for_lane[l]<=pixel[l][5:4];
  end
  wire[63:0]word_for_lane;
  if((l%2)==0)assign word_for_lane=even_q[(l/2)*4+bank_for_lane[l]];
  else assign word_for_lane=odd_q[(l/2)*4+bank_for_lane[l]];
  for(g=0;g<4;g=g+1)begin:CHANNEL
   wire[15:0]datum=packet_dw?word_for_lane[g*16+:16]:
                                     word_for_lane[group_for_lane[l]*16+:16];
   assign out_data[(g*12+l)*16+:16]=mask[l] && (packet_dw?g<packet_groups:g==0)?datum:16'd0;
  end
 end
 for(b=0;b<4;b=b+1)begin:BANK
  wire[1:0]virtual_lane=b^wr_skew[1:0];
  wire[9:0]write_address={wr_pixel_address[13:6],(wr_pixel_address[3:2]^wr_skew[3:2])};
  wire[15:0]write_data=wr_data[virtual_lane*16+:16];
  wire write_enable=write_take && !bad_access && wr_mask[virtual_lane];
  for(c=0;c<6;c=c+1)begin:COPY
   wire[63:0]qa,qb;
   wire[9:0]read_address_a={pixel[c*2][13:6],pixel[c*2][3:2]};
   wire[9:0]address_b={pixel[c*2+1][13:6],pixel[c*2+1][3:2]};
   wire read_a=issue && !bad_access && rd_mask[c*2] && pixel[c*2][1:0]==b;
   wire read_b=issue && !bad_access && rd_mask[c*2+1] && pixel[c*2+1][1:0]==b;
   // Fixed sixteen-bit memories give each group one complete-word writer.
   // PortA has one explicit address and NO_CHANGE output on its writes.
   // This avoids Vivado2018.3's unsupported partial-write TDP template.
   for(g=0;g<4;g=g+1)begin:GROUP
    (* ram_style="block" *)reg[15:0]memory[0:1023];reg[15:0]ga,gb;
    wire writing=write_enable && wr_pixel_address[5:4]==g;
    // Enabled writes imply write_take; enabled reads imply !write_take.
    // Keep fault-qualified enables, but remove their cone from RAM addresses.
    wire[9:0]address_a=write_take?write_address:read_address_a;
    always @(posedge clk)if(writing || read_a)begin
     if(writing)memory[address_a]<=write_data;
     else ga<=memory[address_a];
    end
    always @(posedge clk)if(read_b)gb<=memory[address_b];
    assign qa[g*16+:16]=ga;assign qb[g*16+:16]=gb;
   end
   assign even_q[c*4+b]=qa;assign odd_q[c*4+b]=qb;
  end
  // Preserve the numerical bench's original payload-width assertion.
  wire[63:0]q=COPY[0].qa;
 end endgenerate
 always @(posedge clk)begin
  if(!rst_n || cancel)begin
   valid<=0;tag<=0;mask<=0;packet_dw<=0;packet_groups<=0;
   fault<=0;written_words<=0;read_vectors<=0;
  end else if(clear)begin valid<=0;fault<=0;end
  else if(!fault)begin
   if(bad_access)begin
    fault<=bad_write?8'h91:(bad_groups?8'h90:(|bank_conflict?8'h92:8'h93));valid<=0;
   end else begin
    if(advance)begin
     valid<=issue;
     if(issue)begin mask<=rd_mask;tag<=rd_tag;packet_dw<=rd_depthwise;
      packet_groups<=rd_groups;read_vectors<=read_vectors+1'b1;end
    end
    if(write_take)written_words<=written_words+1'b1;
   end
  end
 end
endmodule
`default_nettype wire
