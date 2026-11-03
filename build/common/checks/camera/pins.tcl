# Diagnostic hypothesis: CN1 and J25 adjacent columns are exchanged at mating. Right bank uses traced schematic alternative; left/control mapping retained.
# Actual source-synchronous capture: do NOT source diagnostic false paths.
set pins {
    clk_50 N18
    cam0_data[0] T10
    cam0_data[1] T11
    cam0_data[2] U12
    cam0_data[3] T12
    cam0_data[4] V13
    cam0_data[5] U13
    cam0_data[6] T15
    cam0_data[7] T14
    cam0_scl V18
    cam0_sda V17
    cam0_vs U17
    cam0_href T16
    cam0_pclk P16
    cam_xclk P15
    cam_reset_n R14
    cam1_data[0] P14
    cam1_data[1] W16
    cam1_data[2] V16
    cam1_data[3] W19
    cam1_data[4] W18
    cam1_data[5] U19
    cam1_data[6] U18
    cam1_data[7] R17
    cam1_scl R16
    cam1_sda V12
    cam1_vs W15
    cam1_pclk W13
    cam1_href P19
    cam_pwdn R19
}
foreach {port pin} $pins {
    set p [get_ports $port]
    if {[llength $p] != 1} { error "Pin mapping must resolve exactly once: $port" }
    set_property PACKAGE_PIN $pin $p
    set_property IOSTANDARD LVCMOS33 $p
}
# Never generate XCLK onto the oscillator's net. This port is input-only in RTL.
set_property DRIVE 4 [get_ports {cam0_scl cam0_sda cam1_scl cam1_sda cam_reset_n cam_pwdn}]
set_property DRIVE 4 [get_ports cam_reset_n]
set_property SLEW SLOW [get_ports {cam0_scl cam0_sda cam1_scl cam1_sda cam_reset_n cam_pwdn}]

# Pinout selected from the explicit official J25 workbook table. Sensor external min/max is
# deliberately absent; routed reference-window report is a separate artifact.
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets pclk0_raw]
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets pclk1_raw]
set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property BITSTREAM.CONFIG.UNUSEDPIN Pullnone [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
