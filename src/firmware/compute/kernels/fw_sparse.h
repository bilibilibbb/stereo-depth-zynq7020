#ifndef FW_SPARSE_H
#define FW_SPARSE_H
#ifndef FW_SIX_SCRATCH_COALESCE_ENABLE
#define FW_SIX_SCRATCH_COALESCE_ENABLE 1
#endif
#ifndef FW_SIX_PW28_RESIDENT_ENABLE
#define FW_SIX_PW28_RESIDENT_ENABLE 1
#endif
#define FW_SIX_PW28_RESIDENT_ACTIVE (FW_SIX_PW28_RESIDENT_ENABLE && FW_SIX_SCRATCH_COALESCE_ENABLE)
#ifndef FW_POINTWISE_RAW_RETAIN_ENABLE
#define FW_POINTWISE_RAW_RETAIN_ENABLE 1
#endif
#ifndef FW_WEIGHT_BANK_WORDS
#define FW_WEIGHT_BANK_WORDS 64
#endif
#ifndef FW_STEM_HORIZONTAL_PHASE_ENABLE
#define FW_STEM_HORIZONTAL_PHASE_ENABLE 1
#endif
#ifndef FW_STEM_TYPED_CAPTURE_ENABLE
#define FW_STEM_TYPED_CAPTURE_ENABLE 1
#endif
#ifndef FW_STEM_SUM_ROM_ENABLE
#define FW_STEM_SUM_ROM_ENABLE 1
#endif
#ifndef FW_HEAD_PAIR_DRAIN_ENABLE
#define FW_HEAD_PAIR_DRAIN_ENABLE 1
#endif
#define FW_HEAD_PAIR_DRAIN_ACTIVE (FW_HEAD_PAIR_DRAIN_ENABLE && FW_WEIGHT_BANK_WORDS>=48)
_Static_assert(FW_WEIGHT_BANK_WORDS==32 || FW_WEIGHT_BANK_WORDS==64,"bounded four weight-bank depth");
#ifndef FW_SIX_PW32_LINK_ENABLE
#define FW_SIX_PW32_LINK_ENABLE 1
#endif
#ifndef FW_SIX_DW31_SOURCE_ENABLE
#define FW_SIX_DW31_SOURCE_ENABLE 1
#endif
#define FW_SIX_DW31_SOURCE_ACTIVE (FW_SIX_DW31_SOURCE_ENABLE && FW_SIX_PW32_LINK_ENABLE)
#ifndef FW_SIX_DW29_SOURCE_ENABLE
#define FW_SIX_DW29_SOURCE_ENABLE 1
#endif
#ifndef FW_SIX_DUAL_PW30_ENABLE
#define FW_SIX_DUAL_PW30_ENABLE 1
#endif
#define FW_SIX_DUAL_PW30_ACTIVE (FW_SIX_DUAL_PW30_ENABLE && FW_SIX_PW28_RESIDENT_ACTIVE && FW_SIX_DW29_SOURCE_ENABLE && FW_SIX_DW31_SOURCE_ACTIVE)
#define FW_SIX_CACHE_BANK (FW_SIX_DUAL_PW30_ACTIVE?1u:0u)
#ifndef FW_SIX_HEAD_PARTIAL_ENABLE
#define FW_SIX_HEAD_PARTIAL_ENABLE 1
#endif
#define FW_SIX_HEAD_PARTIAL_ACTIVE (FW_SIX_HEAD_PARTIAL_ENABLE && FW_SIX_DUAL_PW30_ACTIVE && FW_HEAD_PAIR_DRAIN_ACTIVE)
#ifndef FW_HEAD_PW26_INPLACE_ENABLE
#define FW_HEAD_PW26_INPLACE_ENABLE 1
#endif
#define FW_HEAD_PW26_INPLACE_ACTIVE (FW_HEAD_PW26_INPLACE_ENABLE && FW_HEAD_PAIR_DRAIN_ACTIVE)
#ifndef FW_PW_SOURCE_LEASE_ENABLE
#define FW_PW_SOURCE_LEASE_ENABLE 1
#endif
#ifndef FW_DW_RAW_LAZY_ENABLE
#define FW_DW_RAW_LAZY_ENABLE 1
#endif
#ifndef FW_DW_RECIPE_VIEW_ENABLE
#define FW_DW_RECIPE_VIEW_ENABLE 1
#endif
#ifndef FW_HEAD_BLOCK_ENABLE
#define FW_HEAD_BLOCK_ENABLE 1
#endif
#define FW_HEAD_BLOCK_ACTIVE (FW_HEAD_BLOCK_ENABLE && FW_HEAD_PW26_INPLACE_ACTIVE)
#ifndef FW_HEAD_PW27_PACKED_ENABLE
#define FW_HEAD_PW27_PACKED_ENABLE 1
#endif
#define FW_HEAD_PW27_PACKED_ACTIVE (FW_HEAD_PW27_PACKED_ENABLE && FW_HEAD_BLOCK_ACTIVE)
#define FW_SIX_HEAD_SAFE_FIRST (FW_HEAD_BLOCK_ACTIVE?1u:FW_HEAD_PW26_INPLACE_ACTIVE?3u:5u)
#define FW_HEAD_CLOBBER_BYTES (FW_HEAD_BLOCK_ACTIVE?12288u:FW_HEAD_PW26_INPLACE_ACTIVE?17408u:22528u)
#ifndef FW_SIX_DW_MASK_FUSE_ENABLE
#define FW_SIX_DW_MASK_FUSE_ENABLE 1
#endif
#include "retained_model.h"
#include <stdint.h>
#include "fw_memory_layout.h"
#define FW_TILE 32u
#define FW_SLOTS 3u
#define FW_CI_MAX 32u
#define FW_HEADER_BYTES 64u
#define FW_LOCAL_PIXELS 8u
#define FW_LOCAL_OUTPUTS 16u
#define FW_DWPW_STORAGE_PIXELS 128u
#define FW_LOCAL_MAC_LANES (FW_LOCAL_PIXELS*FW_LOCAL_OUTPUTS)
#define FW_ZERO_MAP_BYTES 40960u
#ifndef FW_STEM_TAP_REUSE_ENABLE
#define FW_STEM_TAP_REUSE_ENABLE 1
#endif
enum {FW_OK=0, FW_PENDING=1, FW_BUSY=2, FW_ARGUMENT=3,
      FW_STALE=4, FW_PACKET=5, FW_CANCELLED=6};
