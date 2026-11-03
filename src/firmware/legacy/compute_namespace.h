#ifndef SS_LEGACY_COMPUTE_NAMESPACE_H
#define SS_LEGACY_COMPUTE_NAMESPACE_H
/* Force-include only for the legacy compute translation units. The retained
 * camera exports its own rc_* ABI and is intentionally compiled unchanged. */
#define rc_io ss_lg_rc_io
#define rc_buffer ss_lg_rc_buffer
#define rc_config ss_lg_rc_config
#define rc_engine ss_lg_rc_engine
#define rc_device ss_lg_rc_device
#define rc_open ss_lg_rc_open
#define rc_begin ss_lg_rc_begin
#define rc_poll ss_lg_rc_poll
#define rc_cancel ss_lg_rc_cancel
#define rc_recover_begin ss_lg_rc_recover_begin
#define rc_recover_poll ss_lg_rc_recover_poll
#define rc_config_begin ss_lg_rc_config_begin
#define rc_config_poll ss_lg_rc_config_poll
#endif
