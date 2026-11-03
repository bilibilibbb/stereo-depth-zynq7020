#ifndef RETAINED_NEURAL_DRIVER_H
#define RETAINED_NEURAL_DRIVER_H
#include "retained_neural_abi.h"
typedef struct {
    void *user;
    int (*read32)(void *,uint32_t,uint32_t *);
    int (*write32)(void *,uint32_t,uint32_t);
    void (*flush)(void *,const void *,uint32_t);
    void (*invalidate)(void *,void *,uint32_t);
    void (*barrier)(void *);
    uint64_t (*now_us)(void *);
} rn_io;
typedef struct {
    rn_io io;
    uint32_t base,expected_build,timeout_us,next_tag,ownership;
    uint32_t first_error,last_status,completed,cycles,read_beats;
    uint32_t cached[RN_DESCRIPTOR_WORDS];
    uint8_t cached_valid[RN_DESCRIPTOR_WORDS];
    void *owned_destination;
    uint32_t owned_bytes;
    uint32_t pending_tag,pending_values,pending_polls;
    uint64_t pending_start_us;
} rn_device;
enum {RN_OK=0,RN_ARGUMENT=1,RN_IO_ERROR=2,RN_IDENTITY=3,RN_BUSY_ERROR=4,RN_TIMEOUT=5,RN_HARDWARE=6,RN_STALE=7,RN_RESET_REQUIRED=8,RN_PENDING=9,RN_CANCELLED_ERROR=10};
/* ownership: 0 CPU, 1 submitted, 2 quarantined. No failure silently retries. */
int rn_open(rn_device *device);
/* rn_begin submits one descriptor and returns immediately after its kick.
 * RN_OK means submitted. rn_poll returns RN_PENDING or a final result; it
 * reads one status per call and never waits for the hardware to progress.
 * Source, weights and destination must remain leased and immutable while
 * ownership=1/2. A frame scheduler needs separate arenas and graph state.
 * The deadline includes time spent on other work between polls. */
int rn_begin(rn_device *,const rn_descriptor *,const void *,const void *,void *);
int rn_poll(rn_device *);
int rn_cancel(rn_device *);
/* Compatibility wrapper waits using the exact same begin/poll receipts. */
int rn_submit(rn_device *device,const rn_descriptor *descriptor,const void *source,const void *weights,void *destination);
int rn_recover(rn_device *device);
#endif
