#ifndef R14_SHARED_COMPUTE_H
#define R14_SHARED_COMPUTE_H
#include <stdint.h>
#include <stddef.h>
#include "retained_neural_abi.h"

enum {RC_NN=0,RC_INTEGER=1,RC_FIELD=2,RC_BOX=3,RC_MAX_BUFFERS=48};
enum {RC_OK=0,RC_PENDING=1,RC_AGAIN=2,RC_ARGUMENT=3,RC_IO_ERROR=4,
      RC_IDENTITY=5,RC_HARDWARE=6,RC_STALE=7,RC_TIMEOUT=8,
      RC_CANCELLED=9,RC_RESET_REQUIRED=10,RC_LEASE_CONFLICT=11};
enum {RC_IDLE=0,RC_JOB=1,RC_QUARANTINE=2,RC_CLEAR=3,RC_CONFIG=4};
typedef struct {
    void *user;
    int (*read32)(void *,uint32_t,uint32_t *);
    int (*write32)(void *,uint32_t,uint32_t);
    void (*flush)(void *,const void *,uint32_t);
    void (*invalidate)(void *,void *,uint32_t);
    void (*barrier)(void *);
    uint64_t (*now_us)(void *);
} rc_io;
typedef struct {void *pointer;uint32_t address,bytes;uint8_t write;} rc_buffer;
typedef struct {
    uint8_t kind,index,bank,slot,scalar,wide;
    uint64_t data;
} rc_config;
typedef struct {
    uint32_t state,first_error,status,next_tag,pending_tag,expected_values;
    uint32_t accepted_before,completed_before,cycles,read_beats,completed;
    uint64_t started_us;
    uint32_t descriptor[81];uint8_t descriptor_valid[81];
    rc_buffer buffers[RC_MAX_BUFFERS];uint32_t buffer_count;
    void *output_cache_pointer;uint32_t output_cache_bytes;
} rc_engine;
typedef struct {
    rc_io io;uint32_t base,expected_build,timeout_us,opened;
    rc_engine engine[4];
    rc_config input_config[40],output_config[8];
    rc_buffer input_buffer[40],output_buffer[8];
    uint8_t input_valid[40],output_valid[8],program_valid[64];
    uint8_t field_mode,native_valid[5];
    uint32_t native_config[5];rc_buffer native_buffer[4];
    rc_config pending_config;rc_buffer pending_config_buffer;
    uint32_t config_before;
} rc_device;

/* One cooperative caller owns this device; callbacks must not re-enter it.
 * Flush callbacks clean and invalidate dirty cache lines before PL writes
 * (the deployment uses Xil_DCacheFlushRange). The owner must not touch a
 * leased output, including channels the current command does not write.
 * All four pages share one physical buffer registry. Physical and host
 * cache-line overlap is rejected if either user writes. Buffers remain
 * leased after any error until confirmed hardware drain and clear.
 * RC_OK from begin means submitted; only a successful poll releases data.
 * Field buffers are ordered: non-scalar inputs, followed by all outputs.
 * Other buffers are ordered: source, optional second source/weights, output.
 * Clock-based deadlines include time spent doing other work between polls.
 * MMIO callbacks return an error on a failed AXI transaction. */
int rc_open(rc_device *);
int rc_begin(rc_device *,unsigned,const uint32_t *,unsigned,const rc_buffer *,unsigned);
int rc_poll(rc_device *,unsigned);
int rc_cancel(rc_device *,unsigned);
int rc_recover_begin(rc_device *,unsigned);
int rc_recover_poll(rc_device *,unsigned);
int rc_config_begin(rc_device *,const rc_config *,const rc_buffer *);
int rc_config_poll(rc_device *);
#endif

