#ifndef FW_SGM_DRIVER_H
#define FW_SGM_DRIVER_H
#include "retained_sgm_driver.h"
#define FW_SGM_NATIVE_COMMITTED 0x4cu
#define FW_SGM_CURVE_COMMITTED 0x50u
#define FW_SGM_PREFIX_TAG 0x54u
#define FW_SGM_PREFIX_ID 0x58u
#define FW_SGM_PREFIX_MAGIC 0x53504731u
/* Single CPU0 controller owns the command and raw buffers. rows counts
 * complete four-native-row/one-curve-row decoder bands,0..64. Only call
 * acquire after prefix_ready proved that band committed. No ownership is
 * released by a prefix; every error still requires actual driver recovery. */
int fw_sgm_prefix_ready(rs_device *,unsigned,unsigned *);
int fw_sgm_prefix_acquire(rs_device *,unsigned);
/* decoded_rows have already been acquired and consumed; successful final
 * release acquires the remaining suffix only. rs_poll keeps its old API. */
int fw_sgm_poll_prefix(rs_device *,unsigned);
#endif
