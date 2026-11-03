`timescale 1ns/1ps
`default_nettype none
// Retained frozen11459 SGM register ABI; original descriptor/status logic.
// The original two clients share the system fabric; no private DMA backend.
module fw_sgm_mmio_rtl #(parameter[31:0] ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000,BUILD_ID=32'h53534731)(
input wire clk,rst_n,
input wire[11:0]s_axi_awaddr,input wire s_axi_awvalid,output wire s_axi_awready,
input wire[31:0]s_axi_wdata,input wire[3:0]s_axi_wstrb,input wire s_axi_wvalid,output wire s_axi_wready,
output reg[1:0]s_axi_bresp,output reg s_axi_bvalid,input wire s_axi_bready,
input wire[11:0]s_axi_araddr,input wire s_axi_arvalid,output wire s_axi_arready,
output reg[31:0]s_axi_rdata,output reg[1:0]s_axi_rresp,output reg s_axi_rvalid,input wire s_axi_rready,
output wire owner_active,bus_poisoned,
output wire[379:0]service_request,input wire[451:0]service_response,
input wire[1:0]service_poisoned
);
reg[31:0]descriptor[0:5];reg aw_hold,w_hold;reg[11:0]aw_address;reg[31:0]w_data;reg[3:0]w_strobe;
reg command,cancel,clear,done_sticky,cancel_sticky;reg[31:0]accepted_tag,completed_tag,accepted_count,completed_count,control_errors;
wire ready,done,cancelled,busy,quiescent,needs_reset,engine_needs_reset;wire[15:0]fault;wire[31:0]debug_state;
wire[31:0]elapsed_cycles,read_beats,write_values,curve_values,write_beats,accepted_pixels,native_committed,curve_committed,prefix_tag;
// Successful DONE is a retained receipt, not a live DMA lease. Cancel/error
// remains owned until actual quiet CLEAR, and poisoned buses require reset.
assign needs_reset=engine_needs_reset || |service_poisoned;
assign bus_poisoned=needs_reset;
assign owner_active=command || cancel || clear || busy || !quiescent || fault!=0 || cancel_sticky || needs_reset;
assign s_axi_awready=rst_n && !aw_hold && !s_axi_bvalid;
assign s_axi_wready=rst_n && !w_hold && !s_axi_bvalid;
assign s_axi_arready=rst_n && !s_axi_rvalid;
wire editable=ready && !busy && quiescent && !command && !cancel && !clear;
function[31:0]merge_strobes;
 input[31:0]old_value,new_value;input[3:0]strobes;integer lane;
 begin for(lane=0;lane<4;lane=lane+1)merge_strobes[lane*8+:8]=strobes[lane]?new_value[lane*8+:8]:old_value[lane*8+:8];end
