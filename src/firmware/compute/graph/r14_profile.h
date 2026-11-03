#ifndef R14_PROFILE_H
#define R14_PROFILE_H
#include <stdint.h>
#ifndef R14_PROFILE_ENABLED
#define R14_PROFILE_ENABLED 0
#endif
/* Diagnostic-only timings overlap when a parent contains a child. Never sum
 * every bucket as frame latency. Hardware cycle counters have their own clock. */
enum r14p_stage {
 R14P_MODEL, R14P_QUANTIZE, R14P_DEQUANTIZE, R14P_FIXED_NODE,
 R14P_COLOR_CONTEXT, R14P_FINAL_PREDICTION, R14P_MEDIAN, R14P_RGB_NATIVE, R14P_GRAY_FILTER,
 R14P_NN_BACKEND, R14P_NN_COPY_IN, R14P_NN_COPY_OUT, R14P_NN_WEIGHT_PACK,
 R14P_NN_DESCRIPTOR_BUILD, R14P_NN_JOB, R14P_NN_CACHE_PREP,
 R14P_NN_DESCRIPTOR_WRITE, R14P_NN_KICK_WAIT, R14P_NN_RECEIPT, R14P_NN_INVALIDATE,
 R14P_SGM_CACHE_PREP, R14P_SGM_DESCRIPTOR_WRITE, R14P_SGM_OWNED_INTERVAL, R14P_SGM_INVALIDATE,
 R14P_CAMERA_WAIT, R14P_YUYV_RGB, R14P_RECTIFY, R14P_PACK_DISPARITY,
 R14P_VALID_MASK, R14P_DEPTH_MM, R14P_RENDER, R14P_ANNOTATION,
 R14P_DISPLAY, R14P_VDMA_HALT, R14P_VDMA_FLUSH, R14P_VDMA_START_WAIT,
 R14P_STAGE_COUNT
};
enum r14p_counter {
 R14P_NN_LAYER_ATTEMPTS, R14P_NN_LAYER_COMPLETED, R14P_NN_LAYER_FAILED,
 R14P_NN_JOB_ATTEMPTS, R14P_NN_JOB_COMPLETED, R14P_NN_JOB_FAILED,
 R14P_NN_CYCLES, R14P_NN_READ_BEATS, R14P_NN_WRITE_VALUES,
 R14P_NN_COPY_IN_BYTES, R14P_NN_COPY_OUT_BYTES,
 R14P_NN_FLUSH_BYTES, R14P_NN_INVALIDATE_BYTES, R14P_NN_DESCRIPTOR_WRITES, R14P_NN_POLL_READS,
 R14P_NN_CANCEL_ATTEMPTS, R14P_NN_RECOVER_ATTEMPTS, R14P_NN_RECOVER_FAILED,
 R14P_SGM_COMPLETED, R14P_SGM_CYCLES, R14P_SGM_READ_BEATS, R14P_SGM_WRITE_BEATS,
 R14P_SGM_FLUSH_BYTES, R14P_SGM_INVALIDATE_BYTES, R14P_SGM_CANCEL_ATTEMPTS,
 R14P_SGM_RECOVER_ATTEMPTS, R14P_SGM_RECOVER_FAILED,
 R14P_DISPLAY_ATTEMPTS, R14P_DISPLAY_COMPLETED, R14P_DISPLAY_FAILED, R14P_DISPLAY_FLUSH_BYTES,
 R14P_LIVE_COMPLETED, R14P_LIVE_FAILURES, R14P_COUNTER_COUNT
};
typedef struct { uint64_t starts,completed,failed,incomplete,ticks,max_ticks; } r14p_sample;
typedef struct {
 r14p_sample stage[R14P_STAGE_COUNT];
 uint64_t counter[R14P_COUNTER_COUNT];
 uint64_t tick_hz,start_tick,stop_tick,clock_errors;
} r14p_snapshot;
#if R14_PROFILE_ENABLED
void r14_profile_start(uint64_t (*tick)(void),uint64_t tick_hz);
void r14_profile_begin(enum r14p_stage stage);
void r14_profile_begin_once(enum r14p_stage stage);
void r14_profile_end(enum r14p_stage stage,int status);
void r14_profile_abort_range(enum r14p_stage first,enum r14p_stage last,int status);
void r14_profile_add(enum r14p_counter counter,uint64_t amount);
void r14_profile_stop(void);
const r14p_snapshot *r14_profile_get(void);
#define R14P_BEGIN(s) r14_profile_begin(s)
#define R14P_BEGIN_ONCE(s) r14_profile_begin_once(s)
#define R14P_END(s,e) r14_profile_end(s,e)
#define R14P_ABORT(a,b,e) r14_profile_abort_range(a,b,e)
#define R14P_ADD(c,n) r14_profile_add(c,(uint64_t)(n))
#else
#define R14P_BEGIN(s) ((void)0)
#define R14P_BEGIN_ONCE(s) ((void)0)
#define R14P_END(s,e) ((void)0)
#define R14P_ABORT(a,b,e) ((void)0)
#define R14P_ADD(c,n) ((void)0)
#endif
#endif