/* Wire ABI: region IDs/offsets, never a PS virtual pointer. Metadata must
 * be mapped uncached/shareable on A9. Payload is clean-published to PL.
 * An engine completion means all reads/writes for that packet have drained. */
typedef struct {
    uint64_t frame;
    uint32_t epoch,attempt,sequence,source_region,destination_region;
    uint32_t first,source_plane_values,destination_plane_values;
    uint32_t mask_bytes,value_bytes;
    uint16_t layer,count,ci,co;
    uint8_t core,flags;
    uint16_t reserved;
    uint32_t reserved2;
} fw_packet;
typedef struct {
    void (*clean)(void *,const void *,uint32_t);
    void (*invalidate)(void *,void *,uint32_t);
    void (*barrier)(void *);
    void *user;
} fw_cache;
/* These are logical operations and bytes, not timing or DDR measurements.
 * Both controls count in-bounds useful dense terms, not padded DSP lanes. */
typedef struct {
    uint64_t dense_mac,executed_mac,zero_activation_mac,zero_weight_mac;
    uint64_t stem_layers,stem_vectors,stem_row_fills,stem_row_hits,stem_tag_checks;
    uint64_t stem_source_fill_read_bytes,stem_raw_fill_write_bytes,stem_raw_word_read_bytes,stem_original_source_read_bytes;
    uint64_t stem_valid_tap_values,stem_padding_values,stem_weight_bank_load_words,stem_row_bound_microblocks;
    uint64_t stem_geometry_preparations,stem_cancelled_drains,stem_max_raw_bytes;
    uint64_t stem_tap_fills,stem_tap_hits,stem_tap_register_write_bytes,stem_tap_register_read_bytes,stem_tap_term_mappings;
    uint64_t stem_phase_tiles,stem_phase_products_removed,stem_phase_source_values_removed,stem_phase_source_differences;
    uint64_t stem_phase_coefficient_adds,stem_phase_wide_copy_bytes,stem_phase_stash_read_bytes,stem_phase_cancel_weight_reads,stem_phase_zero_control_tokens;
    uint64_t stem_phase_source_extra_substeps;
    uint64_t stem_typed_capture_positions,stem_typed_hold_write_bytes,stem_typed_hold_read_bytes;
    uint64_t stem_sum_rom_requests,stem_sum_rom_read_bytes,stem_sum_register_read_bytes,stem_sum_mask_build_values;
    uint64_t stem_stream_steps,stem_stream_output_rows,stem_stream_input_waits,stem_stream_cache_loads,stem_stream_resume_loads;
    uint64_t source_values,source_read_bytes,converted_values;
    uint64_t token_values,mask_write_bytes,value_write_bytes,header_write_bytes;
    uint64_t packet_read_bytes,output_write_bytes,bitmap_tests;
    uint64_t encoder_zero_tests,depthwise_zero_tests,packet_validation_bits,packet_validation_values;
    uint64_t cpu0_tiles,cpu1_tiles,pl_tiles,retired_tiles,backpressure;
    uint64_t clean_bytes,invalidate_bytes,barriers,claims,publications,acknowledgements;
    uint64_t layer_calls,dense_layers,sparse_layers;
    uint64_t forbidden_reuse,stale_receipts,cancelled_packets;
    uint64_t local_zero_tests,local_source_values,local_tiles;
    uint64_t fused_activation_values,pointwise_load_slots,pointwise_dense_mac_slots,pointwise_used_mac_slots;
    uint64_t pointwise_nonzero_tokens,pointwise_local_fifo_write_bytes,pointwise_local_fifo_read_bytes,pointwise_functional_ticks;
    uint64_t identity_input_values,nonidentity_input_values,dense_preparation_values_removed,dense_preparation_read_bytes_removed,dense_preparation_write_bytes_removed;
    uint64_t exact_output_expansion_values;
    uint64_t head_rows,head_owner_loads,head_raw_rows,head_raw_read_bytes,head_raw_write_bytes,head_raw_or_values;
    uint64_t head_PW26_local_write_bytes,head_PW26_local_read_bytes,head_tensor53_write_bytes_removed,head_tensor53_read_bytes_removed,head_logit_write_bytes,head_weight_words;
    uint64_t head_block_groups,head_block_recipe_direct_read_bytes,head_block_recipe_pointer_bindings;
    uint64_t head_PW27_packed_tiles,head_PW27_packed_vectors,head_PW27_summary_bytes,head_PW27_mask_merges;
    uint64_t fused_dw_recipe_view_bindings;
    uint64_t head_DW25_quad_channels,head_DW25_quad_windows,head_DW25_quad_products,head_DW25_horizontal_products_removed,head_DW25_operand_bytes_removed;
    uint64_t pointwise_vector_zero_tests,pointwise_vector_or_values,pointwise_source_replay_values;
    uint64_t pointwise_padded_dense_lanes,pointwise_padded_used_lanes,pointwise_partial_zero_lanes_executed;
    uint64_t pointwise_weight_bank_load_words,pointwise_weight_bank_read_words;
    uint64_t pointwise_read_requests,pointwise_operand_transfers,pointwise_product_transfers,pointwise_feedback_commits;
    uint64_t pointwise_fifo_metadata_reads,pointwise_pipeline_drain_steps,pointwise_ready_stalls,pointwise_result_rows;
    uint64_t zero_summary_producer_values,zero_summary_write_bytes,zero_summary_read_bytes;
    uint64_t zero_summary_source_values_omitted,zero_summary_vectors_omitted,pointwise_vector_candidates;
    uint64_t zero_summary_replayed_values_omitted;
    uint64_t fused_dw_pw_layers,fused_dw_pw_tiles,fused_dw_intermediate_values;
    uint64_t fused_dw_interface_write_bytes_removed,fused_pw_interface_read_bytes_removed;
    uint64_t fused_dw_tile_write_bytes,fused_dw_tile_read_bytes,fused_dw_tile_or_values;
    uint64_t fused_dw_tile_summary_tests,fused_dw_commands,fused_pw_commands,fused_mode_drains;
    uint64_t fused_dw_coefficient_command_bytes;
    uint64_t fused_pw_weight_reload_bytes,fused_dw_coefficient_loads,fused_dw_coefficient_reuse_commands,fused_storage_block_max_bytes;
    uint64_t fused_dw_geometry_old_pixel_sites,fused_dw_geometry_prepared_rows,fused_dw_geometry_block_divisions,fused_dw_geometry_layer_divisions;
    uint64_t fused_dw_geometry_descriptor_write_bytes,fused_dw_geometry_descriptor_read_bytes;
    uint64_t fused_dw_geometry_edge_write_bytes,fused_dw_geometry_edge_read_bytes;
    uint64_t fused_dw_geometry_old_bounds_tests,fused_dw_geometry_preparation_bounds_tests,fused_dw_geometry_boundary_mask_tests;
    uint64_t fused_dw_geometry_interior_values,fused_dw_geometry_boundary_values,fused_dw_geometry_tap_read_bytes,fused_dw_geometry_channel_row_setups;
    uint64_t fused_dw_zero_summary_admitted_layers,fused_dw_zero_window_preparations,fused_dw_zero_summary_read_bytes;
    uint64_t fused_dw_zero_bypassed_commands,fused_dw_zero_bypassed_values,fused_dw_zero_bypassed_terms;
    uint64_t fused_dw_zero_source_read_bytes_removed,fused_dw_zero_bias_formats;
    uint64_t fused_dw_summary_cache_fills,fused_dw_summary_cache_evictions,fused_dw_summary_cache_fill_read_bytes;
    uint64_t fused_dw_summary_cache_local_write_bytes,fused_dw_summary_cache_tag_initializations,fused_dw_summary_cache_configured_bytes;
    uint64_t fused_dw_group_dense_commands,fused_dw_group_channel_admissions,fused_dw_group_admission_rows;
    uint64_t fused_dw_group_coefficient_loads,fused_dw_group_row_setups,fused_dw_group_feed_packets,fused_dw_group_inactive_channel_values;
    uint64_t fused_dw_raw_span_preparations,fused_dw_raw_group_restarts,fused_dw_raw_tag_initializations,fused_dw_raw_tag_tests;
    uint64_t fused_dw_raw_row_fills,fused_dw_raw_row_hits,fused_dw_raw_fill_read_bytes,fused_dw_raw_fill_write_bytes;
    uint64_t fused_dw_raw_feed_words,fused_dw_raw_local_read_bytes,fused_dw_raw_operand_write_bytes,fused_dw_raw_operand_read_bytes;
    uint64_t fused_dw_raw_max_payload_bytes,fused_dw_raw_max_reserved_bytes;
    uint64_t fused_dw_raw_full_layers,fused_dw_raw_selected_span_layers;
    uint64_t fused_dw_storage128_layers,fused_dw_storage64_layers;
    uint64_t fused_dw_storage80_layers,coarse_DW_two_row_pieces,DW11_PW12_four_row_batches,DW13_PW14_four_row_batches;
    uint64_t fused_dw_minimal_pairs,fused_dw_minimal_original_multiplications,fused_dw_minimal_multiplications;
    uint64_t fused_dw_minimal_input_adds,fused_dw_minimal_output_adds,fused_dw_minimal_exact_halves;
    uint64_t fused_dw_minimal_coefficient_adds,fused_dw_minimal_coefficient_scales,fused_dw_minimal_coefficient_bytes;
    uint64_t fused_dw_minimal_operand_read_bytes_removed,fused_dw_minimal_padding_values;
    uint64_t pointwise_8pixel_groups,pointwise_source_substeps,pointwise_narrow_tiles,pointwise_narrow_vector_candidates;
    uint64_t pointwise_fifo_value_bytes,pointwise_fifo_control_bytes;
    uint64_t pointwise_summary_empty_macros,pointwise_8pixel_groups_loaded;
    uint64_t final_stream_steps,final_stream_rows,final_stream_cache_loads,final_stream_resume_loads,final_stream_weight_words;
    uint64_t tail_stream_layers,tail_stream_rows,tail_stream_steps,tail_stream_cache_loads,tail_stream_resume_loads,tail_stream_weight_words;
    uint64_t tail_local_PW_write_bytes,tail_local_PW_read_bytes,tail_local_summary_write_bytes;
    uint64_t tail_unused_DW_values,tail_unused_PW_values,tail_PW_full_write_bytes_removed,tail_PW_external_read_bytes_removed;
    uint64_t suffix_PW28_layers,suffix_DW29_PW30_layers,suffix_unused_PW28_values,suffix_unused_DW29_values,suffix_unused_PW30_values;
    uint64_t upstream_triple_layers,upstream_PW28_rows,upstream_PW28_packed_write_bytes;
    uint64_t upstream_PW28_full_write_bytes_removed,upstream_PW28_external_fill_read_bytes_removed;
    uint64_t upstream_PW28_summary_cache_invalidations,upstream_PW28_row_checks;
    uint64_t six_stream_layers,six_stream_rows,six_PW30_rows,six_cache_loads,six_weight_words,six_source_waits;
    uint64_t six_PW32_inplace_tiles;
    uint64_t six_PW32_link_tiles,six_PW32_link_formats,six_PW32_link_seed_vectors,six_PW32_carrier_write_bytes_removed;
    uint64_t six_PW32_carrier_read_bytes_removed,six_PW32_feedback_read_bytes,six_PW32_link_mask_values;
    uint64_t six_DW31_source_blocks,six_DW31_source_groups,six_DW31_direct_vectors,six_DW31_carry_vectors;
    uint64_t six_DW31_carrier_write_bytes_removed,six_DW31_carrier_read_bytes_removed,six_DW31_carry_write_bytes,six_DW31_carry_read_bytes;
    uint64_t six_DW31_source_mask_groups,six_DW31_source_mask_values,six_DW31_engine_drains,six_DW31_coefficient_hold_bytes;
    uint64_t six_DW31_mask_RAW_read_bytes,six_DW31_mask_FIFO_read_bytes,six_DW31_mask_reread_values_removed,six_DW31_zero_carry_write_bytes_removed;
    uint64_t six_DW29_source_blocks,six_DW29_source_groups,six_DW29_direct_vectors,six_DW29_carry_vectors;
    uint64_t six_DW29_carrier_write_bytes_removed,six_DW29_carrier_read_bytes_removed,six_DW29_carry_write_bytes,six_DW29_carry_read_bytes;
    uint64_t six_DW29_source_mask_groups,six_DW29_source_mask_values,six_DW29_engine_drains,six_DW29_coefficient_hold_bytes;
    uint64_t six_DW29_mask_RAW_read_bytes,six_DW29_mask_FIFO_read_bytes,six_DW29_mask_reread_values_removed,six_DW29_zero_carry_write_bytes_removed;
    uint64_t six_PW28_retained_halos,six_PW28_resident_restores,six_PW28_spill_bytes_removed,six_PW28_restore_bytes_removed;
    uint64_t six_PW28_flush_batches,six_PW28_flush_bytes,six_shared_summary_tag_resets;
    uint64_t six_PW30_retained_halos,six_PW30_resident_restores,six_PW30_spill_bytes_removed,six_PW30_restore_bytes_removed;
    uint64_t six_PW30_flush_batches,six_PW30_flush_bytes,six_PW30_row_allocations,six_PW30_row_retirements;
    uint64_t six_PW30_address_lookups,six_compat_scratch_write_bytes,six_compat_scratch_read_bytes;
    uint64_t six_HEAD_retention_checks,six_HEAD_retained_rows,six_HEAD_flush_bytes,six_HEAD_partial_restores,six_HEAD_partial_restore_rows;
    uint64_t six_halo_spill_batches,six_halo_restore_batches,six_halo_spill_read_bytes,six_halo_spill_write_bytes;
    uint64_t six_halo_restore_read_bytes,six_halo_restore_write_bytes,six_halo_metadata_checks,six_summary_invalidations;
    uint64_t six_PW30_native_write_bytes,six_PW30_full_write_bytes_removed,six_PW30_base_source_fill_bytes_removed,six_mode_changes;
    uint64_t shared_weight_owner_checks,shared_weight_owner_hits,shared_weight_owner_writes,shared_weight_uncached_words;
    uint64_t shared_weight_requested_words,shared_weight_segment_checks,shared_weight_segment_hits,shared_weight_segment_writes,shared_weight_segment_clears;
    uint64_t joint_stem_load_requests,joint_stem_spill_checks,joint_stem_spill_loads,joint_stem_spill_reads,joint_stem_weight_routes,joint_stem_bank_reads,joint_stem_tile_mode_checks;
    uint64_t PW10_cache_loads,PW10_resume_loads,PW10_weight_words;
    uint64_t PW9_cache_loads,PW9_weight_words,PW9_rows,PW9_blocks;
    uint64_t PW10_finite_source_vectors,PW10_prior_source_vectors;
    uint64_t fused_dw_quad_plan_blocks,fused_dw_quad_plan_active_blocks,fused_dw_quad_plan_write_bytes,fused_dw_quad_plan_tests,fused_dw_quad_plan_reads;
    uint64_t fused_dw_quad_feed_groups,fused_dw_quad_windows,fused_dw_quad_original_products,fused_dw_quad_horizontal_products,fused_dw_quad_products;
    uint64_t fused_dw_quad_input_adds,fused_dw_quad_output_adds,fused_dw_quad_exact_quarters,fused_dw_quad_operand_bytes_removed;
    uint64_t fused_dw_quad_coefficient_adds,fused_dw_quad_coefficient_scales,fused_dw_quad_coefficient_bytes,fused_dw_quad_fourth_row_acquires;
    uint64_t fused_dw_dilated_plan_write_bytes,fused_dw_dilated_plan_tests,fused_dw_dilated_plan_reads,fused_dw_dilated_feed_groups;
    uint64_t fused_dw_dilated_pairs,fused_dw_dilated_original_products,fused_dw_dilated_products,fused_dw_dilated_rows;
    uint64_t fused_dw_stride2_pairs,fused_dw_stride2_rows,fused_dw_stride2_original_products,fused_dw_stride2_products,fused_dw_stride2_operand_bytes_removed;
    uint64_t fused_dw_stride2_input_adds,fused_dw_stride2_output_adds,fused_dw_stride2_coefficient_channels,fused_dw_stride2_feed_groups;
    uint64_t fused_dw_dilated_operand_bytes_removed,fused_dw_dilated_coefficient_channels;
    uint64_t fused_dw_recipe_layer_checks,fused_dw_recipe_H_loads,fused_dw_recipe_quad_loads,fused_dw_recipe_read_bytes;
    uint64_t six_DW31_recipe_channels,six_DW31_pairs,six_DW31_original_products,six_DW31_products,six_DW31_operand_bytes_removed;
    uint64_t six_DW31_input_adds,six_DW31_output_adds,six_DW31_row_taps;
    uint64_t six_preserve_checks,six_preserve_reuses,six_preserve_terminal_skips,six_preserve_receipt_checks;
    uint64_t pointwise_old_group_pixel_tests,pointwise_group_valid_tests,pointwise_stem_valid_tests,pointwise_control_copy_values;
    uint64_t pointwise_old_product_writes,pointwise_actual_product_writes,pointwise_old_feedback_adds,pointwise_actual_feedback_adds,pointwise_product_mask_latches,pointwise_feedback_mask_tests;
    uint64_t pointwise_pixel_source_tests,pointwise_pixel_mask_classifications,pointwise_pixel_mask_tests,pointwise_pixel_mask_popcounts;
    uint64_t pointwise_partial_zero_pixels_removed,pointwise_partial_zero_mac_removed,pointwise_nonstem_OR_values_removed,pointwise_summary_nonzero_allzero_groups;
    uint64_t pixel_summary_producer_tests,pixel_summary_bit_sets,pixel_summary_uniform_groups,pixel_summary_DW_OR_values_removed;
    uint64_t pointwise_summary_pixel_masks,pointwise_summary_zero_source_values_omitted,pointwise_summary_zero_source_tests_omitted;
    uint64_t DW11_PW12_batches,DW11_PW12_rows,DW11_PW12_weight_words,DW11_PW12_control_restore_bytes;
    uint64_t DW13_PW14_batches,DW13_PW14_rows,DW13_PW14_weight_words,DW13_PW14_control_restore_bytes;
    uint64_t coarse_raw_cold11,coarse_raw_warm11,coarse_raw_reused_rows11,coarse_raw_reused_bytes11;
    uint64_t coarse_raw_cold13,coarse_raw_warm13,coarse_raw_reused_rows13,coarse_raw_reused_bytes13;
    uint64_t DW1_PW2_batches,DW1_PW2_rows,DW1_PW2_weight_words,DW1_PW2_control_restore_bytes,DW1_PW2_early_batches,DW1_raw_cold,DW1_raw_warm;
} fw_counts;
size_t fw_context_bytes(void);
size_t fw_local_context_bytes(void);
size_t fw_context_registered_bytes(const void *);
int fw_init(void *,size_t,const fw_cache *);
int fw_init_local(void *,size_t,const fw_cache *);
/* Functional ready-pattern injection for the synchronous port model only;
 * bit0 stalls source, bit1 the weight/read stage, bit2 the operand stage,
 * bit3 the output formatter. It never changes arithmetic or HW width. */
