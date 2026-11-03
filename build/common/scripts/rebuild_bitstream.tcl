if {![info exists ::env(SNAPSHOT_BITSTREAM_ADMITTED)] || $::env(SNAPSHOT_BITSTREAM_ADMITTED) ne "1"} {error "Guarded bitstream entry required"}
set root [file normalize [file join [file dirname [info script]] ..]]
set_param general.maxThreads 1
source inputs.tcl
open_checkpoint $inspected_checkpoint
if {[get_property PART [current_design]] ne "xc7z020clg400-2"} {error "Wrong part"}
if {[llength [get_cells -quiet -hier -filter {IS_BLACKBOX == 1}]]} {error "Unresolved hardware"}
if {[llength [get_clocks clk_fpga_0]] != 1 || [get_property PERIOD [get_clocks clk_fpga_0]] != 10.0} {error "Wrong compute clock"}
write_bitstream -force engineering.bit
puts SNAPSHOT_REBUILT_BITSTREAM_UNDEPLOYED
exit
