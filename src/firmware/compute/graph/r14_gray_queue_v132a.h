#ifndef R14_GRAY_QUEUE_V132_H
#define R14_GRAY_QUEUE_V132_H
#include "r14_graph_coop.h"
#include "r14_gray_coop.h"
#define RGQ_RGB_BYTES 230400u
#define RGQ_MEDIAN_BYTES 307200u
#define RGQ_OUTPUT_BYTES 327680u
enum {RGQ_EMPTY=0,RGQ_FRONT_HOLD,RGQ_READY,RGQ_RUNNING,RGQ_DONE,RGQ_FAILED,RGQ_CANCELLED};
typedef struct {
 uint32_t state,sequence,error;void *front;
 uint8_t *rgb;float *median,*output;void *arena;uint32_t arena_bytes;
 rg_job job;
} rgq_slot;
typedef struct {
 uint32_t cookie;rg_coop *gray;rgq_slot slot[2];
 uint64_t snapshot_copy_bytes,handoffs,completed,cancelled,recovered;
} rgq_queue;
/* Caller owns the two immutable snapshots and filter arenas until release.
 * The frontend is released before the queued RG job can begin. The output
 * belongs to this queue until actual RG completion, drain, and release. */
int rgq_init(rgq_queue *,rg_coop *,uint8_t *const[2],float *const[2],void *const[2],uint32_t);
int rgq_enqueue(rgq_queue *,unsigned,unsigned,void *,float *);
int rgq_submit_snapshots(rgq_queue *,unsigned,unsigned,const uint8_t *,const float *,float *);
int rgq_step(rgq_queue *,unsigned);
int rgq_owned(const rgq_queue *,unsigned);
int rgq_release(rgq_queue *,unsigned);
int rgq_cancel(rgq_queue *,unsigned);
int rgq_recover(rgq_queue *,unsigned);
#endif
