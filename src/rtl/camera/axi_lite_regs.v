`timescale 1ns/1ps
// One outstanding read and write. AW and W may arrive in either order.
module axi_lite_regs (
    input wire clk, input wire resetn,
    input wire [31:0] s_awaddr, input wire s_awvalid, output wire s_awready,
    input wire [31:0] s_wdata, input wire [3:0] s_wstrb,
    input wire s_wvalid, output wire s_wready,
    output reg [1:0] s_bresp, output reg s_bvalid, input wire s_bready,
    input wire [31:0] s_araddr, input wire s_arvalid, output wire s_arready,
    output reg [31:0] s_rdata, output reg [1:0] s_rresp,
    output reg s_rvalid, input wire s_rready,
    output wire wr_en, output reg [31:0] wr_addr, output reg [31:0] wr_data,
    output reg [3:0] wr_strb, input wire [1:0] wr_resp,
    output wire [31:0] rd_addr, input wire [31:0] rd_data, input wire [1:0] rd_resp
);
    reg have_aw, have_w;
    assign s_awready = resetn && !have_aw && !s_bvalid;
    assign s_wready = resetn && !have_w && !s_bvalid;
    assign wr_en = have_aw && have_w && !s_bvalid;
    assign s_arready = resetn && !s_rvalid;
    assign rd_addr = s_araddr;
    always @(posedge clk) begin
        if (!resetn) begin
            have_aw<=0; have_w<=0; wr_addr<=0; wr_data<=0; wr_strb<=0;
            s_bvalid<=0; s_bresp<=0; s_rvalid<=0; s_rdata<=0; s_rresp<=0;
        end else begin
            if (s_awvalid && s_awready) begin wr_addr<=s_awaddr; have_aw<=1; end
            if (s_wvalid && s_wready) begin wr_data<=s_wdata; wr_strb<=s_wstrb; have_w<=1; end
            if (wr_en) begin
                have_aw<=0; have_w<=0; s_bvalid<=1; s_bresp<=wr_resp;
            end else if (s_bvalid && s_bready) s_bvalid<=0;
            if (s_arvalid && s_arready) begin
                s_rdata<=rd_data; s_rresp<=rd_resp; s_rvalid<=1;
            end else if (s_rvalid && s_rready) s_rvalid<=0;
        end
    end
endmodule
