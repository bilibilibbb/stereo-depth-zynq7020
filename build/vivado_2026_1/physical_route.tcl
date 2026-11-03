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
if {!$resume_completed_route} {route_design -directive Default}
# Preserve the full routed result before a bounded post-route physical pass.
# This changes placement/routing and may replicate logic; it does not change
# the RTL, timing constraints, clock targets or functional pipeline latency.
set worst [get_timing_paths -delay_type max -max_paths 1]
if {[llength $worst] != 1} {error "Missing routed timing path"}
if {[get_property SLACK $worst] < 0.0} {
    puts SNAPSHOT_POST_ROUTE_PHYSICAL_OPTIMIZATION_STARTED
    phys_opt_design -directive Explore
}
if {[llength [get_cells -quiet -hier -filter {IS_BLACKBOX == 1}]]} {error "Unresolved optimized netlist"}
if {[llength [get_cells -quiet -hier -filter {REF_NAME == DSP48E1}]] > 220} {error "DSP capacity exceeded"}
set ram_equiv [expr {2*[llength [get_cells -quiet -hier -filter {REF_NAME == RAMB36E1}]]+[llength [get_cells -quiet -hier -filter {REF_NAME == RAMB18E1}]]}]
if {$ram_equiv > 280} {error "BRAM capacity exceeded"}
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