int fw_issue_stalls_set(void *,unsigned);
/* Audit a real completed source row while the native layer stays entered.
 * The optional observer never supplies a candidate operand or output. */
typedef void (*fw_stem_observer)(void *,unsigned,unsigned,const uint64_t *);
int fw_stem_observed(void *,int,const rn_parameter *,const void *,int,int,int16_t *,int,int,int,int,int,fw_stem_observer,void *);
int fw_stem_test_corrupt_source(void *);
/* One shared physical raw/weight port state, reused by both frame leases.
 * A row step drains accepted operands and writes before yielding. Only a
 * clean, contiguous input frontier may be consumed. Other PL work must
 * invalidate this shared state before the next stem resumes. */
size_t fw_stem_stream_bytes(void);
int fw_stem_stream_init(void *,size_t);
int fw_stem_stream_invalidate(void *);
/* Explicit invalidation before generic/external bank writers. Raw-only
 * BOX mode changes preserve physical weight words and their owner tags. */
int fw_stem_stream_weights_invalidate(void *);
/* Quiescent/sole-PL directed inspection only; returns0 without a cached
 * raw owner. It conveys no source/read loan or permission to edit banks. */
unsigned fw_stem_stream_owner_mode(const void *);
/* Controller lookup under its sole PL gate; no source payload loan. */
const void *fw_stem_stream_owner_context(const void *);
int fw_shared_weight_residency_checks(void *,void *,uint64_t [12]);
int fw_encoder_joint_weight_checks(void *,void *,uint64_t [12]);
int fw_pointwise_vector_group_checks(void *,uint64_t [4]);
int fw_pointwise_product_mask_checks(void *,uint64_t [7]);
int fw_pointwise_pixel_mask_checks(void *,uint64_t [8]);
/* Validation-build only: scalar bit checks over real stored outputs,
 * which are independently checked by the original convolution. */
