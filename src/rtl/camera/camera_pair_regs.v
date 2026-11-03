`timescale 1ns/1ps
`default_nettype none
// Register-level control, independent from AXI-Lite transport. A completed slot
// remains owned until a matching session-cookie and epoch release is accepted.
module camera_pair_regs #(parameter WIDTH=640,HEIGHT=480)(
    input wire clk,resetn,
    input wire wr_en,input wire [15:0] wr_addr,input wire [31:0] wr_data,input wire [3:0] wr_strb,
    output reg [1:0] wr_resp,
    input wire [15:0] rd_addr,output reg [31:0] rd_data,output reg [1:0] rd_resp,
    input wire [1:0] channel_busy,channel_done,channel_valid,
    input wire [895:0] channel_metadata,
    output reg [1:0] arm,cancel,output wire [63:0] base_address,
    output reg [31:0] ticks_gray,output wire irq
);
    localparam FRAME_BYTES=WIDTH*HEIGHT*2;
    reg active,active_slot,cancelled;
    reg [1:0] owned,ready,seen,valid_seen;
    reg [31:0] ticks,cookie,release_epoch,release_cookie,format,mode_tag,skew_limit,next_epoch;
    reg [31:0] rejected,reject_flags,captured,bad_pairs;
    reg [31:0] descriptor[0:127];
    reg [31:0] rejection;
    integer i;
    wire selected_slot=wr_data[8];
    wire configuration_idle=!active&&owned==0&&channel_busy==0;
    wire [31:0] left_base=active_slot?32'h18200000:32'h18000000;
    wire [31:0] right_base=active_slot?32'h18300000:32'h18100000;
    wire [31:0] tick_delta=descriptor[{active_slot,6'b0}+18]-descriptor[{active_slot,6'b0}+34];
    // Both starts lie within the bounded capture timeout (<2^31 ticks).
    wire [31:0] skew=tick_delta[31]?(~tick_delta+1'b1):tick_delta;
    wire [31:0] pair_error=(!valid_seen[0]?32'h1:0)|(!valid_seen[1]?32'h2:0)|
        ((skew>descriptor[{active_slot,6'b0}+6])?32'h4:0)|
        ((cancelled||(wr_en&&rejection==0&&wr_addr==16'hc&&wr_data==2))?32'h8:0);
    assign base_address={right_base,left_base};
    assign irq=(ready!=0)||(reject_flags!=0);

    always @* begin
        rejection=0;
        if(wr_addr[1:0]!=0||wr_strb!=4'hf)rejection=32'h2;
        else case(wr_addr)
        16'h000c:begin
            case(wr_data)
            32'h1,32'h101:begin
                if(active||channel_busy!=0||owned[selected_slot])rejection=32'h1;
                else if(cookie==0||format<1||format>4||mode_tag==0)rejection=32'h8;
                else if(next_epoch==32'hffffffff)rejection=32'h10;
            end
            32'h2:if(!active)rejection=32'h1;
            32'h3,32'h103:begin
                if(!owned[selected_slot]||!ready[selected_slot])rejection=32'h1;
                else if(release_epoch!=descriptor[{selected_slot,6'b0}+1]||
                        release_cookie!=descriptor[{selected_slot,6'b0}+2])rejection=32'h4;
            end
            32'h4:if(active)rejection=32'h1;
            default:rejection=32'h2;
            endcase
        end
        16'h0010,16'h001c,16'h0020,16'h0024:if(!configuration_idle)rejection=32'h1;
        16'h0014,16'h0018:rejection=0;
        default:rejection=32'h2;
        endcase
        wr_resp=rejection!=0?2'b10:2'b00;
    end

    always @* begin
        rd_data=0;rd_resp=0;
        if(rd_addr[1:0]!=0)rd_resp=2'b10;
        else if(rd_addr>=16'h0100&&rd_addr<16'h0300)rd_data=descriptor[(rd_addr-16'h0100)>>2];
        else if(rd_addr>=16'h0400&&rd_addr<16'h0438)rd_data=channel_metadata[((rd_addr-16'h0400)>>2)*32+:32];
        else if(rd_addr>=16'h0440&&rd_addr<16'h0478)rd_data=channel_metadata[448+((rd_addr-16'h0440)>>2)*32+:32];
        else case(rd_addr)
        16'h0000:rd_data=32'h43415032;
        16'h0004:rd_data=32'h00010000;
        16'h0008:rd_data={22'b0,channel_busy,3'b0,ready,owned,active};
        16'h000c:rd_data=0;
        16'h0010:rd_data=cookie;
        16'h0014:rd_data=release_epoch;
        16'h0018:rd_data=release_cookie;
        16'h001c:rd_data=format;
        16'h0020:rd_data=mode_tag;
        16'h0024:rd_data=skew_limit;
        16'h0028:rd_data=rejected;
        16'h002c:rd_data=reject_flags;
        16'h0030:rd_data=ticks;
        16'h0034:rd_data=next_epoch;
        16'h0038:rd_data=captured;
        16'h003c:rd_data=bad_pairs;
        16'h0040:rd_data=WIDTH;
        16'h0044:rd_data=HEIGHT;
        16'h0048:rd_data=WIDTH*2;
        16'h004c:rd_data=FRAME_BYTES;
        16'h0438,16'h043c,16'h0478,16'h047c:rd_data=0;
        default:rd_resp=2'b10;
        endcase
    end

    always @(posedge clk)begin
        if(!resetn)begin
            active<=0;active_slot<=0;cancelled<=0;owned<=0;ready<=0;seen<=0;valid_seen<=0;
            ticks<=0;ticks_gray<=0;cookie<=0;release_epoch<=0;release_cookie<=0;format<=0;mode_tag<=0;
            skew_limit<=100000;next_epoch<=1;rejected<=0;reject_flags<=0;captured<=0;bad_pairs<=0;arm<=0;cancel<=0;
        end else begin
            ticks<=ticks+1'b1;ticks_gray<=((ticks+32'd1)>>1)^(ticks+32'd1);arm<=0;cancel<=0;
            if(active)begin
                for(i=0;i<2;i=i+1)if(channel_done[i]&&!seen[i])begin
                    seen[i]<=1;valid_seen[i]<=channel_valid[i];
                end
                if(seen==3&&channel_busy==0)begin
                    active<=0;ready[active_slot]<=1;
                    if(captured!=32'hffffffff)captured<=captured+1'b1;
                    if(pair_error!=0&&bad_pairs!=32'hffffffff)bad_pairs<=bad_pairs+1'b1;
                end
            end
            if(wr_en)begin
                if(rejection!=0)begin
                    if(rejected!=32'hffffffff)rejected<=rejected+1'b1;
                    reject_flags<=reject_flags|rejection;
                end else case(wr_addr)
                16'h000c:case(wr_data)
                    32'h1,32'h101:begin
                        active<=1;active_slot<=selected_slot;owned[selected_slot]<=1;ready[selected_slot]<=0;
                        seen<=0;valid_seen<=0;cancelled<=0;arm<=3;
                        next_epoch<=next_epoch+1'b1;
                    end
                    32'h2:begin cancel<=3;cancelled<=1;end
                    32'h3,32'h103:begin owned[selected_slot]<=0;ready[selected_slot]<=0;end
                    32'h4:reject_flags<=0;
                endcase
                16'h0010:cookie<=wr_data;
                16'h0014:release_epoch<=wr_data;
                16'h0018:release_cookie<=wr_data;
                16'h001c:format<=wr_data;
                16'h0020:mode_tag<=wr_data;
                16'h0024:skew_limit<=wr_data;
                endcase
            end
        end
    end

    // Every descriptor word has one static write destination. The two
    // slot bases and all public read addresses remain unchanged. Capture
    // completion precedes a bus command, matching the original NBA order.
    genvar slot,word_index;
    generate for(slot=0;slot<2;slot=slot+1)begin:SLOT_DESCRIPTOR
        for(word_index=0;word_index<64;word_index=word_index+1)begin:WORD
            localparam ADDRESS=slot*64+word_index;
            if((word_index>=16 && word_index<30) ||
               (word_index>=32 && word_index<46))begin:METADATA
                localparam CHANNEL=word_index>=32?1:0;
                localparam FIELD=word_index-(CHANNEL?32:16);
                always @(posedge clk)begin
                    if(!resetn)descriptor[ADDRESS]<=0;
                    else if(active && active_slot==slot &&
                            channel_done[CHANNEL] && !seen[CHANNEL])
                        descriptor[ADDRESS]<=channel_metadata[CHANNEL*448+FIELD*32+:32];
                end
            end else if(word_index<=12)begin:HEADER
                always @(posedge clk)begin
                    if(!resetn)descriptor[ADDRESS]<=0;
                    else begin
                        if(active && active_slot==slot && seen==3 && channel_busy==0)
                            case(word_index)
                            0:descriptor[ADDRESS]<=pair_error==0?32'h6:32'h2;
                            3:descriptor[ADDRESS]<=pair_error;
                            7:descriptor[ADDRESS]<=skew;
                            11:descriptor[ADDRESS]<=ticks;
                            endcase
                        if(wr_en && rejection==0 && wr_addr==16'h000c &&
                           selected_slot==slot)begin
                            case(wr_data)
                            32'h1,32'h101:case(word_index)
                                0:descriptor[ADDRESS]<=1;
                                1:descriptor[ADDRESS]<=next_epoch;
                                2:descriptor[ADDRESS]<=cookie;
                                3:descriptor[ADDRESS]<=0;
                                4:descriptor[ADDRESS]<=format;
                                5:descriptor[ADDRESS]<=mode_tag;
                                6:descriptor[ADDRESS]<=skew_limit;
                                7:descriptor[ADDRESS]<=0;
                                8:descriptor[ADDRESS]<=selected_slot?32'h18200000:32'h18000000;
                                9:descriptor[ADDRESS]<=selected_slot?32'h18300000:32'h18100000;
                                10:descriptor[ADDRESS]<=FRAME_BYTES;
                                11:descriptor[ADDRESS]<=0;
                                12:descriptor[ADDRESS]<=ticks;
                            endcase
                            32'h3,32'h103:if(word_index==0)descriptor[ADDRESS]<=0;
                            endcase
                        end
                    end
                end
            end else begin:RESERVED
                // These words were only assigned by reset in the original.
                always @(posedge clk)if(!resetn)descriptor[ADDRESS]<=0;
            end
        end
    end endgenerate
endmodule
`default_nettype wire
