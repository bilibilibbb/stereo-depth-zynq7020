# Place the exact reviewed complete EDIF under the original constraints.
if {![info exists ::env(SNAPSHOT_PHYSICAL_PLACEMENT_ADMITTED)] || $::env(SNAPSHOT_PHYSICAL_PLACEMENT_ADMITTED) ne "1"} {error "Guarded physical placement required"}
set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root hardware generated_contract.tcl]
set_param general.maxThreads 1
create_project -in_memory -part xc7z020clg400-2
source inputs.tcl
foreach companion $inspected_companion_netlists {read_edif $companion}
read_edif $inspected_netlist
link_design -top snapshot_platform_top -part xc7z020clg400-2
if {[llength [get_cells -quiet -hier -filter {IS_BLACKBOX == 1}]]} {error "Unresolved complete netlist"}
# Exact primitive counts come from the reviewed complete current capacity receipt.
foreach {primitive expected} $inspected_primitive_counts {
 if {[llength [get_cells -quiet -hier -filter "REF_NAME == $primitive"]] != $expected} {error "Current complete primitive mismatch: $primitive expected $expected"}
}
if {[llength [get_cells -quiet -hier -filter {REF_NAME == IOBUF}]] != 6} {error "Physical pad buffers were lost"}
set ps [get_cells -quiet bd/snapshot_bd_i/ps/inst]
if {[llength $ps] != 1} {error "Actual PS7 constraint scope missing"}
read_xdc -cells $ps $inspected_ps_xdc
create_clock -name sys_clk -period 20.0 [get_ports clk_50]
# Historical same recipe/capture flow uses a 60 MHz internal PCLK ceiling.
# Nominal recipe is 56 MHz. This is not sensor/PCB setup-hold evidence.
foreach lane {0 1} {create_clock -name camera${lane}_clk -period 16.667 [get_ports cam${lane}_pclk]}
foreach {name period} [list clk_fpga_0 [expr {1000000000.0/$snapshot_compute_hz}] sys_clk 20.0 camera0_clk 16.667 camera1_clk 16.667] {
    set c [get_clocks -quiet $name]
    if {[llength $c] != 1 || abs([get_property PERIOD $c]-$period)>0.0001} {error "Clock definition mismatch $name"}
}
source [file join $root hardware constraints pins.tcl]
source [file join $root hardware constraints camera_cdc.tcl]
# Exact installed XPM constraints, scoped to real instantiated macros. Preserve
# the vendor's internal CDC waivers, but always report the raw unwaived CDCs.
set f [open vendor_constraints_applied.tsv w]
puts $f "cell\tmacro\tfile"
set xpms [get_cells -quiet -hier -filter {REF_NAME =~ xpm_cdc* || ORIG_REF_NAME =~ xpm_cdc*}]
if {![llength $xpms]} {error "Expected actual AXI clock-converter XPM macros"}
foreach cell $xpms {
    set kind [get_property ORIG_REF_NAME $cell]
    if {$kind eq ""} {set kind [get_property REF_NAME $cell]}
    if {![dict exists $xpm_constraint_files $kind]} {error "Unreviewed XPM macro $kind"}
    set file [dict get $xpm_constraint_files $kind]
    # These installed files are Tcl, including conditional constraint logic.
    # Managed XDC parsing skips those conditionals in Vivado 2018.3.
    read_xdc -unmanaged -no_add -cells $cell $file
    puts $f "$cell\t$kind\t$file"
}
close $f
report_utilization -file before_utilization.rpt
report_utilization -hierarchical -file before_hierarchy.rpt
report_control_sets -verbose -file before_control_sets.rpt
if {$physical_operation eq "optimize_and_report_only"} {
    puts SNAPSHOT_OPT_DESIGN_HELP_BEGIN
    puts [help opt_design]
    puts SNAPSHOT_OPT_DESIGN_HELP_END
    puts SNAPSHOT_PHYSICAL_OPTIMIZATION_STARTED
    if {$logic_optimization eq "ExploreArea"} {
        opt_design -directive ExploreArea
    } elseif {$logic_optimization eq "merge_equivalent_drivers"} {
        opt_design -control_set_merge -merge_equivalent_drivers
    } else {error "Unreviewed logic optimization"}
} elseif {$physical_operation eq "place_and_report_only"} {
    puts SNAPSHOT_PHYSICAL_PLACEMENT_STARTED
    place_design -directive Default
} else {error "Unknown physical operation"}
report_clocks -file clocks.rpt
report_io -file io.rpt
report_cdc -details -no_waiver -file cdc_raw.rpt
report_exceptions -file exceptions.rpt
check_timing -verbose -file check_timing.rpt
report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -file timing.rpt
report_drc -file drc.rpt
report_utilization -file utilization.rpt
report_utilization -hierarchical -file hierarchy.rpt
report_control_sets -verbose -file control_sets.rpt
report_route_status -file route_status.rpt
write_xdc -force applied_constraints.xdc
if {$physical_operation eq "optimize_and_report_only"} {
    write_edif -force optimized.edf
    write_checkpoint -force optimized.dcp
    puts SNAPSHOT_PHYSICAL_OPTIMIZATION_REPORTS_READY
} else {
    write_checkpoint -force placed.dcp
    puts SNAPSHOT_PHYSICAL_PLACEMENT_REPORTS_READY
}
exit
