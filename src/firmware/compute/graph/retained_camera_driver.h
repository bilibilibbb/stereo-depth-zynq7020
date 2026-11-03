#ifndef RETAINED_CAMERA_DRIVER_H
#define RETAINED_CAMERA_DRIVER_H
#include "retained_neural_driver.h"
#include "retained_board_layout.h"
#define RC_FRAME_BYTES 614400u
typedef struct {
    rn_io io;
    uint32_t base,sensor_base,sensor_build,cookie,mode_tag,format,max_skew_ticks,timeout_us;
    uint32_t opened,first_error,detail,active_slot;
    uint32_t ownership[2],epoch[2],descriptor[2][64],last_source[2],source_seen;
    uint64_t started_us[2];
    void *buffers[2][2];
} rc_device;
/* ownership0=free,1=hardware,2=CPU read lease,3=quarantined.
 * On failure preserve descriptors and buffers; there is no automatic reset.
 * Only capture/configure registers are touched. Sensor setup is separate. */
int rc_open(rc_device *device);
int rc_arm(rc_device *device,uint32_t slot);
int rc_poll(rc_device *device,uint32_t slot);
int rc_release(rc_device *device,uint32_t slot);
int rc_cancel(rc_device *device,uint32_t slot);
#endif
