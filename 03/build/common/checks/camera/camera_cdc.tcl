# Actual native BD hierarchy. 8ns is an internal routing requirement, never
# sensor electrical data. Preserve local synchronizer stage-to-stage timing.
set cdc_log [open cdc_constraints_applied.tsv w]
puts $cdc_log "kind\tsource\tdestination\tbudget_ns"
proc cells_exact {expression minimum maximum} {
    set a [get_cells -quiet -hier -regexp $expression]
    if {[llength $a]<$minimum || [llength $a]>$maximum} {error "Endpoint count [llength $a] outside $minimum..$maximum: $expression"}
    foreach c $a {if {![get_property IS_SEQUENTIAL $c]} {error "Non-register endpoint $c"}}
    return $a
}
proc constrain_crossing {kind a b skew} {
    global cdc_log
    set_max_delay -datapath_only 8.0 -from $a -to $b
    set_false_path -hold -from $a -to $b
    if {$skew} {set_bus_skew 8.0 -from $a -to $b}
    puts $cdc_log "$kind\t[join $a ,]\t[join $b ,]\t8"
}
proc async_destinations {a} {
    foreach c $a {if {![get_property ASYNC_REG $c]} {error "Missing ASYNC_REG: $c"}}
}
set cap {bd/snapshot_bd_i/capture_ddr/inst/core}
set sen {bd/snapshot_bd_i/sensor/inst/core}
if {[llength [get_cells -quiet -hier -regexp {^bd/snapshot_bd_i/sensor/inst/core/cap[01]/.*$}]]} {error "Removed legacy ROI unexpectedly instantiated"}
foreach lane {0 1} {
    foreach {s d} {rgray rg1 wgray wg1} {
        set a [cells_exact [format {^%s/camera%d/fifo/%s_reg\[[0-9]+\]$} $cap $lane $s] 10 10]
        set b [cells_exact [format {^%s/camera%d/fifo/%s_reg\[[0-9]+\]$} $cap $lane $d] 10 10]
        async_destinations $b
        constrain_crossing fifo_gray $a $b 1
    }
    set a [cells_exact [format {^%s/control/ticks_gray_reg\[[0-9]+\]$} $cap] 32 32]
    set b [cells_exact [format {^%s/camera%d/source/tick_g1_reg\[[0-9]+\]$} $cap $lane] 32 32]
    async_destinations $b;constrain_crossing ticks_gray $a $b 1
    foreach {kind sr dr} {
        request request_toggle_reg {source/request_sync_reg\[0\]}
        cancel source_cancel_registered_reg {source/cancel_sync_reg\[0\]}
        done source/done_toggle_reg {done_sync_reg\[0\]}
    } {
        set a [cells_exact [format {^%s/camera%d/%s$} $cap $lane $sr] 1 1]
        set b [cells_exact [format {^%s/camera%d/%s$} $cap $lane $dr] 1 1]
        async_destinations $b
        # Audit that cancellation really originates at its single launch flop.
        if {$kind eq "cancel"} {
            set pin [get_pins -of_objects $b -filter {REF_PIN_NAME == D}]
            set origin [lsort -unique [get_cells -of_objects [all_fanin -flat -startpoints_only -to $pin]]]
            if {$origin ne $a} {error "Cancellation has unexpected launch cone: $origin"}
        }
        constrain_crossing $kind $a $b 0
    }
    set a [cells_exact [format {^%s/camera%d/source/(error_flags|frame_id|start_tick|end_tick|byte_count|line_count|accepted_words|crc32|fifo_peak|error_location)_reg\[[0-9]+\]$} $cap $lane] 274 274]
    set b [cells_exact [format {^%s/camera%d/(sampled_source_error|frame_id|start_tick|end_tick|byte_count|line_count|accepted_words|crc32|fifo_peak|error_location)_reg\[[0-9]+\]$} $cap $lane] 274 274]
    constrain_crossing held_frame_metadata $a $b 1
    set a [cells_exact [format {^%s/mon%d/pixel_gray_reg\[[0-9]+\]$} $sen $lane] 32 32]
    set b [cells_exact [format {^%s/mon%d/gray_meta_reg\[[0-9]+\]$} $sen $lane] 32 32]
    async_destinations $b;constrain_crossing monitor_gray $a $b 1
    # Asynchronous diagnostic observers only. DVP functional samplers are not
    # cut or covered by these explicit destination collections.
    foreach signal {vs href} {
        set b [cells_exact [format {^%s/mon%d/%s_sync_reg\[0\]$} $sen $lane $signal] 1 1]
        async_destinations $b
        if {$signal eq "vs"} {
            set a [cells_exact [format {^frame_sync%d/frame_vs_reg$} $lane] 1 1]
        } else {
            set a [cells_exact [format {^frame_sync%d/href_pipe_reg\[7\]$} $lane] 1 1]
        }
        constrain_crossing recovered_level_monitor $a $b 0
    }
    foreach signal {scl sda} {
        set b [cells_exact [format {^%s/cam%d_cfg/%s_sync_reg\[0\]$} $sen $lane $signal] 1 1]
        async_destinations $b
        constrain_crossing sccb_input [get_ports cam${lane}_${signal}] $b 0
    }
}
set b [cells_exact [format {^%s/data_meta_reg\[[0-9]+\]$} $sen] 16 16]
set a [cells_exact {^frame_sync[01]/data_pipe_reg\[7\]\[[0-7]\]$} 16 16]
async_destinations $b;constrain_crossing diagnostic_uncoherent_delayed_data $a $b 0
set b [cells_exact [format {^%s/io_meta_reg\[[0-9]+\]$} $sen] 7 7]
async_destinations $b;constrain_crossing diagnostic_async_io [get_ports {cam_xclk cam0_scl cam0_sda cam1_scl cam1_sda cam_reset_n cam_pwdn}] $b 0
# Only asynchronous reset inputs of actual reset synchronizer chains. All
# local reset-output-to-functional-CLR/PRE recovery/removal paths remain timed.
set reset_cells [cells_exact {^reset_control/release_sync_reg\[[0-2]\]$} 3 3]
set reset_cells [concat $reset_cells [cells_exact {^compute_release/release_sync_reg\[[0-2]\]$} 3 3]]
foreach lane {0 1} {
    foreach e [list [format {^%s/camera%d/fifo/wrst_reg\[[01]\]$} $cap $lane] [format {^%s/camera%d/source/local_reset_reg\[[01]\]$} $cap $lane] [format {^%s/mon%d/p_reset_reg\[[01]\]$} $sen $lane]] {
        set n 2
        set reset_cells [concat $reset_cells [cells_exact $e $n $n]]
    }
}
set reset_cells [concat $reset_cells [cells_exact {^frame_sync[01]/local_reset_reg\[[01]\]$} 4 4]]
async_destinations $reset_cells
set reset_pins [get_pins -of_objects $reset_cells -filter {REF_PIN_NAME == CLR || REF_PIN_NAME == PRE}]
if {[llength $reset_cells]!=22 || [llength $reset_pins]!=22} {error "Unexpected reset synchronizer count"}
set_false_path -to $reset_pins
puts $cdc_log "async_reset_assertion\tPS_or_board_reset\t[join $reset_pins ,]\tasync"
# The SCCB/control outputs are asynchronous open-drain GPIO. This only bounds
# internal output routing latency, not board RC rise time or sensor setup/hold.
set controls [get_ports {cam0_scl cam0_sda cam1_scl cam1_sda cam_reset_n cam_pwdn}]
set_max_delay -datapath_only 8.0 -from [get_clocks sys_clk] -to $controls
set_false_path -hold -from [get_clocks sys_clk] -to $controls
puts $cdc_log "gpio_output_internal\tsys_clk\t[join $controls ,]\t8"
close $cdc_log
