`timescale 1ns/1ps
`default_nettype none
// Single-beat AXI adapter for the two existing INTEGER fabric clients.
// Capture AW and W independently; publish B only after fabric drain.
// Cancellation is handled by the producer: every offered transaction drains.
module r14_context_service_bridge_v221 #(
 parameter [31:0] ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000
)(
 input wire clk,rst_n,
 input wire [31:0] araddr,input wire arvalid,output wire arready,
 output wire [63:0] rdata,output wire [1:0] rresp,
 output wire rvalid,input wire rready,
 input wire [31:0] awaddr,input wire awvalid,output wire awready,
 input wire [63:0] wdata,input wire [7:0] wstrb,input wire wvalid,output wire wready,
 output wire [1:0] bresp,output wire bvalid,input wire bready,
 output wire quiescent,needs_fabric_reset,
 output wire [379:0] service_request,input wire [451:0] service_response
);
 wire [112:0] rr=service_response[112:0];
 // Each client receives read113+write113.
 wire [112:0] ww=service_response[451:339];
 reg read_owned,read_seen,read_error;
 reg aw_have,w_have,write_owned,write_sent,write_reply;
 reg [31:0] write_address;
 reg [63:0] write_data;
 reg [7:0] write_strobes;
 reg [1:0] write_response;
 wire read_offer=arvalid && !read_owned;
 wire write_offer=aw_have && w_have && !write_owned && !write_reply;
 wire read_fault=rr[69+:8]!=0;
 assign arready=!read_owned && rr[0];
 assign rvalid=read_owned && !read_seen && (rr[2] || read_error);
 assign rdata=rr[3+:64];
 assign rresp=read_error || read_fault?2'b10:2'b00;
 assign awready=!aw_have && !write_owned && !write_reply;
 assign wready=!w_have && !write_owned && !write_reply;
 assign bvalid=write_reply;assign bresp=write_response;
 assign quiescent=!read_owned && !aw_have && !w_have && !write_owned && !write_reply && rr[77] && ww[77];
 assign needs_fabric_reset=(read_owned && read_fault && !rr[77]) ||
                          (write_owned && ww[69+:8]!=0 && !ww[77]);
 assign service_request[189:0]={1'b0,ADDRESS_END,ADDRESS_MIN,
      (read_owned && !read_seen && rready),1'b0,8'd0,64'd0,16'd1,
      araddr,1'b0,read_offer,1'b0};
 assign service_request[379:190]={1'b0,ADDRESS_END,ADDRESS_MIN,
      1'b0,(write_owned && !write_sent),write_strobes,write_data,16'd1,
      write_address,1'b1,write_offer,1'b0};
 always @(posedge clk)begin
  if(!rst_n)begin
   read_owned<=0;read_seen<=0;read_error<=0;
   aw_have<=0;w_have<=0;write_owned<=0;write_sent<=0;write_reply<=0;
   write_address<=0;write_data<=0;write_strobes<=0;write_response<=0;
  end else begin
   if(read_offer && arready)begin read_owned<=1;read_seen<=0;read_error<=0;end
   if(rvalid && rready)begin read_seen<=1;read_error<=0;end
   // A rejected/error read has no payload. Deliver one error response only
   // after the real backend is quiet, preserving producer ownership.
   if(read_owned && rr[67] && rr[77])begin
    if(!read_seen && !(rvalid && rready))begin
     if(read_fault)read_error<=1;
    end else begin read_owned<=0;read_seen<=0;read_error<=0;end
   end
   // The backend's done is a pulse. A synthesized error response can be
   // consumed one cycle later; backend quiescence still proves release.
   if(read_owned && read_error && rready && rr[77])begin read_owned<=0;read_seen<=0;read_error<=0;end
   if(awvalid && awready)begin aw_have<=1;write_address<=awaddr;end
   if(wvalid && wready)begin w_have<=1;write_data<=wdata;write_strobes<=wstrb;end
   if(write_offer && ww[0])begin write_owned<=1;write_sent<=0;end
   if(write_owned && !write_sent && ww[1])write_sent<=1;
   if(write_owned && ww[67] && ww[77])begin
    write_owned<=0;write_reply<=1;write_response<=ww[69+:8]!=0?2'b10:2'b00;
   end
   if(write_reply && bready)begin
    aw_have<=0;w_have<=0;write_sent<=0;write_reply<=0;write_response<=0;
   end
  end
 end
endmodule
`default_nettype wire