endfunction
integer i,index;
always @(posedge clk)begin
 if(!rst_n)begin
  aw_hold<=0;w_hold<=0;aw_address<=0;w_data<=0;w_strobe<=0;
  s_axi_bresp<=0;s_axi_bvalid<=0;s_axi_rdata<=0;s_axi_rresp<=0;s_axi_rvalid<=0;
  command<=0;cancel<=0;clear<=0;done_sticky<=0;cancel_sticky<=0;accepted_tag<=0;completed_tag<=0;accepted_count<=0;completed_count<=0;control_errors<=0;
  for(i=0;i<6;i=i+1)descriptor[i]<=0;
 end else begin
  command<=0;cancel<=0;clear<=0;
  if(done)begin done_sticky<=1;completed_tag<=accepted_tag;completed_count<=completed_count+1'b1;end
  if(cancelled)cancel_sticky<=1;
  if(s_axi_bvalid && s_axi_bready)s_axi_bvalid<=0;
  if(s_axi_rvalid && s_axi_rready)s_axi_rvalid<=0;
  if(s_axi_awvalid && s_axi_awready)begin aw_hold<=1;aw_address<=s_axi_awaddr;end
  if(s_axi_wvalid && s_axi_wready)begin w_hold<=1;w_data<=s_axi_wdata;w_strobe<=s_axi_wstrb;end
  if(aw_hold && w_hold && !s_axi_bvalid)begin
   aw_hold<=0;w_hold<=0;s_axi_bvalid<=1;s_axi_bresp<=0;
   if(aw_address[1:0]!=0)begin s_axi_bresp<=2;control_errors<=control_errors+1'b1;end
   else if(aw_address>=12'h100 && aw_address<12'h118)begin
    if(editable)descriptor[(aw_address-12'h100)>>2]<=merge_strobes(descriptor[(aw_address-12'h100)>>2],w_data,w_strobe);
    else begin s_axi_bresp<=2;control_errors<=control_errors+1'b1;end
   end else if(aw_address==12'h010 && w_strobe==4'hf)begin
    if(w_data==1 && editable)begin command<=1;accepted_tag<=descriptor[4];accepted_count<=accepted_count+1'b1;done_sticky<=0;cancel_sticky<=0;end
    else if(w_data==2 && busy && !command)cancel<=1;
    else if(w_data==4 && quiescent && !busy && !needs_reset && !command)begin clear<=1;done_sticky<=0;cancel_sticky<=0;end
    else begin s_axi_bresp<=2;control_errors<=control_errors+1'b1;end
   end else begin s_axi_bresp<=2;control_errors<=control_errors+1'b1;end
  end
  if(s_axi_arvalid && s_axi_arready)begin
   s_axi_rvalid<=1;s_axi_rresp<=0;s_axi_rdata<=0;
   if(s_axi_araddr[1:0]!=0)s_axi_rresp<=2;
   else if(s_axi_araddr>=12'h100 && s_axi_araddr<12'h118)s_axi_rdata<=descriptor[(s_axi_araddr-12'h100)>>2];
   else case(s_axi_araddr)
    12'h000:s_axi_rdata<=32'h524f3134;
    12'h004:s_axi_rdata<=32'h00010000;
    12'h008:s_axi_rdata<=BUILD_ID;
    12'h00c:s_axi_rdata<=32'hed2076d1;
    12'h010:s_axi_rdata<=0;
    12'h014:s_axi_rdata<={fault,10'd0,needs_reset,cancel_sticky,done_sticky,ready,quiescent,busy};
    12'h018:s_axi_rdata<=accepted_tag;
    12'h01c:s_axi_rdata<=completed_tag;
    12'h020:s_axi_rdata<=write_values;
    12'h024:s_axi_rdata<=elapsed_cycles;
    12'h028:s_axi_rdata<=read_beats;
    12'h02c:s_axi_rdata<=debug_state;
    12'h030:s_axi_rdata<=accepted_count;
    12'h034:s_axi_rdata<=completed_count;
    12'h038:s_axi_rdata<=control_errors;
    12'h03c:s_axi_rdata<=32'hd14cebbd;
    12'h040:s_axi_rdata<=curve_values;
    12'h044:s_axi_rdata<=write_beats;
    12'h048:s_axi_rdata<=accepted_pixels;
    12'h04c:s_axi_rdata<=native_committed;
    12'h050:s_axi_rdata<=curve_committed;
    12'h054:s_axi_rdata<=prefix_tag;
    12'h058:s_axi_rdata<=32'h53504731;
    default:s_axi_rresp<=2;
   endcase
  end
 end
end
fw_sgm_raw_dma_rtl #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) engine(
.clk(clk),
.rst_n(rst_n),
.cancel(cancel),
.clear(clear),
.cmd_valid(command),
.cmd_ready(ready),
.left_address(descriptor[0]),
.right_address(descriptor[1]),
.curve_address(descriptor[2]),
.native_address(descriptor[3]),
.cmd_tag(descriptor[4]),
.timeout_cycles(descriptor[5]),
.done(done),
.cancelled(cancelled),
.fault(fault),
.busy(busy),
.quiescent(quiescent),
.needs_fabric_reset(engine_needs_reset),
.debug_state(debug_state),
.elapsed_cycles(elapsed_cycles),
.read_beats(read_beats),
.write_beats(write_beats),
.accepted_pixels(accepted_pixels),
.native_values(write_values),
.curve_values(curve_values),
.native_committed(native_committed),
.curve_committed(curve_committed),
.prefix_tag(prefix_tag),
.service_request(service_request),
.service_response(service_response));
endmodule
`default_nettype wire
