# Actual platform connectivity, optionally assembled from current leaf IP EDIFs.
# Capacity only; no placement, route, clock claim or board operation.
if {![info exists ::env(SNAPSHOT_PLATFORM_AREA_ADMITTED)] || $::env(SNAPSHOT_PLATFORM_AREA_ADMITTED) ne "1"} {error "Guarded platform-area entry required"}
set root [file normalize [file join [file dirname [info script]] ..]]
set_param general.maxThreads 1
set_param synth.enableParallelHelperSpawn none
if {[get_param synth.enableParallelHelperSpawn] ne {none}} {error {Parallel helper must be disabled}}
set_param synth.elaboration.rodinMoreOptions {rt::set_parameter enableParallelHelperSpawn false}
create_project -in_memory -part xc7z020clg400-2
source area_sources.tcl
foreach p $area_custom_edifs {read_edif $p}
if {[llength $area_imports]} {
 foreach pair $area_imports {read_edif [lindex $pair 0];read_verilog [lindex $pair 1]}
} else {
 foreach name $area_ip_names {read_ip [file join $::env(SNAP_REBUILD_PLATFORM_XCI) ip $name ${name}.xci]}
 set_property generate_synth_checkpoint false [get_files -all *.xci]
 generate_target synthesis [get_ips]
}
# The generated XDCs contain clocks/jitter and PS MIO electrical/location
# properties. Preserve RTL CDC attributes and all real clock/reset nets;
# exclude physical constraints only from this private capacity calibration.
set constraints [get_files -all -filter {FILE_TYPE == XDC}]
set f [open excluded_constraints.tsv w]
foreach p $constraints {
 puts $f "$p\t[get_property USED_IN_SYNTHESIS $p]"
 set_property USED_IN_SYNTHESIS false $p
}
close $f
if {$area_leaf eq ""} {
 foreach name $area_custom_wrappers {read_verilog [file join $area_bd ip $name synth ${name}.v]}
 foreach p $area_rtl {read_verilog -sv $p}
 read_verilog [file join $area_bd synth snapshot_bd.v]
 read_verilog [file join $area_bd hdl snapshot_bd_wrapper.v]
} else {set area_expected_blackboxes {}}
set f [open ip_sources.tsv w]
foreach ip [get_ips -quiet] {
 puts $f "IP\t[get_property NAME $ip]\t[get_property IPDEF $ip]"
 foreach p [get_files -all -of_objects $ip] {puts $f "FILE\t$p\t[get_property FILE_TYPE $p]"}
}
foreach pair $area_imports {puts $f "NATIVE_EDIF\t[lindex $pair 0]\t[lindex $pair 1]"}
close $f
# Complete hardware must keep real external open-drain buffers/readback.
# OOC deliberately omits IO insertion and can turn inferred tristates into
# constants; that mode remains only for explicitly partial component work.
set area_mode {-mode out_of_context}
if {$area_complete} {set area_mode {}}
puts "SNAPSHOT_PLATFORM_AREA_STRATEGY $area_top $area_mode -flatten_hierarchy none -directive RuntimeOptimized -no_lc -no_timing_driven"
synth_design -top $area_top -part xc7z020clg400-2 {*}$area_mode -flatten_hierarchy none -directive RuntimeOptimized -no_lc -no_timing_driven
set blackboxes [get_cells -hier -filter {IS_BLACKBOX == 1}]
set actual_blackboxes {}
set f [open blackbox_cells.tsv w]
foreach cell $blackboxes {
 set ref [get_property REF_NAME $cell]
 set original [get_property ORIG_REF_NAME $cell]
 puts $f "$cell\t$ref\t$original"
 if {$original in $area_expected_blackboxes} {lappend actual_blackboxes $original} else {lappend actual_blackboxes $ref}
}
close $f
if {[lsort $actual_blackboxes] ne [lsort $area_expected_blackboxes]} {error "Unexpected platform boundaries: $actual_blackboxes"}
if {[info exists area_child_checkpoints] && [llength $area_child_checkpoints]} {
 set f [open imported_checkpoint_cells.tsv w]
 foreach entry $area_child_checkpoints {
  lassign $entry module file
  set cells [get_cells -hier -filter "IS_BLACKBOX == 1 && (REF_NAME == $module || ORIG_REF_NAME == $module)"]
  if {[llength $cells] != 1} {error "Expected unique real checkpoint boundary $module: $cells"}
  set cell [lindex $cells 0]
  read_checkpoint -cell $cell $file
  set row [list $module $cell]
  foreach primitive {DSP48E1 RAMB36E1 RAMB18E1} {
   lappend row [llength [get_cells -hier -filter "NAME =~ $cell/* && REF_NAME == $primitive"]]
  }
  puts $f [join $row "\t"]
 }
 close $f
 set blackboxes [get_cells -hier -filter {IS_BLACKBOX == 1}]
 if {[llength $blackboxes]} {error "Unresolved children after real checkpoint imports"}
 puts SNAPSHOT_REAL_CHILD_CHECKPOINTS_RESOLVED
}
if {[info exists area_native_children] && [llength $area_native_children]} {
 set f [open imported_native_cells.tsv w]
 foreach module $area_native_children {
  set cells [get_cells -hier -filter "REF_NAME == $module || ORIG_REF_NAME == $module"]
  if {[llength $cells] != 1} {error "Expected unique real native child $module: $cells"}
  set cell [lindex $cells 0]
  set row [list $module $cell]
  foreach primitive {DSP48E1 RAMB36E1 RAMB18E1} {
   lappend row [llength [get_cells -hier -filter "NAME =~ $cell/* && REF_NAME == $primitive"]]
  }
  puts $f [join $row "\t"]
 }
 close $f
}
if {$area_complete} {
 report_utilization -file before_utilization.rpt
 opt_design -propconst -sweep
 if {[llength [get_cells -quiet -hier -filter {IS_BLACKBOX == 1}]]} {error "Unresolved complete platform"}
 puts SNAPSHOT_COMPLETE_PLATFORM_LOGIC_CLEANUP
 set f [open pad_buffers.tsv w]
 foreach pad {cam0_scl cam0_sda cam1_scl cam1_sda cam_pwdn cam_reset_n} {
  set pin [get_pins -of_objects [get_nets -of_objects [get_ports $pad]] -filter {REF_PIN_NAME == IO}]
  if {[llength $pin] != 1} {error "Missing unique actual IO buffer for $pad: $pin"}
  set cell [get_cells -of_objects $pin]
  if {[get_property REF_NAME $cell] ne "IOBUF"} {error "Wrong bidirectional primitive for $pad: $cell"}
  set fields [list $pad $cell]
  foreach term {I T O} {
   set p [get_pins -of_objects $cell -filter "REF_PIN_NAME == $term"]
   lappend fields [get_nets -of_objects $p]
  }
  puts $f [join $fields "\t"]
 }
 close $f
 puts SNAPSHOT_COMPLETE_PLATFORM_REAL_IO
}
report_utilization -file utilization.rpt
report_utilization -hierarchical -file hierarchy.rpt
set f [open metrics.tsv w]
foreach {key filter} {dsp {REF_NAME == DSP48E1} ramb36 {REF_NAME == RAMB36E1} ramb18 {REF_NAME == RAMB18E1} ff {REF_NAME =~ FD*} lut {REF_NAME =~ LUT*} lutram {REF_NAME =~ RAM* && REF_NAME !~ RAMB*} srl {REF_NAME =~ SRL*} ps7 {REF_NAME == PS7} iobuf {REF_NAME == IOBUF} latches {REF_NAME =~ LD*}} {
 puts $f "$key\t[llength [get_cells -hier -filter $filter]]"
}
puts $f "blackboxes\t[llength $blackboxes]"
close $f
if {$::env(SNAP_AREA_EXPORT) eq "1"} {
 write_edif -force component.edf
 if {$area_leaf ne ""} {write_verilog -mode synth_stub -force component_stub.v}
 puts SNAPSHOT_PLATFORM_AREA_EXPORTED
}
puts SNAPSHOT_PLATFORM_AREA_PASS
exit
