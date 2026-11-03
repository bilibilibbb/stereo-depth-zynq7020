`timescale 1ns/1ps
`default_nettype none
// Owned video candidate: a request arriving during vertical blanking can
// capture the upcoming full active frame. The actual shared SOF boundary is
// retained for stereo skew checks; this is not a shortened/partial frame.
// One request toggle owns a complete VS-to-VS frame. Metadata is held until
// the next request, and crosses only after the completion-toggle handshake.
module dvp_frame_source_preview #(parameter WIDTH=640,HEIGHT=480,FIFO_LOG2=9)(
    input wire arst_n,input wire pclk,input wire vs,input wire href,input wire [7:0] data,
    input wire request_toggle,input wire cancel_level,input wire [31:0] ticks_gray,
    output reg done_toggle,output wire [63:0] word_data,output wire word_valid,
    input wire word_ready,input wire [FIFO_LOG2:0] fifo_level,
    output reg [31:0] error_flags,frame_id,start_tick,end_tick,byte_count,line_count,
    output reg [31:0] accepted_words,crc32,fifo_peak,error_location
);
    localparam IDLE=0,WAIT_FRAME=1,CAPTURE=2;
    localparam FRAME_BYTES=WIDTH*HEIGHT*2;
    (* ASYNC_REG="TRUE" *) reg [1:0] local_reset,request_sync,cancel_sync;
    (* ASYNC_REG="TRUE" *) reg [31:0] tick_g1,tick_g2;
    reg seen_request,vs_d,href_d,drop_frame;
    reg [1:0] state;
    reg [2:0] pack_count;
    reg [63:0] pack;
    reg [31:0] line_bytes,frame_counter,crc_state;
    reg [31:0] last_boundary_tick;
    reg blank_open;
    wire vs_rise=vs&&!vs_d;
    wire byte_active=state==CAPTURE&&!vs&&href;
    wire line_end=href_d&&!href;
    wire [31:0] byte_errors=((byte_count>=FRAME_BYTES || line_bytes>=WIDTH*2)?32'h5:0) |
        ((word_valid&&!word_ready)?32'h8:0);
    wire [31:0] completed_lines=line_count+(line_end?1:0);
    wire [31:0] ending_errors=error_flags |
        ((completed_lines!=HEIGHT)?32'h2:0) |
        ((byte_count!=FRAME_BYTES)?32'h4:0) |
        ((pack_count!=0)?32'h200:0) |
        ((line_end&&line_bytes!=WIDTH*2)?32'h1:0) |
        (href?32'h100:0);
    function [31:0] from_gray;
        input [31:0] g;integer i;
        begin from_gray[31]=g[31];for(i=30;i>=0;i=i-1)from_gray[i]=from_gray[i+1]^g[i];end
    endfunction
    function [31:0] crc_byte;
        input [31:0] old;input [7:0] b;reg [31:0] c;integer k;
        begin c=old^{24'b0,b};for(k=0;k<8;k=k+1)c=c[0]?(c>>1)^32'hedb88320:c>>1;crc_byte=c;end
    endfunction
    assign word_data={data,pack[63:8]};
    assign word_valid=local_reset[1]&&byte_active&&pack_count==7&&!drop_frame&&byte_count<FRAME_BYTES&&!cancel_sync[1];
    always @(posedge pclk or negedge arst_n)
        if(!arst_n)local_reset<=0;else local_reset<={local_reset[0],1'b1};
    always @(posedge pclk or negedge local_reset[1]) begin
        if(!local_reset[1])begin
            request_sync<=0;cancel_sync<=0;tick_g1<=0;tick_g2<=0;
            state<=IDLE;seen_request<=0;done_toggle<=0;vs_d<=0;href_d<=0;drop_frame<=0;
            pack<=0;pack_count<=0;line_bytes<=0;frame_counter<=0;crc_state<=32'hffffffff;last_boundary_tick<=0;blank_open<=0;
            error_flags<=0;frame_id<=0;start_tick<=0;end_tick<=0;byte_count<=0;line_count<=0;
            accepted_words<=0;crc32<=0;fifo_peak<=0;error_location<=32'hffffffff;
        end else begin
            request_sync<={request_sync[0],request_toggle};cancel_sync<={cancel_sync[0],cancel_level};
            tick_g1<=ticks_gray;tick_g2<=tick_g1;vs_d<=vs;href_d<=href;
            if(vs_rise)begin frame_counter<=frame_counter+1'b1;last_boundary_tick<=from_gray(tick_g2);blank_open<=1;end
            else if(href)blank_open<=0;
            if(state==IDLE)begin
                if(request_sync[1]!=seen_request)begin
                    seen_request<=request_sync[1];state<=WAIT_FRAME;drop_frame<=0;
                    error_flags<=0;pack_count<=0;pack<=0;line_bytes<=0;byte_count<=0;line_count<=0;
                    accepted_words<=0;crc_state<=32'hffffffff;crc32<=0;fifo_peak<=0;
                    start_tick<=0;end_tick<=0;frame_id<=0;error_location<=32'hffffffff;
                    if((vs_rise||blank_open)&&!href)begin
                        state<=CAPTURE;start_tick<=vs_rise?from_gray(tick_g2):last_boundary_tick;
                        frame_id<=frame_counter+(vs_rise?32'd1:32'd0);
                    end
                end
            end else if(cancel_sync[1])begin
                error_flags<=error_flags|32'h10;crc32<=~crc_state;end_tick<=from_gray(tick_g2);
                state<=IDLE;done_toggle<=seen_request;
            end else if(state==WAIT_FRAME)begin
                if(vs_rise)begin state<=CAPTURE;start_tick<=from_gray(tick_g2);frame_id<=frame_counter+1'b1;end
            end else if(vs_rise)begin
                error_flags<=ending_errors;line_count<=completed_lines;
                crc32<=~crc_state;end_tick<=from_gray(tick_g2);state<=IDLE;done_toggle<=seen_request;
                if(error_flags==0 && ending_errors!=0)error_location<={line_count[15:0],line_bytes[15:0]};
            end else begin
                if(vs&&href)begin error_flags<=error_flags|32'h100;drop_frame<=1;end
                if(line_end)begin
                    line_count<=line_count+1'b1;line_bytes<=0;
                    if(line_bytes!=WIDTH*2)begin
                        error_flags<=error_flags|32'h1;
                        if(error_flags==0)error_location<={line_count[15:0],line_bytes[15:0]};
                    end
                end
                if(byte_active)begin
                    byte_count<=byte_count+1'b1;line_bytes<=line_bytes+1'b1;
                    pack<={data,pack[63:8]};pack_count<=pack_count+1'b1;crc_state<=crc_byte(crc_state,data);
                    if(byte_errors!=0)begin
                        error_flags<=error_flags|byte_errors;
                        if(error_flags==0)error_location<={line_count[15:0],line_bytes[15:0]};
                    end
                    if(word_valid)begin
                        if(word_ready)begin
                            accepted_words<=accepted_words+1'b1;
                            if({{(31-FIFO_LOG2){1'b0}},fifo_level}+1>fifo_peak)fifo_peak<=fifo_level+1'b1;
                        end else begin
                            drop_frame<=1;
                        end
                    end
                end
            end
        end
    end
    initial if(WIDTH<4 || WIDTH%4!=0 || HEIGHT<1 || FIFO_LOG2>15)$error("Invalid DVP geometry");
endmodule
`default_nettype wire
