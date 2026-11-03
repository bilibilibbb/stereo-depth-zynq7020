`timescale 1ns/1ps
// Counts PCLK in its own domain; Gray code is observed in the 50 MHz domain.
// VS timestamps are OUTPUT-FRAME markers, not exposure-start measurements.
module camera_monitor(
    input wire clk, input wire resetn, input wire pclk,
    input wire vs, input wire href, input wire [31:0] ticks,
    output reg [31:0] frames, output reg [31:0] last_frame_tick,
    output reg [31:0] period_ticks, output reg [31:0] last_lines,
    output reg [31:0] current_lines, output wire [31:0] pclk_count,
    output wire [1:0] levels
);
    (* ASYNC_REG="TRUE" *) reg [1:0] p_reset;
    always @(posedge pclk or negedge resetn)
        if (!resetn) p_reset<=0; else p_reset<={p_reset[0],1'b1};
    reg [31:0] pixel_binary;
    reg [31:0] pixel_gray;
    wire [31:0] pixel_next=pixel_binary+1'b1;
    always @(posedge pclk or negedge p_reset[1]) begin
        if (!p_reset[1]) begin pixel_binary<=0; pixel_gray<=0; end
        else begin pixel_binary<=pixel_next; pixel_gray<=pixel_next^(pixel_next>>1); end
    end
    (* ASYNC_REG="TRUE" *) reg [31:0] gray_meta,gray_sync;
    (* ASYNC_REG="TRUE" *) reg [1:0] vs_sync,href_sync;
    reg vs_prev,href_prev;
    assign levels={vs_sync[1],href_sync[1]};
    genvar b;
    generate for(b=0;b<32;b=b+1) begin: decode
        assign pclk_count[b]=^(gray_sync>>b);
    end endgenerate
    always @(posedge clk) begin
        if (!resetn) begin
            gray_meta<=0; gray_sync<=0; vs_sync<=0; href_sync<=0; vs_prev<=0; href_prev<=0;
            frames<=0; last_frame_tick<=0; period_ticks<=0; last_lines<=0; current_lines<=0;
        end else begin
            gray_meta<=pixel_gray; gray_sync<=gray_meta;
            vs_sync<={vs_sync[0],vs}; href_sync<={href_sync[0],href};
            vs_prev<=vs_sync[1]; href_prev<=href_sync[1];
            if (vs_sync[1] && !vs_prev) begin
                frames<=frames+1'b1;
                period_ticks<=(frames==0) ? 0 : ticks-last_frame_tick;
                last_frame_tick<=ticks; last_lines<=current_lines;
                current_lines<=0;
            end else if (href_sync[1] && !href_prev) current_lines<=current_lines+1'b1;
        end
    end
endmodule
