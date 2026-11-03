if {![info exists ::env(SNAPSHOT_LEGACY_NN_ADMITTED)] || $::env(SNAPSHOT_LEGACY_NN_ADMITTED) ne "1"} {error "Guarded NN entry required"}
set root [file normalize [file join [file dirname [info script]] ..]]
set_param general.maxThreads 1
set_param synth.enableParallelHelperSpawn none
if {[get_param synth.enableParallelHelperSpawn] ne {none}} {error {Parallel helper must be disabled}}
set_param synth.maxThreads 1
set_param synth.elaboration.rodinMoreOptions {rt::set_parameter enableParallelHelperSpawn false}
create_project -in_memory -part xc7z020clg400-2
source nn_sources.tcl
foreach p $nn_rtl {read_verilog -sv $p}
read_xdc nn_clock.xdc
synth_design -top $nn_top -generic $nn_generics -part xc7z020clg400-2 -mode out_of_context -flatten_hierarchy rebuilt
foreach entry $nn_imports {
 lassign $entry module checkpoint
 set cells [get_cells -hier -filter "IS_BLACKBOX == 1 && (REF_NAME == $module || ORIG_REF_NAME == $module)"]
 if {[llength $cells]!=1} {error "Expected one actual NN child $module: $cells"}
 read_checkpoint -cell [lindex $cells 0] $checkpoint
}
if {[llength $nn_imports]} {opt_design -propconst -sweep}
if {[llength [get_cells -quiet -hier -filter {IS_BLACKBOX == 1}]]} {error "Unresolved NN body"}
if {[llength [get_cells -quiet -hier -filter {REF_NAME =~ LD*}]]} {error "Unexpected NN latch"}
if {[llength [get_clocks clk]]!=1 || [get_property PERIOD [get_clocks clk]]!=10.0} {error "10ns NN clock missing"}
report_utilization -file utilization.rpt
report_utilization -hierarchical -file hierarchy.rpt
report_timing_summary -delay_type min_max -report_unconstrained -file internal_timing.rpt
report_timing -from [get_clocks clk] -to [get_clocks clk] -delay_type max -max_paths 20 -nworst 1 -file internal_paths.rpt
set f [open metrics.tsv w]
foreach {key filter} {dsp {REF_NAME == DSP48E1} ramb36 {REF_NAME == RAMB36E1} ramb18 {REF_NAME == RAMB18E1} ff {REF_NAME =~ FD*} lut {REF_NAME =~ LUT*} latches {REF_NAME =~ LD*} blackboxes {IS_BLACKBOX == 1}} {
 puts $f "$key\t[llength [get_cells -hier -filter $filter]]"
}
close $f
set_property used_in_implementation false [get_files nn_clock.xdc]
write_edif -force nn.edf
write_checkpoint -force nn.dcp
puts SNAPSHOT_NATIVE_LEGACY_NN_PASS
exit
