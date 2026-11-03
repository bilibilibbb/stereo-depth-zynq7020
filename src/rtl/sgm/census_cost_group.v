`timescale 1ns/1ps
// LANES independent Hamming costs. Valid costs 0..24; invalid sentinel 255.
// Descriptor i occupies in_right[i*24+:24]. No spatial history in this block.
module census_cost_group #(
    parameter LANES=32, parameter TAG_WIDTH=32
)(
    input wire clk,input wire resetn,
    input wire in_valid,output wire in_ready,
    input wire [23:0] in_left,input wire in_left_support,
    input wire [LANES*24-1:0] in_right,input wire [LANES-1:0] in_right_support,
    input wire [TAG_WIDTH-1:0] in_tag,
    output reg out_valid,input wire out_ready,
    output reg [LANES*8-1:0] out_cost,
    output reg [TAG_WIDTH-1:0] out_tag
);
    function [2:0] pop4;
        input [3:0] x;
        begin pop4={2'b0,x[0]}+{2'b0,x[1]}+{2'b0,x[2]}+{2'b0,x[3]}; end
    endfunction
    function [4:0] pop24;
        input [23:0] x;
        reg [3:0] a,b,c;
        begin
            a={1'b0,pop4(x[3:0])}+{1'b0,pop4(x[7:4])};
            b={1'b0,pop4(x[11:8])}+{1'b0,pop4(x[15:12])};
            c={1'b0,pop4(x[19:16])}+{1'b0,pop4(x[23:20])};
            pop24={1'b0,a}+{1'b0,b}+{1'b0,c};
        end
    endfunction
    wire [LANES*8-1:0] costs;
    genvar d;
    generate for(d=0;d<LANES;d=d+1) begin: lane
        assign costs[d*8+:8]=(in_left_support && in_right_support[d]) ?
            {3'b0,pop24(in_left^in_right[d*24+:24])} : 8'hff;
    end endgenerate
    assign in_ready=!out_valid || out_ready;
    always @(posedge clk) begin
        if(!resetn) begin out_valid<=0; out_cost<={LANES{8'hff}}; out_tag<=0; end
        else if(in_ready) begin
            out_valid<=in_valid;
            if(in_valid) begin out_cost<=costs; out_tag<=in_tag; end
        end
    end
endmodule
