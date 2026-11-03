`timescale 1ns/1ps
`default_nettype none
// One standard DMA backend, with command ownership retained through drain/poison.
module retained_compute_dma_side_rr_v2 #(parameter CLIENTS=1,WRITE=0,INDEX_BITS=(CLIENTS<=1?1:$clog2(CLIENTS)),
 parameter[31:0]ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000)(
 input wire clk,rst_n,input wire[CLIENTS*190-1:0]request,input wire[CLIENTS-1:0]permit,
 output wire[CLIENTS*113-1:0]response,output wire backend_quiescent,
    output wire [31:0] m_axi_araddr,output wire [7:0] m_axi_arlen,
    output wire [2:0] m_axi_arsize,output wire [1:0] m_axi_arburst,
    output wire m_axi_arvalid,input wire m_axi_arready,
    input wire [63:0] m_axi_rdata,input wire [1:0] m_axi_rresp,
    input wire m_axi_rlast,input wire m_axi_rvalid,output wire m_axi_rready,
    output wire [31:0] m_axi_awaddr,output wire [7:0] m_axi_awlen,
    output wire [2:0] m_axi_awsize,output wire [1:0] m_axi_awburst,
    output wire m_axi_awvalid,input wire m_axi_awready,
    output wire [63:0] m_axi_wdata,output wire [7:0] m_axi_wstrb,
    output wire m_axi_wlast,output wire m_axi_wvalid,input wire m_axi_wready,
    input wire [1:0] m_axi_bresp,input wire m_axi_bvalid,output wire m_axi_bready

);
 reg active,rejected,early_cancel;reg[1:0]pending;reg[31:0]command_address,command_min,command_limit;reg[15:0]command_beats;reg command_line;reg[32:0]command_end;reg[INDEX_BITS-1:0]owner,selected;reg grant_valid;reg[CLIENTS-1:0]registered_grant;wire found;integer c;reg[INDEX_BITS-1:0]next_owner,candidate;
 wire[189:0]requests[0:CLIENTS-1];wire[189:0]chosen=requests[selected],owned=requests[owner];
 wire backend_ready,backend_done,backend_cancelled,backend_progress,backend_wr_ready,backend_rd_valid;
 wire[7:0]backend_fault;wire[31:0]backend_transferred;wire[63:0]backend_rd_data;
 wire backend_idle;wire launched=active && pending==0 && !rejected && !early_cancel;
 assign backend_quiescent=backend_idle && !active && !grant_valid;
 wire invalid_range=command_address<command_min || command_end>{1'b0,command_limit} || command_end[32] ||
  (command_line && (WRITE!=0 || command_beats!=8 || command_address[5:0]!=0));
 wire[CLIENTS-1:0]eligible,after_owner,priority_set,grant;
 wire accept=rst_n && !active && grant_valid && backend_ready && (|(eligible & registered_grant));
 // A fixed eligible vector avoids eight variable-index request muxes.
 // Select the first eligible client at/after next_owner, or wrap to zero.
 // Register selection before ready returns. Cancellation/permit still mask a reserved grant immediately.
 wire[INDEX_BITS-1:0]encoded;
 genvar e,b,v;
 generate for(e=0;e<CLIENTS;e=e+1)begin:ELIGIBLE
  assign eligible[e]=requests[e][1] && requests[e][2]==(WRITE!=0) && !requests[e][0] && permit[e];
  assign after_owner[e]=eligible[e] && next_owner<=e;
  if(e==0)assign grant[e]=priority_set[e];
  else assign grant[e]=priority_set[e] && !(|priority_set[e-1:0]);
 end
 for(b=0;b<INDEX_BITS;b=b+1)begin:ENCODE
  wire[CLIENTS-1:0]votes;
  for(v=0;v<CLIENTS;v=v+1)begin:VOTE
   assign votes[v]=grant[v] && ((v>>b)&1);
  end
  assign encoded[b]=|votes;
 end endgenerate
 assign priority_set=(|after_owner)?after_owner:eligible;
 assign found=|eligible;
 // Legacy clients probe ready in NEXT/FINISH while their own valid is0.
 // This local preflight can never accept a command: valid commands require
 // the registered onehot grant. No global eligible/priority path returns ready.
 genvar g;
 generate for(g=0;g<CLIENTS;g=g+1)begin:CLIENT
  reg[7:0]last_fault;reg[12:0]last_transferred;
  wire mine=active && owner==g;
  wire[7:0]current_fault=mine?(rejected?8'd1:(launched?backend_fault:8'd0)):last_fault;
  wire[31:0]current_count=mine?(launched?backend_transferred:32'd0):{19'd0,last_transferred};
  assign requests[g]=request[g*190+:190];
  assign response[g*113+0]=rst_n && !active && backend_ready && !requests[g][0] && permit[g] && requests[g][2]==(WRITE!=0) && ((grant_valid && registered_grant[g]) || (!grant_valid && !requests[g][1]));
  assign response[g*113+1]=mine && launched && backend_wr_ready;
  // A cancelled owner never sees a return, on the very same cycle.
  // Keeping this local avoids cancel -> global owner mux -> shared RD_VALID
  // -> native configuration counter -> source-loader state feedback.
  assign response[g*113+2]=mine && launched && !requests[g][0] && backend_rd_valid;
  assign response[g*113+3+:64]=backend_rd_data;
  // synthesis translate_off
  wire baseline_return=mine && launched &&
       (backend.state==1 || backend.state==2 || backend.state==8) &&
       backend.read_count!=0 && !backend.stop && backend.fault==0;
  always @(posedge clk)if(rst_n && response[g*113+2]!==baseline_return)
   $fatal(1,"Local owner return cancellation differs from original shared gate");
  // synthesis translate_on
  assign response[g*113+67]=mine && (rejected || early_cancel || (launched && backend_done));
  assign response[g*113+68]=mine && (early_cancel || (launched && backend_cancelled));
  assign response[g*113+69+:8]=current_fault;
  assign response[g*113+77]=!mine || rejected || early_cancel || (launched && backend_idle);
  assign response[g*113+78]=mine && launched && backend_progress;
  assign response[g*113+79+:32]=current_count;
  assign response[g*113+111]=mine && launched && m_axi_rvalid && m_axi_rready;
  assign response[g*113+112]=mine && launched && m_axi_wvalid && m_axi_wready;
  always @(posedge clk)begin
   if(!rst_n)begin last_fault<=0;last_transferred<=0;end
   else if(accept && selected==g)begin last_fault<=0;last_transferred<=0;end
   else if(mine)begin last_fault<=current_fault;last_transferred<=current_count[12:0];end
  end
 end endgenerate
 always @(posedge clk)begin
  if(!rst_n)begin grant_valid<=0;registered_grant<=0;selected<=0;next_owner<=0;active<=0;rejected<=0;early_cancel<=0;owner<=0;pending<=0;command_address<=0;command_min<=0;command_limit<=0;command_beats<=0;command_line<=0;command_end<=0;end
  else if(active)begin
   grant_valid<=0;
   if(rejected || early_cancel || (launched && backend_done && backend_idle))begin active<=0;rejected<=0;early_cancel<=0;end
   else if(pending!=0 && owned[0])begin early_cancel<=1;pending<=0;end
   else case(pending)
    1:begin command_end<={1'b0,command_address}+({17'd0,command_beats}<<3);pending<=2;end
    2:begin if(invalid_range)begin rejected<=1;pending<=0;end else pending<=3;end
    3:if(backend_ready)pending<=0;
    default:begin end
   endcase
  end else if(accept)begin
   grant_valid<=0;
   next_owner<=selected+1'b1;active<=1;owner<=selected;pending<=1;rejected<=0;early_cancel<=0;
   command_address<=chosen[3+:32];command_beats<=chosen[35+:16];command_min<=chosen[125+:32];command_limit<=chosen[157+:32];command_line<=chosen[189];
  end else if(grant_valid)begin
   // A client may withdraw an unaccepted command on cancel or permit loss.
   // No ownership or RR advance occurs until its actual valid/ready handshake.
   if(!(eligible & registered_grant))grant_valid<=0;
  end else if(found && backend_ready)begin
   registered_grant<=grant;selected<=encoded;grant_valid<=1;
  end
 end
 depth_dma_burst_pingpong_v1 #(.LOCAL_RETURN_CANCEL(1),.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) backend(
  .clk(clk),.rst_n(rst_n),.cancel(launched && owned[0]),
  .cmd_valid(active && pending==3 && !owned[0]),.cmd_ready(backend_ready),.cmd_write(WRITE!=0),
  .cmd_address(command_address),.cmd_beats(command_beats),
  .wr_strb(owned[115+:8]),.wr_data(owned[51+:64]),.wr_valid(launched && owned[123]),.wr_ready(backend_wr_ready),
  .rd_data(backend_rd_data),.rd_valid(backend_rd_valid),.rd_ready(launched && owned[124]),
  .done(backend_done),.cancelled(backend_cancelled),.fault(backend_fault),.quiescent(backend_idle),.progress(backend_progress),.transferred(backend_transferred),
  .m_axi_araddr(m_axi_araddr),.m_axi_arlen(m_axi_arlen),.m_axi_arsize(m_axi_arsize),.m_axi_arburst(m_axi_arburst),.m_axi_arvalid(m_axi_arvalid),.m_axi_arready(m_axi_arready),.m_axi_rdata(m_axi_rdata),.m_axi_rresp(m_axi_rresp),.m_axi_rlast(m_axi_rlast),.m_axi_rvalid(m_axi_rvalid),.m_axi_rready(m_axi_rready),.m_axi_awaddr(m_axi_awaddr),.m_axi_awlen(m_axi_awlen),.m_axi_awsize(m_axi_awsize),.m_axi_awburst(m_axi_awburst),.m_axi_awvalid(m_axi_awvalid),.m_axi_awready(m_axi_awready),.m_axi_wdata(m_axi_wdata),.m_axi_wstrb(m_axi_wstrb),.m_axi_wlast(m_axi_wlast),.m_axi_wvalid(m_axi_wvalid),.m_axi_wready(m_axi_wready),.m_axi_bresp(m_axi_bresp),.m_axi_bvalid(m_axi_bvalid),.m_axi_bready(m_axi_bready));
 // synthesis translate_off
 initial if(CLIENTS!=1 && CLIENTS!=2 && CLIENTS!=4 && CLIENTS!=8)$fatal(1,"Round robin client count must be1,2,4or8");
 always @(posedge clk)if(rst_n && launched && backend_transferred>4096)$fatal(1,"shared DMA count outside command bound");
 // synthesis translate_on
endmodule
`default_nettype wire
