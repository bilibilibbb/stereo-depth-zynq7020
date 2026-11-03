#ifndef R14_NN_COOP_H
#define R14_NN_COOP_H
#include "r14_typed_nn.h"
#include "r14_shared_compute.h"
#include "r14_dual_layout.h"
typedef struct {
 rc_device *device;int16_t *packed_weights;
 uint32_t weight_address,weight_capacity,cycle_limit,weight_used;
 const rn_parameter *cached_parameter[64];uint32_t parameter_offset[64];
 uint64_t layer_calls,dma_jobs;
} r14_shared_parameters;

enum {R14_LAYER_IDLE=0,R14_LAYER_READY=1,R14_LAYER_WAIT=2,R14_LAYER_DONE=3,R14_LAYER_FAILED=4,R14_LAYER_CANCELLED=5};
typedef struct {
 uint32_t state,frame,channel,tag,weight_offset,error;
 int id,ih,iw,oh,ow,wide,fraction,relu;
 const rn_parameter *parameter;
 const void *source;int16_t *destination;
 uint32_t source_address,destination_address,source_bytes,destination_bytes;
} r14_layer_job;
typedef struct {
 r14_shared_parameters *neural;r14_layer_job *active,*registered[2];uint32_t cookie;
 uint8_t *memory[2];uint32_t address[2],bytes;
 uint64_t calls,jobs,source_bytes,destination_bytes;int preferred_frame;
} r14_nn_coop;
int r14_nn_coop_init(r14_nn_coop *,r14_shared_parameters *,void *,void *,uint32_t);
int r14_nn_coop_begin(r14_nn_coop *,r14_layer_job *,uint32_t,int,const rn_parameter *,const void *,int,int,int16_t *,int,int,int,int,int);
/* One step either submits one channel/page, checks its single status, or
 * returns pending while the one physical device is owned by the other frame. */
int r14_nn_coop_step(r14_nn_coop *,r14_layer_job *);
int r14_nn_coop_cancel(r14_nn_coop *,r14_layer_job *);
int r14_nn_coop_recover(r14_nn_coop *);
/* Only READY submission is prioritized; active poll/cancel/drain are unchanged. */
int r14_nn_coop_prefer_frame(r14_nn_coop *,int);
#endif
