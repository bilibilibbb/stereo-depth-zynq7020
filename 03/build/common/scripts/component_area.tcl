# Offline component calibration only. No physical implementation or board IO.
if {![info exists ::env(SNAPSHOT_AREA_ADMITTED)] || $::env(SNAPSHOT_AREA_ADMITTED) ne "1"} {error "Guarded component entry required"}
set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root hardware generated_contract.tcl]
set_param general.maxThreads 1
# Installed 2018.3 rtSynthParallelPrep.tcl still starts a helper with maxThreads=1.
# The installed Rodin option was requested, but the 20261020T091409 run still
# spawned a helper before RTL elaboration. Report observation, not success.
# No vendor installation or either Job memory limit is changed.
set_param synth.elaboration.rodinMoreOptions {rt::set_parameter enableParallelHelperSpawn false}
puts "SNAPSHOT_AREA_PARALLEL_HELPER disable_requested_unverified"
create_project -in_memory -part xc7z020clg400-2
source area_sources.tcl
foreach p $area_edifs {read_edif $p}
foreach p $area_rtl {read_verilog -sv $p}
set kind $::env(SNAP_AREA_COMPONENT)
set generics {}
switch -- $kind {
 compute {set top fw_shared_devices_rtl;set generics [list SGM_BUILD_ID=[expr {$snapshot_build}] ADDRESS_MIN=268435456 ADDRESS_END=469762048]}
 nn {set top fw_nn_mmio_rtl;set generics [list ADDRESS_MIN=268435456 ADDRESS_END=469762048]}
 sgm {set top fw_sgm_mmio_rtl;set generics [list BUILD_ID=[expr {$snapshot_build}] ADDRESS_MIN=268435456 ADDRESS_END=469762048]}
 box {set top fw_color_device_rtl;set generics [list SHARED_DMA=1 ADDRESS_MIN=268435456 ADDRESS_END=469762048]}
 camera {set top camera_pair_capture}
 sensor {set top learning_capture;set generics [list BUILD_ID=[expr {$snapshot_build}] PROFILE_SGM=1 CAPTURE_ENABLED=1]}
 mac {set top fw_mac128;set generics [list SPLIT_RESULT_READY=1]}
 raw {set top fw_nn_raw_banks}
 sgm_volume {set top fw_sgm_volume_rtl}
 sgm_path {set top sgm_path6_group_v1;set generics [list LANES=16 TAG_WIDTH=4]}
 sgm_row {set top sgm_row_ram_v1;set generics [list DATA_BITS=390 DEPTH=320 ADDR_BITS=9]}
 sgm_pool {set top fw_sgm_curve_pool_rtl}
 nn_shell {set top fw_nn_mmio_rtl;set generics [list ADDRESS_MIN=268435456 ADDRESS_END=469762048]}
 pointwise_shell {set top fw_pointwise_rtl}
 sgm_shell {set top fw_sgm_mmio_rtl;set generics [list BUILD_ID=[expr {$snapshot_build}] ADDRESS_MIN=268435456 ADDRESS_END=469762048]}
 nn_control {set top fw_nn_mmio_rtl;set generics [list ADDRESS_MIN=268435456 ADDRESS_END=469762048]}
 nn_parent {set top fw_nn_mmio_rtl;set generics [list ADDRESS_MIN=268435456 ADDRESS_END=469762048]}
 nn_transport {set top fw_nn_transport_rtl;set generics [list ADDRESS_MIN=268435456 ADDRESS_END=469762048]}
 nn_device {set top fw_nn_device_rtl}
 nn_gather {set top fw_nn_raw_gather}
 nn_dw_pack {set top fw_nn_dw_pack_rtl}
 nn_dw_store {set top fw_nn_dw_store_rtl}
 nn_residual {set top fw_nn_residual_rtl}
 nn_scalar_store {set top fw_nn_scalar_store_rtl}
 camera_channel {set top dvp_ddr_channel}
 camera_shell {set top camera_pair_capture}
 compute_shell {set top fw_shared_devices_rtl;set generics [list SGM_BUILD_ID=[expr {$snapshot_build}] ADDRESS_MIN=268435456 ADDRESS_END=469762048]}
 default {error "Unsupported component"}
}
puts "SNAPSHOT_COMPONENT_BEGIN $kind $top"
set area_profile baseline
if {[info exists ::env(SNAP_AREA_PROFILE)]} {set area_profile $::env(SNAP_AREA_PROFILE)}
if {$area_profile ni {baseline lut_combining default sharing_auto}} {error "Unsupported area profile"}
set strategy [list -flatten_hierarchy rebuilt]
# The unchanged MAC exceeded the process cap during default RTL optimization.
# Vivado's runtime directive performs fewer optimization passes; this is a
# separately identified synthesis strategy, not a source/numerical change.
if {$kind in {mac nn sgm sgm_volume nn_shell pointwise_shell sgm_shell camera camera_shell nn_parent}} {set strategy [list -flatten_hierarchy none -directive RuntimeOptimized -no_lc]}
if {$kind eq "pointwise_shell"} {lappend strategy -resource_sharing off}
# These shells exhausted memory around part/timing loading and subsequent
# optimization. Resource-only calibration has no clock claim, so explicitly
# omit timing-driven optimization; physical timing remains a separate gate.
if {$kind in {pointwise_shell camera_shell nn_shell nn_parent}} {lappend strategy -no_timing_driven}
# One-factor capacity calibration. Keep the source, boundaries and every
# other setting identical; only allow the synthesis tool's LUT pairing.
if {$area_profile eq "lut_combining"} {
 set position [lsearch -exact $strategy -no_lc]
 if {$position<0} {error "Selected component has no disabled LUT combining to compare"}
 set strategy [lreplace $strategy $position $position]
}
# Revisit standard synthesis only for the now smaller actual MAC. The
# original memory/CPU/mutex guard is unchanged; this is a tool-profile
# comparison on identical RTL, not a source or performance improvement.
if {$area_profile eq "default"} {
 if {$kind ne "mac"} {error "Default-strategy comparison is MAC-only"}
 set strategy [list -flatten_hierarchy rebuilt]
}
if {$area_profile eq "sharing_auto"} {
 if {$kind ne "pointwise_shell"} {error "Sharing comparison is pointwise-only"}
 set position [lsearch -exact $strategy -resource_sharing]
 if {$position<0} {error "Expected explicit sharing-off baseline"}
 lset strategy [expr {$position+1}] auto
}
puts "SNAPSHOT_AREA_PROFILE $area_profile"
puts "SNAPSHOT_AREA_STRATEGY $strategy"
synth_design -top $top -generic $generics -part xc7z020clg400-2 -mode out_of_context {*}$strategy
set blackboxes [get_cells -hier -filter {IS_BLACKBOX == 1}]
set latches [get_cells -hier -filter {REF_NAME =~ LD*}]
set bbout [open blackbox_cells.tsv w]
set actual_blackboxes {}
foreach cell $blackboxes {
 set ref [get_property REF_NAME $cell]
 lappend actual_blackboxes $ref
 puts $bbout "$cell\t$ref"
}
close $bbout
if {[lsort $actual_blackboxes] ne [lsort $area_expected_blackboxes]} {error "Unexpected unresolved boundaries: $actual_blackboxes"}
report_utilization -file utilization.rpt
report_utilization -hierarchical -file hierarchy.rpt
set out [open metrics.tsv w]
foreach {key filter} {dsp {REF_NAME == DSP48E1} ramb36 {REF_NAME == RAMB36E1} ramb18 {REF_NAME == RAMB18E1} ff {REF_NAME =~ FD*} lut {REF_NAME =~ LUT*} lutram {REF_NAME =~ RAM* && REF_NAME !~ RAMB*} srl {REF_NAME =~ SRL*}} {
 puts $out "$key\t[llength [get_cells -hier -filter $filter]]"
}
puts $out "blackboxes\t[llength $blackboxes]"
puts $out "latches\t[llength $latches]"
close $out
if {[llength $latches]} {error "Inferred latch cells: $latches"}
# Inspect the specific remaining formatter path on the complete pointwise
# netlist. A clock added after synthesis does not change the area strategy.
# Only internal clock-to-clock paths are covered: external I/O timing and
# actual placement/routing are deliberately not inferred from this report.
if {$kind eq "pointwise_shell" && ![llength $blackboxes]} {
 set period_ns [expr {1000000000.0/$snapshot_compute_hz}]
 create_clock -name component_clk -period $period_ns [get_ports clk]
 report_timing -delay_type max -from [get_clocks component_clk] -to [get_clocks component_clk] -max_paths 10 -input_pins -file internal_timing.rpt
 report_clocks -file component_clocks.rpt
 puts "SNAPSHOT_COMPONENT_INTERNAL_TIMING_READY period_ns=$period_ns placed=0 routed=0"
}
# Area calibration only. Checkpoint compression caused an additional allocation
# after valid synthesis/reports and exceeded the unchanged Job limit. Do not
# make unrelated netlist serialization a requirement for a resource report.
# A deployable assembled netlist still needs a separate successful export.
if {[info exists ::env(SNAP_AREA_EXPORT)] && $::env(SNAP_AREA_EXPORT) eq "1"} {
 # Text EDIF avoids the checkpoint compression step that previously failed.
 # This is an experiment under the same Job cap, not a proven memory saving.
 write_edif -force component.edf
 puts "SNAPSHOT_COMPONENT_EDIF_EXPORTED $kind"
}
puts "SNAPSHOT_COMPONENT_SYNTHESIS_PASS $kind"
exit
