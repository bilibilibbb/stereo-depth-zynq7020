#ifndef R14_FIELD_COOP_H
#define R14_FIELD_COOP_H
#include "r14_shared_compute.h"
enum {RF_READY=1,RF_CONFIGURE=2,RF_CONFIG_WAIT=3,RF_SUBMIT=4,RF_WAIT=5,
      RF_DONE=6,RF_FAILED=7,RF_CANCELLED=8};
typedef struct {
 uint32_t words,inputs,outputs;
 const uint32_t *program;
 const rc_config *input,*output;
} rf_program;
typedef struct {
 uint32_t cookie,owned,state,error,index,count,tag;
 const rf_program *program;
 rc_buffer input[40],output[8],buffers[RC_MAX_BUFFERS];
 uint32_t buffer_count,native_mode,native_address[5];
} rf_job;
struct field_cache_v282;
typedef struct {
 uint32_t cookie,cycle_limit;
 rc_device *device;
 rf_job *registered[2],*active;
 uint64_t jobs,values;
 void *cpu_queue;const struct field_cache_v282 *cpu_cache;uint32_t cpu_ticket;
} rf_coop;
/* One cooperative caller. Immutable program and field buffers remain leased
 * until explicit release, including configuration, cancellation and recovery.
 * This PS candidate accepts the five frozen typed programs; native modes
 * include their original Q40 transport semantics. No PL FIELD command is sent.
 * Scalars have no rc_buffer; non-scalars use program declaration order. */
int rf_init(rf_coop *,rc_device *,uint32_t);
int rf_begin(rf_coop *,rf_job *,unsigned,const rf_program *,uint32_t,
             const rc_buffer *,const rc_buffer *);
void field_coop_counts_v280(uint32_t[7],uint32_t[7]);
int rf_bind_cpu_queue_v284(rf_coop *,void *,const struct field_cache_v282 *);
int rf_step(rf_coop *,rf_job *);
int rf_native_begin(rf_coop *,rf_job *,unsigned,uint32_t,const rc_buffer *);
int rf_color_native_begin(rf_coop *,rf_job *,unsigned,unsigned,const rc_buffer *);
int rf_cancel(rf_coop *,rf_job *);
int rf_recover(rf_coop *);
int rf_release(rf_coop *,rf_job *);
#endif
