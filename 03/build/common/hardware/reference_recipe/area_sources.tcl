set area_bd [file join $root {build/20261027T171305_443192Z/area/generated/snapshot_bd}]
set area_ip_names {snapshot_bd_auto_cc_0 snapshot_bd_auto_cc_1 snapshot_bd_auto_pc_0 snapshot_bd_camera_protocol_0 snapshot_bd_compute_protocol_0 snapshot_bd_fp_protocol_0 snapshot_bd_ps_0 snapshot_bd_sgm_protocol_0 snapshot_bd_xbar_0 snapshot_bd_xbar_1}
set area_leaf {}
set area_top {snapshot_platform_top}
set area_imports {}
set area_custom_edifs [list [file join $root {build/20261027T171305_443192Z/area/imported/r14_shared_compute_mmio_v1.edf}] [file join $root {build/20261027T171305_443192Z/area/imported/fw_sgm_mmio_rtl.edf}] [file join $root {build/20261027T171305_443192Z/area/imported/camera_pair_capture.edf}]]
set area_complete 1
set area_native_children {r14_shared_compute_mmio_v1 fw_sgm_mmio_rtl camera_pair_capture}
set area_custom_wrappers {snapshot_bd_compute_0 snapshot_bd_sensor_0 snapshot_bd_capture_ddr_0}
set area_expected_blackboxes {}
set area_child_checkpoints [list ]
set area_rtl [list [file join $root {hardware/rtl/camera_platform_reset.v}] [file join $root {hardware/rtl/dvp_href_frame_v2.v}] [file join $root {hardware/rtl/fw_shared_dma_fabric_snapshot.sv}] [file join $root {hardware/rtl/fw_shared_dma_side_snapshot.sv}] [file join $root {hardware/rtl/snapshot_compute_bdif.sv}] [file join $root {hardware/rtl/snapshot_platform_top.v}] [file join $root {reference/camera_rtl/axi_lite_regs.v}] [file join $root {reference/camera_rtl/camera_ddr_bdif.v}] [file join $root {reference/camera_rtl/camera_monitor.v}] [file join $root {build/20261027T171305_443192Z/area/imported/camera_pair_capture.sv}] [file join $root {reference/camera_rtl/capture_core.v}] [file join $root {reference/camera_rtl/learning_capture.v}] [file join $root {reference/camera_rtl/sccb_master.v}] [file join $root {build/20261027T171305_443192Z/area/imported/fw_sgm_mmio_rtl.sv}] [file join $root {reference/compute/src/fw_shared_dma_backend.sv}] [file join $root {build/20261027T171305_443192Z/area/imported/r14_shared_compute_mmio_v1.sv}]]