void fw_pixel_summary_bit_proofs(uint64_t [2]);
int fw_stem_stream_open(void *,const void *,int,int,int16_t *,int,int);
int fw_stem_stream_step(void *,void *,unsigned);
int fw_stem_stream_rows(const void *,unsigned *);
int fw_final_stream_open(void *,const void *,int16_t *);
int fw_final_stream_step(void *,void *);
int fw_final_stream_rows(const void *,unsigned *);
int fw_final_ring_open(void *,const void *,int16_t *);
int fw_final_ring_row(void *,void *,int16_t *,unsigned);
int fw_final_ring_rows(const void *,unsigned *);
typedef int (*fw_tail_row_check)(void *,const void *,unsigned,const int16_t *,const int16_t *,const int16_t *);
int fw_tail_ring_open(void *,const void *,int16_t *);
int fw_tail_ring_row(void *,void *,int16_t *,unsigned,fw_tail_row_check);
int fw_tail_ring_rows(const void *,unsigned *);
int fw_PW28_suffix_backend(void *,int,const rn_parameter *,const void *,int,int,int16_t *,int,int,int,int,int);
int fw_DW29_PW30_suffix_backend(void *,int,int,const void *,int,int,int16_t *,int,int,int,int,int,int,int,int,int);
int fw_zero_summary_domain(const void *,const void *,unsigned *,unsigned *,unsigned *);
int fw_stem_stream_test_observer(void *,fw_stem_observer,void *);
#ifdef R14_HOST_REFERENCE
typedef void (*fw_pointwise_source_probe)(void *,unsigned,unsigned,unsigned,unsigned);
int fw_pointwise_source_probe_set(void *,fw_pointwise_source_probe,void *);
typedef void (*fw_tail_source_probe)(void *,unsigned);
int fw_tail_source_probe_set(void *,fw_tail_source_probe,void *);
typedef int (*fw_dw_quad_probe)(void *,unsigned,unsigned,unsigned,const uint8_t *,const int16_t *);
int fw_dw_quad_probe_set(void *,fw_dw_quad_probe,void *);
typedef int (*fw_dw_dilated_probe)(void *,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,const uint8_t *,const int16_t *);
typedef int (*fw_dw_stride2_probe)(void *,unsigned,unsigned,unsigned,unsigned,unsigned,const uint8_t *,const int16_t *);
int fw_dw_stride2_probe_set(void *,fw_dw_stride2_probe,void *);
int fw_dw_dilated_probe_set(void *,fw_dw_dilated_probe,void *);
#endif
int fw_output_zero_summary_set(void *,int);
/* External numerical writers invalidate old pointer-based summary identity
 * before changing a tensor; they never publish a fabricated block bitmap. */
