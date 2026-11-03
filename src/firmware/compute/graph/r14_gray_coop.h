#ifndef R14_GRAY_COOP_H
#define R14_GRAY_COOP_H
#include "r14_filter_programs.h"
#include "field_stream_v346.h"
#define RG_ARENA_BYTES 1552384u
#define RG_STREAM_OFFSET_V349 1228800u
#define RG_ARENA0 0x19950000u
#define RG_ARENA1 0x19b90000u
enum {RG_GUIDE=1,RG_FIELDS_GATHER=2,RG_FIELDS_START=3,RG_FIELDS_WAIT=4,
      RG_BOX_START=5,RG_BOX_WAIT=6,RG_COEFF_START=7,RG_COEFF_WAIT=8,
      RG_NORMALIZE=9,RG_TAIL_GATHER=10,RG_TAIL_START=11,RG_TAIL_WAIT=12,
      RG_DONE=13,RG_FAILED=14,RG_CANCELLED=15};
typedef struct {
 uint32_t cookie,owned,state,error,frame,cursor,box_index,tail_count,tag,stream_active;
 const uint8_t *rgb;const float *disparity;float *output;uint8_t *arena;
 uint32_t address;
 rf_job field;
} rg_job;
typedef struct {
 uint32_t cookie,cycle_limit;rc_device *device;rf_coop fields;rf_coop *field_service;
 rg_job *registered[2],*box_active;
 uint64_t box_jobs,box_values,frames,cpu_guide_values,cpu_gather_values,cpu_pack_values;
 uint32_t native_epoch;field_cache_v282 stream_cache;void *stream_user;int (*stream_done)(void *,rg_job *);int (*stream_fields_done)(void *,rg_job *);
} rg_coop;
/* Exact accepted gray4 chain. RGB conversion and field gathering use CPU;
 * reductions/products, covariance solve, interpolation and all six two-axis
 * BOX planes run on the CP43 common engine with a native gray tail. Two frames have private
 * DDR arenas. Buffers and job storage remain leased through failed recovery.
 * Completion means complete depth data, not HDMI adoption or real boardFPS. */
int rg_init(rg_coop *,rc_device *,uint32_t);
int rg_consumer_step_v346(rg_coop *,rg_job *);
int rg_begin(rg_coop *,rg_job *,unsigned,const uint8_t *,const float *,float *,void *,uint32_t);
int rg_step(rg_coop *,rg_job *);
int rg_cancel(rg_coop *,rg_job *);
int rg_recover(rg_coop *);
int rg_release(rg_coop *,rg_job *);
#endif
