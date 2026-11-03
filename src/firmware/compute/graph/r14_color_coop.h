#ifndef R14_COLOR_COOP_H
#define R14_COLOR_COOP_H
#include "r14_filter_programs.h"
#include "field_stream_v346.h"
/* These frame-private scratch ranges can be reused by gray only after
 * explicit release. The complete graph must own the surrounding arena. */
#define RCL_ARENA_BYTES 1327616u
#define RCL_STREAM_OFFSET_V349 823808u
#define RCL_ARENA0 0x19950000u
#define RCL_ARENA1 0x19b90000u
enum {RCL_CENTER=1,RCL_GUIDE_FIELDS,RCL_VERTICAL_START,RCL_VERTICAL_WAIT,
      RCL_PREFIX_START,RCL_PREFIX_WAIT,RCL_COST_FIELDS,RCL_BOX_START,
      RCL_BOX_WAIT,RCL_COV_GATHER,RCL_COV_START,RCL_COV_WAIT,
      RCL_RHS_GATHER,RCL_RHS_START,RCL_RHS_WAIT,
      RCL_DONE,RCL_FAILED,RCL_CANCELLED};
typedef struct {
 uint32_t cookie,owned,state,error,frame,cursor,plane,disparity,count,tag,stream_active;
 const float *logits,*guide;float *output;uint8_t *arena;uint32_t address;
 double inv[82];rf_job field;
} rcl_job;
typedef struct {
 uint32_t cookie,cycle_limit;rc_device *device;rf_coop fields;
 rcl_job *registered[2],*box_active;
 uint64_t frames,box_jobs,box_values,covariance_values,rhs_values;
 uint64_t cpu_center_values,cpu_guide_products,cpu_cost_products;
 uint64_t affine_weights,affine_cache_hits,cpu_affine_ops;
 field_cache_v282 stream_cache;void *stream_user;
 void (*stream_row)(void *,const double *,unsigned);int (*stream_done)(void *,rcl_job *);
} rcl_coop;
/* Exact original y>=4/x>=d masks, clipped windows, subtraction/addition
 * order, prefix arithmetic and covariance grouping. The existing CP43 BOX
 * and FIELD process all reductions and covariance and an eight-instruction cached affine RHS.
 * CPU computes ten FP64 operations per exact pixel/mask cache miss; those
 * operations are tracked but their ARM execution time is not simulated. CPU retains
 * widening/centering/products and address gathers; no new RTL or DSP pool.
 * Test-model clocks do not represent ARM execution or board throughput. */
int rcl_init(rcl_coop *,rc_device *,uint32_t);
int rcl_consumer_step_v346(rcl_coop *,rcl_job *);
int rcl_begin(rcl_coop *,rcl_job *,unsigned,const float *,const float *,float *,void *,uint32_t);
int rcl_step(rcl_coop *,rcl_job *);
int rcl_cancel(rcl_coop *,rcl_job *);
int rcl_recover(rcl_coop *);
int rcl_release(rcl_coop *,rcl_job *);
#endif