int fw_external_output_invalidate(void *);
int fw_neural_memory_layout(const void *,fw_memory_span *,unsigned,unsigned *);
int fw_frame_begin(void *);
int fw_open(void *,uint64_t,uint32_t,uint32_t,int,const rn_parameter *,
            const void *,int,int,int16_t *,int,int,int,int,int);
int fw_cpu_step(void *,unsigned);
int fw_pl_step(void *,unsigned);
int fw_retire(void *);
int fw_cancel(void *);
int fw_layer_status(void *);
int fw_completion_receipt(void *,uint32_t,uint32_t,uint32_t);
int fw_counts_get(const void *,fw_counts *);
/* Read-only directed-test hook: corrupt only a producer-published packet;
 * no raw pointer exposure and no mutable frozen parameters. */
int fw_test_corrupt_ready(void *,unsigned);
int fw_typed_backend(void *,int,const rn_parameter *,const void *,int,int,
                     int16_t *,int,int,int,int,int);
int fw_dense_backend(void *,int,const rn_parameter *,const void *,int,int,
                     int16_t *,int,int,int,int,int);
/* Primary variant: exact typed input read in the PL consumer, no PS token
 * scan, packed DDR staging, or per-packet payload validation. A local tile
 * holds only the accumulators; all output lanes share one input term. */
