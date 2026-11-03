# Complete actual selected component. No platform fit, physical or board action.
if {![info exists ::env(SNAPSHOT_LEGACY_AREA_ADMITTED)] || $::env(SNAPSHOT_LEGACY_AREA_ADMITTED) ne "1"} {error "Guarded legacy component entry required"}
set root [file normalize [file join [file dirname [info script]] ..]]
set_param general.maxThreads 1
set_param synth.enableParallelHelperSpawn none
if {[get_param synth.enableParallelHelperSpawn] ne {none}} {error {Parallel helper must be disabled}}
set_param synth.maxThreads 1
set_param synth.elaboration.rodinMoreOptions {rt::set_parameter enableParallelHelperSpawn false}
create_project -in_memory -part xc7z020clg400-2
source area_sources.tcl
if {$area_edif ne ""} {read_edif $area_edif}
foreach p $area_rtl {read_verilog -sv $p}
# The rt command exists before synthesis but its database is not initialized;
# do not call the internal setter early (retained diagnostic122558). The
# elaboration option remains only a request; actual helper logs are authoritative.
synth_design -top $area_top -generic $area_generics -part xc7z020clg400-2 -mode out_of_context -flatten_hierarchy none -directive RuntimeOptimized -no_lc -no_timing_driven
if {$area_checkpoint ne ""} {
 set cells [get_cells -hier -filter "IS_BLACKBOX == 1 && (REF_NAME == $area_child || ORIG_REF_NAME == $area_child)"]
 if {[llength $cells] != 1} {error "Expected unique real NN checkpoint boundary: $cells"}
 read_checkpoint -cell [lindex $cells 0] $area_checkpoint
 puts SNAPSHOT_REAL_NN_CHECKPOINT_IMPORTED
}
if {[llength [get_cells -quiet -hier -filter {IS_BLACKBOX == 1}]]} {error "Unresolved real child"}
if {[llength [get_cells -quiet -hier -filter {REF_NAME =~ LD*}]]} {error "Unexpected latch"}
report_utilization -file before_utilization.rpt
opt_design -propconst -sweep
report_utilization -file utilization.rpt
report_utilization -hierarchical -file hierarchy.rpt
set f [open metrics.tsv w]
foreach {key filter} {dsp {REF_NAME == DSP48E1} ramb36 {REF_NAME == RAMB36E1} ramb18 {REF_NAME == RAMB18E1} ff {REF_NAME =~ FD*} lut {REF_NAME =~ LUT*} latches {REF_NAME =~ LD*} blackboxes {IS_BLACKBOX == 1}} {
 puts $f "$key\t[llength [get_cells -hier -filter $filter]]"
}
close $f
write_edif -force component.edf
write_verilog -mode synth_stub -force component_stub.v
# Inspect the mapped control cone before spending another complete physical run.
# Apply this internal diagnostic only after exporting the unchanged netlist.
if {$area_compute_period_ns > 0} {
 create_clock -name component_compute -period $area_compute_period_ns [get_ports clk]
 report_timing_summary -delay_type min_max -report_unconstrained -file compute_internal_timing.rpt
 report_timing -from [get_clocks component_compute] -to [get_clocks component_compute] -delay_type max -max_paths 20 -nworst 1 -file compute_internal_paths.rpt
}
puts SNAPSHOT_LEGACY_COMPONENT_AREA_PASS
exit
