# Route the exact reviewed placed checkpoint. Its existing constraints persist.
if {![info exists ::env(SNAPSHOT_PHYSICAL_ROUTE_ADMITTED)] || $::env(SNAPSHOT_PHYSICAL_ROUTE_ADMITTED) ne "1"} {error "Guarded physical route required"}
set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root hardware generated_contract.tcl]
set_param general.maxThreads 1
source inputs.tcl
open_checkpoint $inspected_checkpoint
if {[get_property PART [current_design]] ne "xc7z020clg400-2"} {error "Wrong placed part"}
if {[llength [get_cells -quiet -hier -filter {IS_BLACKBOX == 1}]]} {error "Unresolved placed netlist"}
foreach {primitive expected} $inspected_primitive_counts {
    if {[llength [get_cells -quiet -hier -filter "REF_NAME == $primitive"]] != $expected} {error "Current primitive mismatch: $primitive"}
}
foreach {name period} [list clk_fpga_0 [expr {1000000000.0/$snapshot_compute_hz}] sys_clk 20.0 camera0_clk 16.667 camera1_clk 16.667] {
    set c [get_clocks -quiet $name]
    if {[llength $c] != 1 || abs([get_property PERIOD $c]-$period)>0.0001} {error "Placed clock mismatch: $name"}
}
puts SNAPSHOT_PHYSICAL_ROUTE_STARTED
route_design -directive Default
report_clocks -file clocks.rpt
report_io -file io.rpt
report_cdc -details -no_waiver -file cdc_raw.rpt
report_exceptions -file exceptions.rpt
check_timing -verbose -file check_timing.rpt
report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -file timing.rpt
report_timing -from [get_clocks clk_fpga_0] -to [get_clocks clk_fpga_0] -delay_type max -max_paths 20 -nworst 1 -file compute_paths.rpt
report_drc -file drc.rpt
report_utilization -file utilization.rpt
report_utilization -hierarchical -file hierarchy.rpt
report_route_status -file route_status.rpt
report_bus_skew -file bus_skew.rpt
write_xdc -force applied_constraints.xdc
write_checkpoint -force routed.dcp
puts SNAPSHOT_PHYSICAL_ROUTE_REPORTS_READY
exit
