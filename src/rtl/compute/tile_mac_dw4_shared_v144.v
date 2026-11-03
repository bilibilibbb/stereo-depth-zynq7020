`timescale 1ns/1ps
`default_nettype none
// 128 signed INT16 lanes. Product register separates multiplication from P feedback.
// 16384 * (-32768)^2 = 2^44, safely inside signed 48-bit accumulation.
// A result is published only after the last accepted product has committed.
module tile_mac_dw4_shared_v144(
    input wire clk,rst_n,cancel,start,
    input wire [15:0] terms,input wire start_dw,start_dw_pair,
    input wire[2:0]start_dw_groups,
    input wire[575:0]feed_dw_weights,feed_dw_pixels0,feed_dw_pixels1,
    output wire start_ready,
    input wire feed_valid,
    output wire feed_ready,
    input wire [127:0] feed_a,
    input wire [255:0] feed_b,
    output reg result_valid,
    input wire result_ready,
    input wire [7:0] result_index,
    output wire [63:0] result_data,
    output wire result_index_fault,
    input wire [6:0] result_row,input wire row_advance,
    output wire [767:0] row_data,
    output wire result_row_fault,
    output reg bad_config,
    output reg [15:0] accepted_terms,committed_terms
);
    reg active,operand_valid,operand_last,product_valid,product_last;
    reg dw_mode,dw_pair;reg[2:0]dw_groups;reg[575:0]operand_dw0,operand_dw1,operand_dw_weights;
    reg[6:0]operand_dw_index,product_dw_index,partial_dw_index,half_dw_index;
    reg partial_dw_valid,partial_dw_last,half_dw_valid,half_dw_last;
    reg dw_result_valid,dw_result_last;reg[6:0]dw_result_index;
    (* use_dsp="no" *) reg signed[32:0]dw_partial[0:39];(* use_dsp="no" *) reg signed[33:0]dw_half[0:23];
    (* use_dsp="no" *) wire signed[35:0]dw_total0[0:3],dw_total1[0:3];
    genvar dg;
    generate for(dg=0;dg<4;dg=dg+1)begin:DW_SUM
     assign dw_total0[dg]={{2{dw_half[dg*6][33]}},dw_half[dg*6]}+{{2{dw_half[dg*6+1][33]}},dw_half[dg*6+1]}+{{2{dw_half[dg*6+2][33]}},dw_half[dg*6+2]};
     assign dw_total1[dg]={{2{dw_half[dg*6+3][33]}},dw_half[dg*6+3]}+{{2{dw_half[dg*6+4][33]}},dw_half[dg*6+4]}+{{2{dw_half[dg*6+5][33]}},dw_half[dg*6+5]};
    end endgenerate
    reg accumulator_clear;
    reg [127:0] operand_a;
    reg [255:0] operand_b;
    reg [15:0] target;reg[6:0] head_row;
    wire signed [31:0] product[0:127];
    wire signed [47:0] acc[0:127];
    // Only DW uses these two scalar banks. No new multipliers or row cache.
    // Each completed term overwrites its even/odd address before publication.
    wire signed[35:0]dw_selected[0:3];wire signed[47:0]dw_selected48[0:3];
    // Each group has one write address. The previous combined arrays had
    // four simultaneous writes and Vivado expanded them into registers.
    generate for(dg=0;dg<4;dg=dg+1)begin:DW_READ
     (* ram_style="distributed" *) reg signed[35:0]even_bank[0:31],odd_bank[0:31];
     always @(posedge clk)begin
      if(rst_n && !cancel && half_dw_valid && dg<dw_groups)begin
       if(!half_dw_index[0])even_bank[half_dw_index[5:1]]<=dw_total0[dg];
       if(dw_pair || half_dw_index[0])odd_bank[half_dw_index[5:1]]<=dw_pair?dw_total1[dg]:dw_total0[dg];
      end
     end
     assign dw_selected[dg]=head_row[0]?odd_bank[head_row[5:1]]:even_bank[head_row[5:1]];
     assign dw_selected48[dg]={{12{dw_selected[dg][35]}},dw_selected[dg]};
    end endgenerate
    wire accept=feed_valid && feed_ready;
    assign start_ready=!active && !operand_valid && !product_valid && !dw_result_valid && !partial_dw_valid && !half_dw_valid && !result_valid && !cancel;
    assign feed_ready=active && !cancel;
    assign result_index_fault=result_valid && (result_index>=128 || result_index/16!=head_row);
    assign result_data=result_valid && !result_index_fault?(dw_mode?(result_index[3:0]<dw_groups?{{16{dw_selected48[result_index[1:0]][47]}},dw_selected48[result_index[1:0]]}:64'd0):{{16{acc[result_index%16][47]}},acc[result_index%16]}):64'b0;
    assign result_row_fault=result_valid && (result_row>=(dw_mode?64:8) || result_row!=head_row);
    integer i,dw_r,dw_t;
    genvar r,c;
    generate for(c=0;c<16;c=c+1)begin:ROW_READ
        assign row_data[c*48+:48]=result_valid && !result_row_fault?(dw_mode?(c<dw_groups && c<4?dw_selected48[c%4]:48'b0):acc[c]):48'b0;
    end
    // UG479 tables2-7/8/9: M+P=0100101, M=0000101, C=0110000.
    // MREG thenPREG exactly replace dense product thenaccumulator registers.
    // DW reads the product fromP rather than an external M tap; add one DW
    // metadata stage before the unchanged signed reduction. Denseacceptance
    // andfinalcommit timing are unchanged. All128 lanes physically shareMAC.
    wire shift_acc=result_valid && row_advance && !result_row_fault && head_row<7 && !dw_mode;
    wire reset_mac=!rst_n || cancel;
    for(r=0;r<8;r=r+1)begin:ROW
        for(c=0;c<16;c=c+1)begin:COL
            wire selected_dw=dw_mode && c<9 && r/2<dw_groups;
            wire signed[15:0]pixel;
            wire signed[15:0]weight;
            if(c<9)begin:SHARED_OPERANDS
                wire[15:0]tap_pixel=r%2==0?operand_dw0[(r/2)*144+c*16+:16]:operand_dw1[(r/2)*144+c*16+:16];
                wire[15:0]tap_weight=operand_dw_weights[(r/2)*144+c*16+:16];
                assign pixel=dw_mode?$signed(tap_pixel):$signed(operand_a[r*16+:16]);
                assign weight=dw_mode?$signed(tap_weight):$signed(operand_b[c*16+:16]);
            end else begin:DENSE_OPERANDS
                assign pixel=$signed(operand_a[r*16+:16]);assign weight=$signed(operand_b[c*16+:16]);
            end
            wire[47:0]shift_source;
            if(r<7)assign shift_source=acc[(r+1)*16+c];else assign shift_source=48'd0;
            wire[6:0]operation=shift_acc?7'b0110000:(dw_mode?7'b0000101:7'b0100101);
            wire multiply_enable=operand_valid && (!dw_mode || selected_dw);
            wire p_enable=shift_acc || (product_valid && (!dw_mode || selected_dw));
            DSP48E1 #(.A_INPUT("DIRECT"),.B_INPUT("DIRECT"),.USE_DPORT("FALSE"),
                .USE_MULT("MULTIPLY"),.USE_SIMD("ONE48"),.AREG(0),.ACASCREG(0),
                .BREG(0),.BCASCREG(0),.ADREG(0),.DREG(0),.MREG(1),.PREG(1),
                .CREG(0),.ALUMODEREG(0),.CARRYINREG(0),.CARRYINSELREG(0),
                .INMODEREG(0),.OPMODEREG(0)) mac(
                .CLK(clk),.A({{14{pixel[15]}},pixel}),.B({{2{weight[15]}},weight}),
                .C(shift_source),.D(25'd0),.ACIN(30'd0),.BCIN(18'd0),.PCIN(48'd0),
                .ALUMODE(4'd0),.CARRYIN(1'b0),.CARRYINSEL(3'd0),.CARRYCASCIN(1'b0),
                .MULTSIGNIN(1'b0),.INMODE(5'd0),.OPMODE(operation),
                .CEA1(1'b0),.CEA2(1'b0),.CEB1(1'b0),.CEB2(1'b0),.CEAD(1'b0),.CED(1'b0),
                .CEC(1'b0),.CEALUMODE(1'b0),.CECARRYIN(1'b0),.CECTRL(1'b0),.CEINMODE(1'b0),
                .CEM(multiply_enable),.CEP(p_enable),
                .RSTA(reset_mac),.RSTB(reset_mac),.RSTC(reset_mac),.RSTD(reset_mac),
                .RSTM(reset_mac),.RSTP(reset_mac || (accumulator_clear && !dw_mode)),
                .RSTALUMODE(reset_mac),.RSTALLCARRYIN(reset_mac),.RSTCTRL(reset_mac),.RSTINMODE(reset_mac),
                .P(acc[r*16+c]),.ACOUT(),.BCOUT(),.PCOUT(),.CARRYCASCOUT(),.CARRYOUT(),
                .MULTSIGNOUT(),.OVERFLOW(),.UNDERFLOW(),.PATTERNDETECT(),.PATTERNBDETECT());
            assign product[r*16+c]=acc[r*16+c][31:0];
        end
    end
 endgenerate
    always @(posedge clk)begin
        if(!rst_n)begin
            dw_mode<=0;dw_pair<=0;dw_groups<=0;operand_dw_weights<=0;operand_dw0<=0;operand_dw1<=0;operand_dw_index<=0;product_dw_index<=0;partial_dw_index<=0;half_dw_index<=0;
            partial_dw_valid<=0;partial_dw_last<=0;half_dw_valid<=0;half_dw_last<=0;dw_result_valid<=0;dw_result_last<=0;dw_result_index<=0;
            accumulator_clear<=0;active<=0;operand_valid<=0;operand_last<=0;product_valid<=0;product_last<=0;target<=0;head_row<=0;
            result_valid<=0;bad_config<=0;accepted_terms<=0;committed_terms<=0;

        end else if(cancel)begin
            dw_mode<=0;dw_pair<=0;dw_groups<=0;operand_dw_weights<=0;operand_dw0<=0;operand_dw1<=0;operand_dw_index<=0;product_dw_index<=0;partial_dw_index<=0;half_dw_index<=0;
            partial_dw_valid<=0;partial_dw_last<=0;half_dw_valid<=0;half_dw_last<=0;dw_result_valid<=0;dw_result_last<=0;dw_result_index<=0;
            accumulator_clear<=0;active<=0;operand_valid<=0;operand_last<=0;product_valid<=0;product_last<=0;result_valid<=0;
            accepted_terms<=0;committed_terms<=0;bad_config<=0;head_row<=0;
        end else begin
            accumulator_clear<=start && start_ready;

            operand_valid<=accept;
            product_valid<=operand_valid;product_last<=operand_last;
            product_dw_index<=operand_dw_index;
            dw_result_valid<=product_valid && dw_mode;dw_result_last<=product_last;dw_result_index<=product_dw_index;
            partial_dw_valid<=dw_result_valid;partial_dw_last<=dw_result_last;partial_dw_index<=dw_result_index;
            half_dw_valid<=partial_dw_valid;half_dw_last<=partial_dw_last;half_dw_index<=partial_dw_index;
            if(dw_result_valid && dw_mode)begin
                for(dw_r=0;dw_r<8;dw_r=dw_r+1)begin
                    for(dw_t=0;dw_t<4;dw_t=dw_t+1)
                        dw_partial[dw_r*5+dw_t]<=$signed({product[dw_r*16+dw_t*2][31],product[dw_r*16+dw_t*2]})+
                            $signed({product[dw_r*16+dw_t*2+1][31],product[dw_r*16+dw_t*2+1]});
                    dw_partial[dw_r*5+4]<={product[dw_r*16+8][31],product[dw_r*16+8]};
                end
            end
            if(partial_dw_valid)begin
                for(dw_r=0;dw_r<8;dw_r=dw_r+1)begin
                    dw_half[dw_r*3]<=$signed({dw_partial[dw_r*5][32],dw_partial[dw_r*5]})+$signed({dw_partial[dw_r*5+1][32],dw_partial[dw_r*5+1]});
                    dw_half[dw_r*3+1]<=$signed({dw_partial[dw_r*5+2][32],dw_partial[dw_r*5+2]})+$signed({dw_partial[dw_r*5+3][32],dw_partial[dw_r*5+3]});
                    dw_half[dw_r*3+2]<={dw_partial[dw_r*5+4][32],dw_partial[dw_r*5+4]};
                end
            end
            if(half_dw_valid)begin
                committed_terms<=committed_terms+1'b1;
                if(half_dw_last)result_valid<=1;
            end
            if(result_valid && result_ready)result_valid<=0;
            if(start && start_ready)begin
                dw_mode<=start_dw;dw_pair<=start_dw_pair;dw_groups<=start_dw_groups;bad_config<=terms==0 || terms>16384 || (start_dw && (terms>64 || start_dw_groups==0 || start_dw_groups>4));
                active<=terms!=0 && terms<=16384 && (!start_dw || (terms<=64 && start_dw_groups>=1 && start_dw_groups<=4));target<=terms;
                accepted_terms<=0;committed_terms<=0;head_row<=0;operand_last<=0;product_last<=0;
                // The registered clear occurs before the earliest product commit.
            end
            if(accept)begin
                operand_a<=feed_a;operand_b<=feed_b;operand_dw_weights<=feed_dw_weights;
                operand_dw0<=feed_dw_pixels0;operand_dw1<=feed_dw_pixels1;
                operand_dw_index<=accepted_terms*(dw_pair?2:1);
                accepted_terms<=accepted_terms+1'b1;
                operand_last<=accepted_terms+1'b1==target;
                if(accepted_terms+1'b1==target)active<=0;
            end
            // A row is shifted only after the owner has committed its read.
            // P may load the adjacent row through the DSP C input; no8:1 P mux.
            if(result_valid && row_advance && !result_row_fault && head_row<(dw_mode?63:7))begin


                head_row<=head_row+1'b1;
            end
            if(product_valid && !dw_mode)begin

                committed_terms<=committed_terms+1'b1;
                if(product_last)result_valid<=1;
            end
        end
    end
endmodule
`default_nettype wire
