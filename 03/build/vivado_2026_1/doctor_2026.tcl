if {![info exists ::env(SNAP2026_GUARDED)] || $::env(SNAP2026_GUARDED) ne "doctor"} {
    error "Use the guarded run_2026.py doctor entry"
}
if {[version -short] ne "2026.1"} {error "Vivado 2026.1 is required"}
set_param general.maxThreads 1
set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root modern ip_catalog.tcl]
if {[llength [get_parts -quiet xc7z020clg400-2]] != 1} {error "Zynq-7020 device support missing"}
create_project -in_memory -part xc7z020clg400-2
set f [open installed_inventory.tsv w]
puts $f "tool\t[version -short]"
puts $f "part\t[get_property PART [current_project]]"
foreach name {processing_system7 axi_interconnect axi_protocol_converter} {
    puts $f "$name\t[snapshot_2026_ip $name]"
}
close $f
report_param -file runtime_parameters.txt
close_project
puts SNAPSHOT_2026_DOCTOR_PASS
exit 0