int fw_local_backend(void *,int,const rn_parameter *,const void *,int,int,
                     int16_t *,int,int,int,int,int);
int fw_local_dense_issue_backend(void *,int,const rn_parameter *,const void *,int,int,
                                 int16_t *,int,int,int,int,int);
/* Immediate DW/PW pair.128-pixel STORAGE reuses the SAME128 multipliers:
 * <=4 selected original DW channels, <=64 row pixels, and original8x16 PW.
 * Whole-block zero-channel admission trades away partial-window savings;
 * native paired DW applies only to even stride1/dilation1 commands.
 * Selected source rows share the existing32KiB scratch with the8192B tile;
 * source fills and skewed four-channel operands precede MAC consumption.
 * When bounded all-channel rows fit, static original groups preserve cache
 * identity through PW and subsequent storage blocks; tighter shapes retain
 * selected-channel span fills. Neither mode adds a source-bank port.
 * Wide PW groups reload the existing four32-row banks per storage block;
 * smaller groups remain resident. Output must be disjoint from input.
 * An optional verifier observes candidate DW blocks and channel pitch;
 * it never supplies a candidate operand or output. */
typedef void (*fw_dw_observer)(void *,unsigned,unsigned,const int16_t *,unsigned);
typedef int (*fw_PW28_packed_row_check)(void *,const void *,unsigned,const uint64_t *,unsigned,unsigned);
typedef int (*fw_six_row_check)(void *,const void *,int,unsigned,const void *,unsigned,unsigned);
int fw_six_ring_open(void *,const void *,int16_t *,void *,void *);
/* Compiled numeric/summary extent inside the unchanged32KiB RAW. */
unsigned fw_six_ring_raw_region_bytes(void);
int fw_six_ring_set_ready(void *,unsigned);
int fw_six_ring_rows(const void *,unsigned *);
int fw_six_ring_row(void *,void *,int16_t *,unsigned,fw_six_row_check);
int fw_six_ring_checkpoint(void *,void *);
int fw_six_ring_preserve(void *,void *);
/* Sole PL gate: cached preservation and authenticated general-writer flush. */
int fw_six_ring_preserve_cached(void *,void *);
int fw_six_ring_flush_resident(void *,void *);
int fw_six_ring_preserve_HEAD(void *,void *);
int fw_six_ring_HEAD_ready(void *);
const void *fw_six_ring_cached_context(const void *,unsigned);
int fw_six_ring_cached_drop(void *,const void *);
/* Actual row span: pointer starts at first_column; pitch remains channel stride.
 * Full-row paths pass first0/count80, bounded paths pass first0..64/count16. */
