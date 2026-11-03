# Resolve only the named AMD IP family in the current 2026.1 catalog.
# Never import an old XCI or choose an unrelated IP to satisfy a missing family.
proc snapshot_2026_ip {name} {
    set family xilinx.com:ip:${name}:
    set choices {}
    foreach object [get_ipdefs -all -quiet ${family}*] {
        set vlnv [get_property VLNV $object]
        if {[string first $family $vlnv] == 0} {lappend choices $vlnv}
    }
    set choices [lsort -unique -dictionary $choices]
    if {[llength $choices] == 0} {error "Required IP family absent from 2026.1 catalog: $name"}
    return [lindex $choices end]
}
