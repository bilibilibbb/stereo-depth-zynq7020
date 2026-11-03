# Actual SDK software generation from the current source-bound hardware export.
# This script does not connect to hardware or create a boot image.
if {![info exists ::env(SNAPSHOT_BOOT_SUPPORT_ADMITTED)] || $::env(SNAPSHOT_BOOT_SUPPORT_ADMITTED) ne "1"} {
 error "Use run_check.py --boot-support with the current platform receipt"
}
set out [file normalize [lindex $argv 0]]
set hw [hsi::open_hw_design [file join $out snapshot.hdf]]
set cpu [hsi::get_cells -hier -filter {IP_NAME == ps7_cortexa9}]
if {[llength $cpu]!=2 || [llength [hsi::get_cells -hier ps7_cortexa9_0]]!=1} {
 error "Expected the actual dual Cortex-A9 platform"
}
set f [open [file join $out hardware_processors.tsv] w]
foreach p $cpu {
 foreach k [lsort [common::list_property $p]] {
  if {[string match CONFIG.* $k]} {puts $f "$p\t$k\t[common::get_property $k $p]"}
 }
}
close $f
# The installed FSBL template selects its real standalone/xilffs/xilrsa BSP
# dependencies and copies the PS init embedded in this export. No stale BSP.
hsi::generate_app -app zynq_fsbl -proc ps7_cortexa9_0 -os standalone -dir [file join $out fsbl] -compile
hsi::close_hw_design $hw
puts SNAPSHOT_BOOT_SUPPORT_GENERATED_UNDEPLOYED
exit
