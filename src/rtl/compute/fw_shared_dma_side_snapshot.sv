`timescale 1ns/1ps

`default_nettype none

// One standard DMA backend, with command ownership retained through drain/poison.

module fw_shared_dma_side #(parameter CLIENTS=1,WRITE=0,FIXED_CLIENT=-1,INDEX_BITS=(CLIENTS<=1?1:$clog2(CLIENTS)),

 parameter[31:0]ADDRESS_MIN=32'h19000000,ADDRESS_END=32'h1a000000)(

 input wire clk,rst_n,input wire[CLIENTS*190-1:0]request,input wire[CLIENTS-1:0]permit,

 output wire[CLIENTS*113-1:0]response,output wire backend_quiescent,output wire[CLIENTS-1:0]poisoned,

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

 reg active,rejected;reg[INDEX_BITS-1:0]owner,selected;reg found;integer c;

 wire[189:0]requests[0:CLIENTS-1];wire[189:0]chosen=requests[selected],owned=requests[owner];

 wire backend_poisoned;
 wire backend_ready,backend_done,backend_cancelled,backend_progress,backend_wr_ready,backend_rd_valid;

 wire[7:0]backend_fault;wire[31:0]backend_transferred;wire[63:0]backend_rd_data;

 wire[32:0]command_end={1'b0,chosen[3+:32]}+({17'd0,chosen[35+:16]}<<3);

 wire invalid_range=chosen[3+:32]<chosen[125+:32] || command_end>{1'b0,chosen[157+:32]} || command_end[32] ||

  (chosen[189] && (WRITE!=0 || chosen[35+:16]!=8 || chosen[3+:6]!=0));

 wire accept=rst_n && !active && found && backend_ready;

 always @*begin

  found=0;selected=FIXED_CLIENT>=0?FIXED_CLIENT:0;

  if(FIXED_CLIENT>=0)begin
   found=requests[FIXED_CLIENT][1] && requests[FIXED_CLIENT][2]==(WRITE!=0) && !requests[FIXED_CLIENT][0] && permit[FIXED_CLIENT];
  end else for(c=CLIENTS-1;c>=0;c=c-1)if(requests[c][1] && requests[c][2]==(WRITE!=0) && !requests[c][0] && permit[c])begin found=1;selected=c;end

 end

 genvar g;

 generate for(g=0;g<CLIENTS;g=g+1)begin:CLIENT

  reg[7:0]last_fault;reg[12:0]last_transferred;

  wire mine=active && owner==g;

  wire[7:0]current_fault=mine?(rejected?8'd1:backend_fault):last_fault;

  wire[31:0]current_count=mine?(rejected?32'd0:backend_transferred):{19'd0,last_transferred};

  assign poisoned[g]=mine && backend_poisoned;
  assign requests[g]=request[g*190+:190];

  assign response[g*113+0]=rst_n && !active && backend_ready && !requests[g][0] && permit[g] && requests[g][2]==(WRITE!=0) && (!found || selected==g);

  assign response[g*113+1]=mine && !rejected && backend_wr_ready;

  assign response[g*113+2]=mine && !rejected && backend_rd_valid;

  assign response[g*113+3+:64]=backend_rd_data;

  assign response[g*113+67]=mine && (rejected || backend_done);

  assign response[g*113+68]=mine && !rejected && backend_cancelled;

  assign response[g*113+69+:8]=current_fault;

  assign response[g*113+77]=!mine || rejected || backend_quiescent;

  assign response[g*113+78]=mine && !rejected && backend_progress;

  assign response[g*113+79+:32]=current_count;

  assign response[g*113+111]=mine && !rejected && m_axi_rvalid && m_axi_rready;

  assign response[g*113+112]=mine && !rejected && m_axi_wvalid && m_axi_wready;

  always @(posedge clk)begin

   if(!rst_n)begin last_fault<=0;last_transferred<=0;end

   else if(accept && selected==g)begin last_fault<=0;last_transferred<=0;end

   else if(mine)begin last_fault<=current_fault;last_transferred<=current_count[12:0];end

  end

 end endgenerate

 always @(posedge clk)begin

  if(!rst_n)begin active<=0;rejected<=0;owner<=0;end

  else if(active)begin

   if(rejected || (backend_done && backend_quiescent))begin active<=0;rejected<=0;end

  end else if(accept)begin active<=1;owner<=selected;rejected<=invalid_range;end

 end

 fw_shared_dma_backend #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) backend(

  .clk(clk),.rst_n(rst_n),.cancel(active && !rejected && owned[0]),

  .cmd_valid(!active && found && !invalid_range),.cmd_ready(backend_ready),.cmd_write(WRITE!=0),

  .cmd_address(chosen[3+:32]),.cmd_beats(chosen[35+:16]),

  .wr_strb(owned[115+:8]),.wr_data(owned[51+:64]),.wr_valid(active && !rejected && owned[123]),.wr_ready(backend_wr_ready),

  .rd_data(backend_rd_data),.rd_valid(backend_rd_valid),.rd_ready(active && !rejected && owned[124]),

  .done(backend_done),.cancelled(backend_cancelled),.fault(backend_fault),.quiescent(backend_quiescent),.poisoned(backend_poisoned),.progress(backend_progress),.transferred(backend_transferred),

  .m_axi_araddr(m_axi_araddr),.m_axi_arlen(m_axi_arlen),.m_axi_arsize(m_axi_arsize),.m_axi_arburst(m_axi_arburst),.m_axi_arvalid(m_axi_arvalid),.m_axi_arready(m_axi_arready),.m_axi_rdata(m_axi_rdata),.m_axi_rresp(m_axi_rresp),.m_axi_rlast(m_axi_rlast),.m_axi_rvalid(m_axi_rvalid),.m_axi_rready(m_axi_rready),.m_axi_awaddr(m_axi_awaddr),.m_axi_awlen(m_axi_awlen),.m_axi_awsize(m_axi_awsize),.m_axi_awburst(m_axi_awburst),.m_axi_awvalid(m_axi_awvalid),.m_axi_awready(m_axi_awready),.m_axi_wdata(m_axi_wdata),.m_axi_wstrb(m_axi_wstrb),.m_axi_wlast(m_axi_wlast),.m_axi_wvalid(m_axi_wvalid),.m_axi_wready(m_axi_wready),.m_axi_bresp(m_axi_bresp),.m_axi_bvalid(m_axi_bvalid),.m_axi_bready(m_axi_bready));

 // synthesis translate_off

 always @(posedge clk)if(rst_n && active && !rejected && backend_transferred>4096)$fatal(1,"shared DMA count outside command bound");

 // synthesis translate_on

 // synthesis translate_off
 integer fixed_check;
 always @(posedge clk)if(rst_n && FIXED_CLIENT>=0)begin
  if(FIXED_CLIENT>=CLIENTS)$fatal(1,"Fixed DMA client outside fabric");
  for(fixed_check=0;fixed_check<CLIENTS;fixed_check=fixed_check+1)
   if(fixed_check!=FIXED_CLIENT && requests[fixed_check][1] && requests[fixed_check][2]==(WRITE!=0) && !requests[fixed_check][0] && permit[fixed_check])
    $fatal(1,"Nonfixed DMA client requested this side");
 end
 // synthesis translate_on
endmodule

`default_nettype wire

