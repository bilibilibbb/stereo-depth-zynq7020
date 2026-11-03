`timescale 1ns/1ps
`default_nettype none
// v3: wait three system cycles AFTER synchronized completion BEFORE sampling
// the held source metadata. Then consume source errors only after a system-clock metadata register;
// expose the synchronized completion bit to debug. Held-bus constraints still required.
// Bounded single-frame capture. Failure drains both the CDC FIFO and all AXI
// obligations; a stuck slave remains busy/faulted and can never release its DDR.
module dvp_ddr_channel #(
    parameter WIDTH=640,HEIGHT=480,FIFO_LOG2=9,WATCHDOG_CYCLES=10000000,
    parameter [31:0] ADDRESS_MIN=32'h18000000,ADDRESS_END=32'h18400000
)(
    input wire clk,input wire arst_n,input wire arm,input wire cancel,
    input wire [31:0] base_address,input wire [31:0] ticks_gray,
    input wire pclk,vs,href,input wire [7:0] data,
    output reg busy,output reg done,output reg frame_valid,
    output reg [31:0] error_flags,frame_id,start_tick,end_tick,byte_count,line_count,
    output reg [31:0] accepted_words,crc32,fifo_peak,error_location,
    output reg [31:0] committed_words,fifo_reads,elapsed_cycles,
    output wire [31:0] debug_state,
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
    localparam FRAME_BYTES=WIDTH*HEIGHT*2,FRAME_WORDS=FRAME_BYTES/8;
    (* ASYNC_REG="TRUE" *) reg [1:0] local_reset,done_sync;
    reg request_toggle,aborting,inflight,metadata_seen,completion_seen;
    reg source_cancel_registered;
    reg [2:0] metadata_settle;
    reg [31:0] address;
    reg [31:0] sampled_source_error;
    wire source_done,fifo_wr_valid,fifo_wr_ready,fifo_rd_valid,fifo_rd_ready,fifo_empty;
    wire [63:0] fifo_wr_data,fifo_rd_data;
    wire [FIFO_LOG2:0] fifo_wr_level;
    wire [31:0] source_error,source_id,source_start,source_end,source_bytes,source_lines,
        source_words,source_crc,source_peak,source_location;
    wire source_cancel=busy&&(aborting||cancel);
    wire [32:0] address_end={1'b0,base_address}+FRAME_BYTES;
    wire dma_ready,dma_wr_ready,dma_done,dma_cancelled,dma_quiet,dma_progress;
    wire [7:0] dma_fault;
    wire [31:0] dma_transferred;
    wire dma_cancel=source_cancel;
    wire dma_command=busy&&!aborting&&!cancel&&!inflight&&fifo_rd_valid&&committed_words<FRAME_WORDS;
    wire [15:0] command_words=(FRAME_WORDS-committed_words)>16?16:FRAME_WORDS-committed_words;
    wire completion_arrived=busy&&!completion_seen&&done_sync[1]==request_toggle;
    wire metadata_arrived=busy&&completion_seen&&!metadata_seen&&metadata_settle==1;
    wire timed_out=busy&&elapsed_cycles>=WATCHDOG_CYCLES&&(error_flags&32'h20)==0;
    wire [31:0] new_errors=(cancel?32'h10:0)|(timed_out?32'h20:0)|
        ((inflight&&dma_done&&dma_fault!=0)?(32'h40|{dma_fault,24'b0}):0)|
        (metadata_seen?sampled_source_error:0);
    assign fifo_rd_ready=busy&&(aborting||cancel||dma_wr_ready);
    assign debug_state={24'b0,metadata_seen,done_sync[1],request_toggle,aborting,inflight,dma_quiet,fifo_empty,busy};
    always @(posedge clk or negedge arst_n)
        if(!arst_n)local_reset<=0;else local_reset<={local_reset[0],1'b1};
    dvp_frame_source_preview #(.WIDTH(WIDTH),.HEIGHT(HEIGHT),.FIFO_LOG2(FIFO_LOG2)) source(
        .arst_n(arst_n),.pclk(pclk),.vs(vs),.href(href),.data(data),
        .request_toggle(request_toggle),.cancel_level(source_cancel_registered),.ticks_gray(ticks_gray),
        .done_toggle(source_done),.word_data(fifo_wr_data),.word_valid(fifo_wr_valid),.word_ready(fifo_wr_ready),.fifo_level(fifo_wr_level),
        .error_flags(source_error),.frame_id(source_id),.start_tick(source_start),.end_tick(source_end),
        .byte_count(source_bytes),.line_count(source_lines),.accepted_words(source_words),.crc32(source_crc),.fifo_peak(source_peak),.error_location(source_location));
    camera_async_fifo #(.DATA_BITS(64),.LOG2_DEPTH(FIFO_LOG2)) fifo(
        .arst_n(arst_n),.wclk(pclk),.rclk(clk),.wdata(fifo_wr_data),.wvalid(fifo_wr_valid),.wready(fifo_wr_ready),.wlevel(fifo_wr_level),
        .rdata(fifo_rd_data),.rvalid(fifo_rd_valid),.rready(fifo_rd_ready),.rempty(fifo_empty));
    depth_dma #(.ADDRESS_MIN(ADDRESS_MIN),.ADDRESS_END(ADDRESS_END)) dma(
        .clk(clk),.rst_n(local_reset[1]),.cancel(dma_cancel),.cmd_valid(dma_command),.cmd_ready(dma_ready),.cmd_write(1'b1),
        .cmd_address(address+(committed_words<<3)),.cmd_beats(command_words),
        .wr_data(fifo_rd_data),.wr_valid(fifo_rd_valid&&!aborting&&!cancel),.wr_ready(dma_wr_ready),
        .rd_data(),.rd_valid(),.rd_ready(1'b0),.done(dma_done),.cancelled(dma_cancelled),.fault(dma_fault),
        .quiescent(dma_quiet),.progress(dma_progress),.transferred(dma_transferred),
        .m_axi_araddr(m_axi_araddr),.m_axi_arlen(m_axi_arlen),.m_axi_arsize(m_axi_arsize),.m_axi_arburst(m_axi_arburst),.m_axi_arvalid(m_axi_arvalid),.m_axi_arready(m_axi_arready),
        .m_axi_rdata(m_axi_rdata),.m_axi_rresp(m_axi_rresp),.m_axi_rlast(m_axi_rlast),.m_axi_rvalid(m_axi_rvalid),.m_axi_rready(m_axi_rready),
        .m_axi_awaddr(m_axi_awaddr),.m_axi_awlen(m_axi_awlen),.m_axi_awsize(m_axi_awsize),.m_axi_awburst(m_axi_awburst),.m_axi_awvalid(m_axi_awvalid),.m_axi_awready(m_axi_awready),
        .m_axi_wdata(m_axi_wdata),.m_axi_wstrb(m_axi_wstrb),.m_axi_wlast(m_axi_wlast),.m_axi_wvalid(m_axi_wvalid),.m_axi_wready(m_axi_wready),
        .m_axi_bresp(m_axi_bresp),.m_axi_bvalid(m_axi_bvalid),.m_axi_bready(m_axi_bready));
    always @(posedge clk or negedge arst_n) begin
        if(!arst_n)begin
            source_cancel_registered<=0;done_sync<=0;request_toggle<=0;aborting<=0;inflight<=0;metadata_seen<=0;completion_seen<=0;metadata_settle<=0;address<=0;sampled_source_error<=0;
            busy<=0;done<=0;frame_valid<=0;error_flags<=0;frame_id<=0;start_tick<=0;end_tick<=0;byte_count<=0;line_count<=0;
            accepted_words<=0;crc32<=0;fifo_peak<=0;error_location<=32'hffffffff;committed_words<=0;fifo_reads<=0;elapsed_cycles<=0;
        end else if(local_reset[1])begin
            done<=0;done_sync<={done_sync[0],source_done};
            // Only a flip-flop output crosses to the PCLK cancel synchronizer.
            // The local DMA still sees immediate cancellation; the source stops
            // one clk later and any additional FIFO words are drained.
            source_cancel_registered<=source_cancel;
            if(!busy)begin
                if(arm)begin
                    frame_valid<=0;error_flags<=0;committed_words<=0;fifo_reads<=0;elapsed_cycles<=0;
                    metadata_seen<=0;completion_seen<=0;metadata_settle<=0;aborting<=0;inflight<=0;sampled_source_error<=0;
                    frame_id<=0;start_tick<=0;end_tick<=0;byte_count<=0;line_count<=0;
                    accepted_words<=0;crc32<=0;fifo_peak<=0;error_location<=32'hffffffff;
                    if(base_address[2:0]!=0 || base_address<ADDRESS_MIN || address_end[32] || address_end>{1'b0,ADDRESS_END} || !fifo_empty || !dma_quiet || cancel)begin
                        error_flags<=32'h80;done<=1;
                    end else begin address<=base_address;busy<=1;request_toggle<=!request_toggle;end
                end
            end else begin
                if(elapsed_cycles!=32'hffffffff)elapsed_cycles<=elapsed_cycles+1'b1;
                if(new_errors!=0)begin aborting<=1;error_flags<=error_flags|new_errors;end
                if(dma_command&&dma_ready)inflight<=1;
                if(fifo_rd_valid&&fifo_rd_ready)fifo_reads<=fifo_reads+1'b1;
                if(inflight&&dma_done)begin
                    inflight<=0;committed_words<=committed_words+dma_transferred;
                end
                if(completion_arrived)begin completion_seen<=1;metadata_settle<=3;end
                else if(metadata_settle!=0)metadata_settle<=metadata_settle-1'b1;
                if(metadata_arrived)begin
                    metadata_seen<=1;sampled_source_error<=source_error;
                    frame_id<=source_id;start_tick<=source_start;end_tick<=source_end;byte_count<=source_bytes;line_count<=source_lines;
                    accepted_words<=source_words;crc32<=source_crc;fifo_peak<=source_peak;error_location<=source_location;
                end
                if(metadata_seen&&metadata_settle==0&&fifo_reads==accepted_words&&fifo_empty&&!inflight&&dma_quiet)begin
                    busy<=0;done<=1;
                    frame_valid<=!aborting&&new_errors==0&&error_flags==0&&committed_words==FRAME_WORDS&&byte_count==FRAME_BYTES&&line_count==HEIGHT;
                    if(!aborting&&committed_words!=FRAME_WORDS)error_flags<=error_flags|new_errors|32'h4;
                end
            end
        end
    end
    initial if(FRAME_BYTES%8!=0 || WATCHDOG_CYCLES<32 || FRAME_BYTES<8)$error("Invalid channel contract");
endmodule
`default_nettype wire