typedef int (*fw_head_row_check)(void *,const void *,int,unsigned,const int16_t *,unsigned,unsigned,unsigned);
typedef int (*fw_head_packet_ready)(void *,unsigned,unsigned,const int16_t *);
int fw_head_batch(void *,void *,const void *,int16_t *,int16_t *,unsigned,unsigned,uint64_t,uint32_t,uint32_t,fw_head_row_check,fw_head_packet_ready,void *);
int fw_head_begin(void *);
#ifdef R14_HOST_REFERENCE
int fw_six_test_corrupt_halo(void *,unsigned);
void fw_weight_program_reset(void);
void fw_weight_program_stats(uint64_t [35]);
int fw_stem_phase_checks(uint64_t [10]);
int fw_stem_sum_rom_checks(uint64_t [5]);
int fw_six_test_halo_storage(const void *,const void **,const void **);
int fw_six_test_corrupt_resident(void *,void *,unsigned);
int fw_six_test_poison_raw(void *);
int fw_six_resident_checks(void *,void *,uint64_t [14]);
int fw_six_HEAD_partial_checks(void *,void *,uint64_t [10]);
int fw_six_test_clobber_HEAD(void *);
int fw_six_test_corrupt_partial(void *,void *,unsigned);
int fw_six_test_HEAD_row_classes(const void *,const void *,unsigned *,unsigned *,unsigned *);
int fw_six_test_compatibility(void *,unsigned);
int fw_six_test_pool(const void *,unsigned [3]);
int fw_six_test_snapshot_retired(const void *);
int fw_pointwise_RAW_lifetime_checks(void *,void *,uint64_t [9]);
int fw_pointwise_RAW_snapshot(const void *,void *,unsigned *);
int fw_feedback_source_state(const void *,unsigned [5]);
int fw_feedback_source_corrupt(void *,unsigned);
int fw_six_feedback_checks(void *,uint64_t [7]);
int fw_DW31_source_state(const void *,unsigned [5]);
int fw_DW31_source_corrupt(void *,unsigned);
int fw_six_native_source_checks(void *,uint64_t [7]);
int fw_six_native_DW29_checks(void *,uint64_t [7]);
#endif
#ifdef R14_HOST_REFERENCE
int fw_test_active_parameter(const void *);
#endif
int fw_PW28_DW29_PW30_suffix_observed(void *,const void *,int16_t *,fw_PW28_packed_row_check,void *,fw_dw_observer,void *);
int fw_PW28_DW29_PW30_suffix_backend(void *,int,const rn_parameter *,const void *,int,int,int16_t *,int,int,int,int,int);
int fw_DW29_PW30_suffix_observed(void *,int,int,const void *,int,int,int16_t *,int,int,int,int,int,int,int,int,int,fw_dw_observer,void *);
typedef int (*fw_pair_backend)(void *,int,int,const void *,int,int,int16_t *,int,int,int,int,int,int,int,int,int);
int fw_dwpw_backend(void *,int,int,const void *,int,int,int16_t *,int,int,int,int,int,int,int,int,int);
/* Shared immutable transformed coefficient table; NULL for unsupported
 * identity/recipe. No new per-device ROM copy. recipe0=quad,2=stride2. */
