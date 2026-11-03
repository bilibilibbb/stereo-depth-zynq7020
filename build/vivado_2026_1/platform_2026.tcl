# Complete snapshot BD source. Run only through the admitted guarded build.
# No synthesis, implementation, bitstream, download or board reset here.
if {![info exists ::env(SNAPSHOT_2026_PLATFORM_ADMITTED)] || $::env(SNAPSHOT_2026_PLATFORM_ADMITTED) ne "1"} {
 error "Use run_2026.py platform with its source binding and memory guard"
}
set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root hardware generated_contract.tcl]
if {[version -short] ne "2026.1"} {error "This platform port requires Vivado 2026.1"}
source [file join $root modern ip_catalog.tcl]
set f [open tool_version.txt w]
puts $f [version]
close $f
set_param general.maxThreads 1
create_project snapshot ./project -part xc7z020clg400-2
# Initialize IP Integrator before HDL parsing opens auxiliary tool interpreters.
# Let the installed tool own its XGUI initialization; no manual preload/stub.
create_bd_design snapshot_bd
set_property target_language Verilog [current_project]
set_property source_mgmt_mode All [current_project]
add_files $snapshot_rtl
foreach p $snapshot_rtl {set_property FILE_TYPE SystemVerilog [get_files $p]}
# Preserve the existing Verilog module-reference interfaces. These three
# interface wrappers use Verilog2001 syntax; their computational children
# remain SystemVerilog. Do not rewrite or duplicate frozen arithmetic.
foreach p {hardware/rtl/snapshot_compute_bdif.sv reference/camera_rtl/learning_capture.v reference/camera_rtl/camera_ddr_bdif.v} {
 set_property FILE_TYPE Verilog [get_files [file join $root $p]]
}
set_property top snapshot_compute_bdif [current_fileset]
update_compile_order -fileset sources_1
check_syntax -fileset sources_1
set ps [create_bd_cell -type ip -vlnv [snapshot_2026_ip processing_system7] ps]
set_property -dict $snapshot_ps $ps
set compute_mhz [expr {$snapshot_compute_hz/1000000.0}]
set_property -dict [list CONFIG.PCW_USE_M_AXI_GP0 1 CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ $compute_mhz CONFIG.PCW_EN_CLK0_PORT 1 CONFIG.PCW_EN_RST0_PORT 1] $ps
foreach n {0 1 2 3} {set_property CONFIG.PCW_USE_S_AXI_HP$n 1 $ps}
foreach n {0 1 2 3} {set_property CONFIG.PCW_S_AXI_HP${n}_DATA_WIDTH 64 $ps}
foreach name {DDR FIXED_IO} {
 set before [get_bd_intf_ports -quiet]
 make_bd_intf_pins_external [get_bd_intf_pins ps/$name]
 set made {}
 foreach p [get_bd_intf_ports] {if {[lsearch -exact $before $p]<0} {lappend made $p}}
 if {[llength $made]!=1} {error "PS external interface count: $name"}
 set_property name $name $made
}
create_bd_port -dir I -type clk board_clk
set_property CONFIG.FREQ_HZ 50000000 [get_bd_ports board_clk]
create_bd_port -dir O -type clk compute_clk
create_bd_port -dir I -type rst board_resetn
create_bd_port -dir I -type rst compute_resetn
create_bd_port -dir O -type rst ps_fclk_resetn
foreach p {board_resetn compute_resetn} {set_property CONFIG.POLARITY ACTIVE_LOW [get_bd_ports $p]}
set_property CONFIG.ASSOCIATED_RESET board_resetn [get_bd_ports board_clk]
set_property CONFIG.ASSOCIATED_RESET compute_resetn [get_bd_ports compute_clk]
connect_bd_net [get_bd_pins ps/FCLK_CLK0] [get_bd_ports compute_clk]
connect_bd_net [get_bd_pins ps/FCLK_RESET0_N] [get_bd_ports ps_fclk_resetn]
foreach {name module} {compute snapshot_compute_bdif sensor learning_capture capture_ddr camera_ddr_bdif} {
 create_bd_cell -type module -reference $module $name
}
set_property CONFIG.SGM_BUILD_ID $snapshot_build [get_bd_cells compute]
set_property -dict [list CONFIG.BUILD_ID [expr {$snapshot_build}] CONFIG.PROFILE_SGM 1 CONFIG.CAPTURE_ENABLED 1] [get_bd_cells sensor]
create_bd_cell -type ip -vlnv [snapshot_2026_ip axi_interconnect] control
set_property -dict [list CONFIG.NUM_SI 1 CONFIG.NUM_MI 4] [get_bd_cells control]
foreach {name count} {compute_memory 1 fp_memory 1 sgm_memory 1 camera_memory 2} {
 create_bd_cell -type ip -vlnv [snapshot_2026_ip axi_interconnect] $name
 set_property -dict [list CONFIG.NUM_SI $count CONFIG.NUM_MI 1] [get_bd_cells $name]
}
# All selected real producers split at 16 beats and 4 KiB, at the same 64-bit width
# as the HP port. Keep the vendor ID/WID/order logic but omit long-burst
# splitting. The admitted platform runner checks the actual DMA sources.
foreach name {compute_protocol fp_protocol sgm_protocol camera_protocol} {
 create_bd_cell -type ip -vlnv [snapshot_2026_ip axi_protocol_converter] $name
 set_property -dict [list CONFIG.SI_PROTOCOL AXI4 CONFIG.MI_PROTOCOL AXI3 CONFIG.DATA_WIDTH 64 CONFIG.TRANSLATION_MODE 0] [get_bd_cells $name]
}
proc link {a b} {connect_bd_intf_net [get_bd_intf_pins $a] [get_bd_intf_pins $b]}
proc pin {a b} {connect_bd_net [get_bd_pins $a] [get_bd_pins $b]}
proc fromport {a b} {connect_bd_net [get_bd_ports $a] [get_bd_pins $b]}
link ps/M_AXI_GP0 control/S00_AXI
foreach {index slave} {M00 compute/S_AXI M01 compute/SGM_AXI M02 sensor/S_AXI M03 capture_ddr/S_AXI} {link control/${index}_AXI $slave}
foreach {master path hp} {M0_AXI compute 0 M1_AXI fp 1 M2_AXI sgm 3} {
 link compute/$master ${path}_memory/S00_AXI
 link ${path}_memory/M00_AXI ${path}_protocol/S_AXI
 link ${path}_protocol/M_AXI ps/S_AXI_HP$hp
 pin ps/FCLK_CLK0 ${path}_protocol/aclk
 fromport compute_resetn ${path}_protocol/aresetn
 pin ps/FCLK_CLK0 ps/S_AXI_HP${hp}_ACLK
}
link capture_ddr/M0_AXI camera_memory/S00_AXI
link capture_ddr/M1_AXI camera_memory/S01_AXI
link camera_memory/M00_AXI camera_protocol/S_AXI
link camera_protocol/M_AXI ps/S_AXI_HP2
fromport board_clk camera_protocol/aclk
fromport board_resetn camera_protocol/aresetn
pin ps/FCLK_CLK0 compute/clk
fromport compute_resetn compute/resetn
foreach c {sensor capture_ddr} {fromport board_clk $c/clk;fromport board_resetn $c/resetn}
foreach p {ps/M_AXI_GP0_ACLK ps/S_AXI_HP2_ACLK} {fromport board_clk $p}
foreach {cell prefixes domain} {compute_memory {S00 M00} compute fp_memory {S00 M00} compute sgm_memory {S00 M00} compute camera_memory {S00 S01 M00} board control {S00 M00 M01 M02 M03} board} {
 if {$domain eq "compute"} {pin ps/FCLK_CLK0 $cell/ACLK;fromport compute_resetn $cell/ARESETN} else {fromport board_clk $cell/ACLK;fromport board_resetn $cell/ARESETN}
 foreach p $prefixes {
  if {$domain eq "compute" || ($cell eq "control" && $p in {M00 M01})} {
   pin ps/FCLK_CLK0 $cell/${p}_ACLK;fromport compute_resetn $cell/${p}_ARESETN
  } else {fromport board_clk $cell/${p}_ACLK;fromport board_resetn $cell/${p}_ARESETN}
 }
}
foreach {name width dir} $snapshot_camera_ports {
 if {$width==1} {create_bd_port -dir $dir $name} else {create_bd_port -dir $dir -from [expr {$width-1}] -to 0 $name}
 fromport $name sensor/$name
 if {$name in {pclk0 pclk1 vs0 vs1 href0 href1 data0 data1}} {fromport $name capture_ddr/$name}
}
foreach {slave base} $snapshot_addresses {
 set segment [get_bd_addr_segs $slave/reg0]
 if {[llength $segment]!=1} {error "Ambiguous register segment $slave"}
 create_bd_addr_seg -range 64K -offset $base [get_bd_addr_spaces ps/Data] $segment SEG_[string map {/ _} $slave]
}
foreach {master hp} {compute/M0_AXI 0 compute/M1_AXI 1 compute/M2_AXI 3 capture_ddr/M0_AXI 2 capture_ddr/M1_AXI 2} {
 create_bd_addr_seg -range 1G -offset 0 [get_bd_addr_spaces $master] [get_bd_addr_segs ps/S_AXI_HP$hp/HP${hp}_DDR_LOWOCM] SEG_DDR
}
validate_bd_design
if {abs([get_property CONFIG.PCW_ACT_FPGA0_PERIPHERAL_FREQMHZ $ps]-$compute_mhz)>0.01} {error "Unexpected compute clock"}
if {[get_property CONFIG.PCW_ENET0_PERIPHERAL_ENABLE $ps]!=1 || [get_property CONFIG.PCW_ENET0_GRP_MDIO_ENABLE $ps]!=1} {error "Ethernet profile was not applied"}
save_bd_design
write_bd_tcl -force snapshot_bd_export.tcl
set f [open platform_ps.tsv w]
foreach k [lsort [list_property $ps]] {if {[string match CONFIG.PCW_* $k]} {puts $f "$k\t[get_property $k $ps]"}}
close $f
set f [open address_map.tsv w]
foreach s [get_bd_addr_segs -hier] {puts $f "$s\t[get_property OFFSET $s]\t[get_property RANGE $s]"}
close $f
set f [open clock_reset_map.tsv w]
foreach p [get_bd_pins -hier -filter {TYPE == clk || TYPE == rst}] {puts $f "$p\t[get_bd_nets -of_objects $p]"}
close $f
set bd [get_files */snapshot_bd.bd]
set_property synth_checkpoint_mode Hierarchical $bd
generate_target all $bd
set wrappers [make_wrapper -files $bd -top]
if {[llength $wrappers] != 1} {error "Expected one generated BD wrapper"}
add_files $wrappers
set_property top snapshot_platform_top [current_fileset]
update_compile_order -fileset sources_1
check_syntax -fileset sources_1
set f [open resolved_ips.tsv w]
foreach cell [get_bd_cells -hier -filter {TYPE == ip}] {
 puts $f "$cell\t[get_property VLNV $cell]"
}
close $f
write_hw_platform -fixed -force snapshot.xsa
foreach usage {synthesis simulation} {
 set f [open ${usage}_compile_order.tsv w]
 foreach p [get_files -compile_order sources -used_in $usage] {puts $f "$p\t[get_property FILE_TYPE $p]\t[get_property LIBRARY $p]"}
 close $f
}
close_project
puts SNAPSHOT_2026_PLATFORM_XSA_GENERATED
exit
