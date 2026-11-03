`timescale 1ns/1ps
`default_nettype none
// Single outstanding, 64-bit INCR, <=16 beats and never crosses 4KiB.
// Two buffers overlap filling and draining; each burst is complete BEFORE AW.
// Single external write outstanding, cancellation cannot starve its W channel.
// Once AR/AW VALID is raised it is held through acceptance, including cancellation.
// Malformed RLAST poisons the channel until fabric reset; quiescent stays false.
module fw_shared_dma_backend #(
    // Opt in only when the owner wrapper suppresses cancelled returns locally.
    // Generic users retain the original immediate cancel gate on RD_VALID.
    parameter LOCAL_RETURN_CANCEL=0,
    parameter [31:0] ADDRESS_MIN=32'h10000000,
    parameter [31:0] ADDRESS_END=32'h20000000
)(
    input wire clk,input wire rst_n,input wire cancel,
    input wire cmd_valid,output wire cmd_ready,input wire cmd_write,
    input wire [31:0] cmd_address,input wire [15:0] cmd_beats,
    input wire[7:0]wr_strb,input wire [63:0] wr_data,input wire wr_valid,output wire wr_ready,
    output wire [63:0] rd_data,output wire rd_valid,input wire rd_ready,
    output reg done,output reg cancelled,output reg [7:0] fault,
    output wire quiescent,output wire poisoned,output wire progress,output wire [31:0] transferred,
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
    localparam IDLE=0,AR=1,READ=2,FILL=3,AW=4,WRITE=5,BRESP=6,POISON=7,RFINISH=8;
    reg [3:0] state;
    reg[12:0]transferred_count;
    assign transferred={19'd0,transferred_count};
    reg [31:0] address;
    reg [15:0] remaining;
    reg [4:0] burst,index;
    reg killed;
    reg [63:0] buffer [0:31];reg[7:0]buffer_strb[0:31];
    reg [1:0] full;
    reg fill_bank,drain_bank;
    reg [4:0] fill_index,fill_burst,bank_length[0:1];
    reg [15:0] fill_left;
    reg [31:0] fill_address;
    wire stop=cancel || killed;
    // Same unsigned widths, overflow handling and burst timing as baseline.
    // These three exact adders per backend may use available DSP resources.
    (* use_dsp="yes" *) wire [32:0] cmd_end={1'b0,cmd_address}+({17'b0,cmd_beats}<<3);
    (* use_dsp="yes" *) wire [31:0] next_address=address+({27'b0,burst}<<3);
    (* use_dsp="yes" *) wire [31:0] next_fill_address=fill_address+({27'd0,fill_burst}<<3);
    function [4:0] burst_size;
        input [31:0] addr;input [15:0] left;
        reg [9:0] boundary;reg [15:0] n;
        begin boundary=(13'd4096-{1'b0,addr[11:0]})>>3;
            n=left>16?16:left;if(n>boundary)n=boundary;burst_size=n[4:0];end
    endfunction
    assign quiescent=state==IDLE;
    assign poisoned=state==POISON;
    assign cmd_ready=quiescent && !cancel;
    assign wr_ready=(state==FILL || state==AW || state==WRITE || state==BRESP) && !full[fill_bank] && fill_left!=0 && !stop && fault==0;
    // Only accepted, response-checked AXI beats enter the return queue.
    // AXI RREADY depends on registered occupancy; consumer ready cannot
    // traverse native map/cache/config and return to its own input on this cycle.
    reg[63:0]read_queue[0:1];reg[1:0]read_count;reg read_head,read_tail;
    wire read_pop=rd_valid && rd_ready;
    wire bad_read=state==READ && m_axi_rvalid && m_axi_rready &&
                  (m_axi_rresp!=0 || m_axi_rlast!=(index==burst-1));
    wire read_push=state==READ && m_axi_rvalid && m_axi_rready && !stop && fault==0 && !bad_read;
    assign rd_data=read_queue[read_head];
    assign rd_valid=(state==READ || state==AR || state==RFINISH) && read_count!=0 && !killed && (LOCAL_RETURN_CANCEL || !cancel) && fault==0;
    always @(posedge clk)begin
        if(!rst_n || state==IDLE || stop || fault!=0 || bad_read)begin
            read_count<=0;read_head<=0;read_tail<=0;
        end else begin
            if(read_push)begin read_queue[read_tail]<=m_axi_rdata;read_tail<=!read_tail;end
            if(read_pop)read_head<=!read_head;
            case({read_push,read_pop})
                2'b10:read_count<=read_count+1'b1;
                2'b01:read_count<=read_count-1'b1;
                default:begin end
            endcase
        end
    end
    assign m_axi_araddr=address;assign m_axi_arlen={3'b0,burst}-1'b1;
    assign m_axi_arsize=3;assign m_axi_arburst=1;assign m_axi_arvalid=state==AR;
    assign m_axi_rready=(state==READ && (stop || fault!=0 || m_axi_rresp!=0 || read_count<2)) || state==POISON;
    assign m_axi_awaddr=address;assign m_axi_awlen={3'b0,burst}-1'b1;
    assign m_axi_awsize=3;assign m_axi_awburst=1;assign m_axi_awvalid=state==AW;
    assign m_axi_wdata=buffer[{drain_bank,index[3:0]}];assign m_axi_wstrb=buffer_strb[{drain_bank,index[3:0]}];
    assign m_axi_wlast=index==burst-1;assign m_axi_wvalid=state==WRITE;
    assign m_axi_bready=state==BRESP;
    assign progress=(m_axi_arvalid && m_axi_arready)||(m_axi_rvalid && m_axi_rready)||
        (m_axi_awvalid && m_axi_awready)||(m_axi_wvalid && m_axi_wready)||(m_axi_bvalid && m_axi_bready);
    always @(posedge clk) begin
        if(!rst_n) begin
            state<=IDLE;address<=0;remaining<=0;burst<=0;index<=0;killed<=0;
            full<=0;fill_bank<=0;drain_bank<=0;fill_index<=0;fill_burst<=0;fill_left<=0;fill_address<=0;bank_length[0]<=0;bank_length[1]<=0;
            done<=0;cancelled<=0;fault<=0;transferred_count<=0;
        end else begin
            done<=0;cancelled<=0;
            if(cancel && state!=IDLE)killed<=1;
            // Only complete bursts can become externally visible. A second
            // buffer may fill while the current fully buffered burst drains.
            // Cancel/error discards unpublished data and finishes any AW already
            // presented, so external W never depends on the cancelled producer.
            if(wr_valid && wr_ready)begin
                buffer[{fill_bank,fill_index[3:0]}]<=wr_data;
                buffer_strb[{fill_bank,fill_index[3:0]}]<=wr_strb;
                if(fill_index+1==fill_burst)begin
                    full[fill_bank]<=1;bank_length[fill_bank]<=fill_burst;
                    fill_bank<=!fill_bank;fill_index<=0;
                    fill_left<=fill_left-fill_burst;
                    fill_address<=next_fill_address;
                    fill_burst<=burst_size(next_fill_address,fill_left-fill_burst);
                end else fill_index<=fill_index+1'b1;
            end
            case(state)
            IDLE:if(cmd_valid && cmd_ready) begin
                killed<=0;fault<=0;transferred_count<=0;
                full<=0;fill_bank<=0;drain_bank<=0;fill_index<=0;
                fill_left<=cmd_beats;fill_address<=cmd_address;fill_burst<=burst_size(cmd_address,cmd_beats);
                if(cmd_beats==0 || cmd_beats>4096 || cmd_address[2:0]!=0 || cmd_address<ADDRESS_MIN ||
                    cmd_end>{1'b0,ADDRESS_END} || cmd_end[32])begin fault<=1;done<=1;end
                else begin
                    address<=cmd_address;remaining<=cmd_beats;burst<=burst_size(cmd_address,cmd_beats);index<=0;
                    state<=cmd_write?FILL:AR;
                end
            end
            AR:if(m_axi_arready)begin state<=READ;index<=0;end
            READ:if(m_axi_rvalid && m_axi_rready)begin
                transferred_count<=transferred_count+1'b1;
                if(m_axi_rresp!=0 && fault==0)fault<=2;
                if(m_axi_rlast!=(index==burst-1)) begin fault<=fault==0?4:fault;state<=POISON;end
                else if(index==burst-1)begin
                    if(stop || fault!=0 || m_axi_rresp!=0 || remaining==burst)begin
                        if(stop || fault!=0 || m_axi_rresp!=0)begin state<=IDLE;done<=1;cancelled<=stop;end
                        else state<=RFINISH;
                    end else begin
                        address<=next_address;remaining<=remaining-burst;
                        burst<=burst_size(next_address,remaining-burst);index<=0;state<=AR;
                    end
                end else index<=index+1'b1;
            end
            RFINISH:if(stop || fault!=0 || read_count==0 || (read_count==1 && read_pop))begin
                state<=IDLE;done<=1;cancelled<=stop;
            end
            FILL:if(stop)begin state<=IDLE;done<=1;cancelled<=1;full<=0;end
                else if(full[drain_bank])begin burst<=bank_length[drain_bank];state<=AW;index<=0;end
            AW:if(m_axi_awready)begin state<=WRITE;index<=0;end
            WRITE:if(m_axi_wready)begin
                if(index==burst-1)begin state<=BRESP;index<=0;end else index<=index+1'b1;
            end
            BRESP:if(m_axi_bvalid)begin
                transferred_count<=transferred_count+burst;full[drain_bank]<=0;drain_bank<=!drain_bank;
                if(m_axi_bresp!=0 && fault==0)fault<=3;
                if(stop || m_axi_bresp!=0 || remaining==burst)begin state<=IDLE;done<=1;cancelled<=stop;full<=0;end
                else begin
                    address<=next_address;remaining<=remaining-burst;
                    burst<=burst_size(next_address,remaining-burst);index<=0;state<=FILL;
                end
            end
            POISON:state<=POISON;
            default:begin state<=POISON;fault<=5;end
            endcase
        end
    end
// synthesis translate_off
    always @(posedge clk)if(rst_n && transferred_count>4096)$fatal(1,"Accepted DMA transfer count exceeded4096");
    // synthesis translate_on
endmodule
`default_nettype wire