const int32_t *fw_dw_recipe_coefficients(unsigned,const rn_parameter *,unsigned);
/* Frozen PW27 packed17 program, term-major four64-bit bank words. */
const uint64_t (*fw_head_packed17_coefficients(const rn_parameter *))[4];
int fw_dwpw_observed(void *,int,int,const void *,int,int,int16_t *,int,int,int,int,int,int,int,int,int,fw_dw_observer,void *);
int fw_environment(const void *,fw_cache *,uint64_t *,uint32_t *);
int fw_output_activation_set(void *,int);
int fw_output_activation_get(const void *);
int fw_output_storage_set(void *,int,int);
int fw_output_storage_fraction(const void *);
int fw_PW10_stream_open(void *,const void *,int16_t *);
int fw_PW9_row_batch(void *,void *,const void *,int16_t *,unsigned,int,int,int);
int fw_DW11_PW12_row_batch(void *,void *,const int16_t *,int16_t *,unsigned,unsigned,fw_dw_observer,void *);
int fw_stem_DW1_PW2_row_batch(void *,void *,const int16_t *,int16_t *,unsigned,unsigned,unsigned,fw_dw_observer,void *);
int fw_stem_DW1_PW2_progress(const void *,unsigned *,const int16_t **,const int16_t **);
int fw_DW13_PW14_row_batch(void *,void *,const int16_t *,int16_t *,unsigned,unsigned,fw_dw_observer,void *);
int fw_DW11_PW12_row_grain(void *,void *,const int16_t *,int16_t *,unsigned,unsigned,unsigned,fw_dw_observer,void *);
int fw_DW13_PW14_row_grain(void *,void *,const int16_t *,int16_t *,unsigned,unsigned,unsigned,fw_dw_observer,void *);
int fw_DW_pipeline_summary_state(const void *,unsigned *,unsigned *,unsigned *,unsigned *,unsigned *);
int fw_coarse_raw_snapshot(const void *,unsigned,unsigned [5],unsigned *,const void **,const void **,const int16_t **);
#ifdef R14_HOST_REFERENCE
int fw_DW_pipeline_corrupt_summary_bank(void *,unsigned,unsigned);
int fw_coarse_raw_corrupt_identity(void *,unsigned,unsigned);
#endif
int fw_PW10_stream_step(void *,void *,unsigned);
int fw_PW10_stream_rows(const void *,unsigned *);
int fw_full_forward(void *,int,const float *,const float *,const float *,
                    const float *,const float *,const float *,float *,void *,size_t);
#endif
