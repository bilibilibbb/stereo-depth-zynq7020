set physical_operation {place_and_report_only}
set logic_optimization {}
set inspected_primitive_counts [dict create DSP48E1 170 RAMB36E1 38 RAMB18E1 116 PS7 1 IOBUF 6]
set inspected_netlist [file join $root {build/20261027T171857_092967Z/physical_placement/snapshot_platform_top.edf}]
set inspected_ps_xdc [file join $root {build/20261027T171305_443192Z/area/generated/snapshot_bd/ip/snapshot_bd_ps_0/snapshot_bd_ps_0.xdc}]
set xpm_constraint_files [dict create {xpm_cdc_array_single} {D:/vivado_18.3/Vivado/2018.3/data/ip/xpm/xpm_cdc/tcl/xpm_cdc_array_single.tcl} {xpm_cdc_async_rst} {D:/vivado_18.3/Vivado/2018.3/data/ip/xpm/xpm_cdc/tcl/xpm_cdc_async_rst.tcl} {xpm_cdc_gray} {D:/vivado_18.3/Vivado/2018.3/data/ip/xpm/xpm_cdc/tcl/xpm_cdc_gray.tcl} {xpm_cdc_handshake} {D:/vivado_18.3/Vivado/2018.3/data/ip/xpm/xpm_cdc/tcl/xpm_cdc_handshake.tcl} {xpm_cdc_low_latency_handshake} {D:/vivado_18.3/Vivado/2018.3/data/ip/xpm/xpm_cdc/tcl/xpm_cdc_low_latency_handshake.tcl} {xpm_cdc_single} {D:/vivado_18.3/Vivado/2018.3/data/ip/xpm/xpm_cdc/tcl/xpm_cdc_single.tcl} {xpm_cdc_sync_rst} {D:/vivado_18.3/Vivado/2018.3/data/ip/xpm/xpm_cdc/tcl/xpm_cdc_sync_rst.tcl}]
