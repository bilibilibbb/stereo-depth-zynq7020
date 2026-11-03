// Derived from reference/camera_rtl/retained_platform_top.v: remove HDMI only.
`timescale 1ns/1ps
module snapshot_platform_top(
inout wire [14:0] DDR_addr,
inout wire [2:0] DDR_ba,
inout wire DDR_cas_n,
inout wire DDR_ck_n,
inout wire DDR_ck_p,
inout wire DDR_cke,
inout wire DDR_cs_n,
inout wire [3:0] DDR_dm,
inout wire [31:0] DDR_dq,
inout wire [3:0] DDR_dqs_n,
inout wire [3:0] DDR_dqs_p,
inout wire DDR_odt,
inout wire DDR_ras_n,
inout wire DDR_reset_n,
inout wire DDR_we_n,
inout wire FIXED_IO_ddr_vrn,
inout wire FIXED_IO_ddr_vrp,
inout wire [53:0] FIXED_IO_mio,
inout wire FIXED_IO_ps_clk,
inout wire FIXED_IO_ps_porb,
inout wire FIXED_IO_ps_srstb,
input wire clk_50,
input wire cam0_pclk,
input wire cam0_vs,
input wire cam0_href,
input wire [7:0] cam0_data,
input wire cam1_pclk,
input wire cam1_vs,
input wire cam1_href,
input wire [7:0] cam1_data,
inout wire cam0_scl,
inout wire cam0_sda,
inout wire cam1_scl,
inout wire cam1_sda,
inout wire cam_reset_n,
inout wire cam_pwdn,
input wire cam_xclk
);
wire raw,board_clk,pclk0,pclk1;wire pclk0_raw,pclk1_raw;
IBUF input_clk(.I(clk_50),.O(raw));BUFG global_clk(.I(raw),.O(board_clk));
IBUF ib0(.I(cam0_pclk),.O(pclk0_raw));BUFG gb0(.I(pclk0_raw),.O(pclk0));
IBUF ib1(.I(cam1_pclk),.O(pclk1_raw));BUFG gb1(.I(pclk1_raw),.O(pclk1));
wire board_resetn,ps_fclk_resetn,compute_clk,compute_resetn;
camera_platform_reset compute_release(.clk(compute_clk),.ps_resetn(board_resetn),.resetn(compute_resetn));
camera_platform_reset reset_control(.clk(board_clk),.ps_resetn(ps_fclk_resetn),.resetn(board_resetn));
wire scl0_low,sda0_low,scl1_low,sda1_low,camera_reset_low,camera_pwdn_low;
assign cam0_scl=scl0_low?1'b0:1'bz;
assign cam0_sda=sda0_low?1'b0:1'bz;
assign cam1_scl=scl1_low?1'b0:1'bz;
assign cam1_sda=sda1_low?1'b0:1'bz;
// EBF410251 R3 pulls RESET down: release is not a high level.
// Explicit input buffer preserves actual pad observation, not command echo.
wire camera_reset_pad_in;
(* DONT_TOUCH = "TRUE" *) IOBUF camera_reset_iobuf(
 .I(~camera_reset_low), .T(1'b0), .O(camera_reset_pad_in), .IO(cam_reset_n));
assign cam_pwdn=camera_pwdn_low?1'b0:1'bz;

// Preserve physical VS pads for passive boundary scan / future ILA; neither
// physical VS pad drives the capture path in this reviewed recovery build.
(* MARK_DEBUG="TRUE", KEEP="TRUE" *) wire raw_vs0,raw_vs1;
(* DONT_TOUCH="TRUE" *) IBUF raw_vs0_input(.I(cam0_vs),.O(raw_vs0));
(* DONT_TOUCH="TRUE" *) IBUF raw_vs1_input(.I(cam1_vs),.O(raw_vs1));
wire recovered_vs0,recovered_vs1,recovered_href0,recovered_href1;
wire [7:0] recovered_data0,recovered_data1;
dvp_href_frame_v2 frame_sync0(.arst_n(board_resetn),.pclk(pclk0),.href(cam0_href),.data(cam0_data),
 .frame_vs(recovered_vs0),.delayed_href(recovered_href0),.delayed_data(recovered_data0));
dvp_href_frame_v2 frame_sync1(.arst_n(board_resetn),.pclk(pclk1),.href(cam1_href),.data(cam1_data),
 .frame_vs(recovered_vs1),.delayed_href(recovered_href1),.delayed_data(recovered_data1));

snapshot_bd_wrapper bd(.compute_clk(compute_clk),.compute_resetn(compute_resetn),.DDR_addr(DDR_addr),.DDR_ba(DDR_ba),.DDR_cas_n(DDR_cas_n),.DDR_ck_n(DDR_ck_n),.DDR_ck_p(DDR_ck_p),.DDR_cke(DDR_cke),.DDR_cs_n(DDR_cs_n),.DDR_dm(DDR_dm),.DDR_dq(DDR_dq),.DDR_dqs_n(DDR_dqs_n),.DDR_dqs_p(DDR_dqs_p),.DDR_odt(DDR_odt),.DDR_ras_n(DDR_ras_n),.DDR_reset_n(DDR_reset_n),.DDR_we_n(DDR_we_n),.FIXED_IO_ddr_vrn(FIXED_IO_ddr_vrn),.FIXED_IO_ddr_vrp(FIXED_IO_ddr_vrp),.FIXED_IO_mio(FIXED_IO_mio),.FIXED_IO_ps_clk(FIXED_IO_ps_clk),.FIXED_IO_ps_porb(FIXED_IO_ps_porb),.FIXED_IO_ps_srstb(FIXED_IO_ps_srstb),.board_clk(board_clk),.board_resetn(board_resetn),.ps_fclk_resetn(ps_fclk_resetn),.pclk0(pclk0),.pclk1(pclk1),.scl0_low(scl0_low),.sda0_low(sda0_low),.scl1_low(scl1_low),.sda1_low(sda1_low),.camera_reset_low(camera_reset_low),.camera_pwdn_low(camera_pwdn_low),.vs0(recovered_vs0),.href0(recovered_href0),.data0(recovered_data0),.scl0(cam0_scl),.sda0(cam0_sda),.vs1(recovered_vs1),.href1(recovered_href1),.data1(recovered_data1),.scl1(cam1_scl),.sda1(cam1_sda),.camera_reset_level(camera_reset_pad_in),.camera_pwdn_level(cam_pwdn),.camera_xclk_level(cam_xclk));
endmodule

