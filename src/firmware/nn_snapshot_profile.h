#ifndef SS_NN_SNAPSHOT_PROFILE_H
#define SS_NN_SNAPSHOT_PROFILE_H
#include "fw_nn_device.h"
/* Snapshot ABI15 preserves ABI14 compute/DMA descriptors, but deliberately
 * has no hardware result-summary readout. Bit18 advertises that absence.
 * The software activation zero maps and numerical output remain unchanged.
 * ABI14 clients reject this profile rather than misreading zero summaries. */
#define SS_NN_SNAPSHOT_ABI 15u
#define SS_NN_CAP_NO_RESULT_SUMMARY (1u<<18)
#define SS_NN_SNAPSHOT_CAPS (262143u|SS_NN_CAP_NO_RESULT_SUMMARY)
#undef FW_NN_MMIO_ABI
#undef FW_NN_MMIO_CAPS
#define FW_NN_MMIO_ABI SS_NN_SNAPSHOT_ABI
#define FW_NN_MMIO_CAPS SS_NN_SNAPSHOT_CAPS
#endif
