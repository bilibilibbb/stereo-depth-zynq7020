#ifndef RETAINED_SGM_DRIVER_H
#define RETAINED_SGM_DRIVER_H
#include "retained_neural_driver.h"

#define RS_MAGIC 0x524f3134u
#define RS_ABI 0x00010000u
#define RS_CENSUS_BYTES 307200u
#define RS_NATIVE_BYTES 614400u
#define RS_CURVE_BYTES 368640u
enum { RS_PENDING=9, RS_CANCELLED=10 };
typedef struct {
    uint32_t left_address,right_address,curve_address,native_address,timeout_cycles;
    const void *left,*right;
    void *curve,*native;
} rs_frame;
typedef struct {
    rn_io io;
    uint32_t base,expected_build,timeout_us,next_tag,ownership,opened;
    uint32_t first_error,last_status,completed,cycles,read_beats,write_beats;
    uint32_t polls,cached[6];
    uint8_t cached_valid[6];
    uint64_t started_us;
    void *owned_curve,*owned_native;
} rs_device;
/* The caller retains both input and output buffers until poll/recover returns OK.
 * A failure quarantines all buffers. Cancel does not imply that AXI has drained.
 * start/poll permit CPU work while SGM owns its separate DDR buffers. */
int rs_open(rs_device *device);
int rs_start(rs_device *device,const rs_frame *frame);
int rs_poll(rs_device *device);
int rs_submit(rs_device *device,const rs_frame *frame);
int rs_cancel(rs_device *device);
int rs_recover(rs_device *device);
#endif
