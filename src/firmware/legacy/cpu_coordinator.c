/* One CPU0 coordinator routes every CPU1 receipt to its retained frame.
 * This translation unit includes the real graph and exact range kernels.
 * It owns no tensor arenas, no PL registers, and no CPU1-side callbacks.
 */
#include "cpu_jobs.c"
#include "r14_cpu_graph_v182.h"

#define C182_MAGIC 0x434f0182u
typedef struct c182_binding c182_binding;
typedef struct {
 uint32_t magic,next_job,submitted,completed,error;
 c179_queue *queue;c182_binding *binding[R14_CPU_LEASES];
} c182_coordinator;
struct c182_binding {
 uint32_t magic,slot,epoch,retry,job,task,active,ready,cancelled,status;
 uint64_t frame,input_sequence;c182_coordinator *owner;r14_cpu_completion_v179 completion;
};
static int c182_coordinator_valid(const c182_coordinator *c){return c && c->magic==C182_MAGIC && c->queue && c->queue->magic==C179_MAGIC;}
static int c182_binding_valid(const c182_binding *b){return b && b->magic==C182_MAGIC && c182_coordinator_valid(b->owner) && b->slot<R14_CPU_LEASES && b->owner->binding[b->slot]==b;}
size_t r14_cpu_coordinator_bytes_v182(void){return sizeof(c182_coordinator);}
size_t r14_cpu_binding_bytes_v182(void){return sizeof(c182_binding);}
int r14_cpu_coordinator_init_v182(void *s,size_t n,void *queue){
 c182_coordinator *c=(c182_coordinator*)s;c179_queue *q=(c179_queue*)queue;unsigned i;
 if(!c179_storage(s,n,sizeof(*c)) || !q || q->magic!=C179_MAGIC)return RN_ARGUMENT;
 if(c->magic==C182_MAGIC){for(i=0;i<R14_CPU_LEASES;i++)if(c->binding[i])return RN_BUSY_ERROR;}
 memset(c,0,sizeof(*c));c->magic=C182_MAGIC;c->queue=q;c->next_job=q->next_job;return RN_OK;
}
int r14_cpu_binding_open_v182(void *s,size_t n,void *coordinator,unsigned slot,uint64_t frame,uint64_t input,unsigned retry){
 c182_binding *b=(c182_binding*)s;c182_coordinator *c=(c182_coordinator*)coordinator;unsigned epoch;int rc;
 if(!c179_storage(s,n,sizeof(*b)) || !c182_coordinator_valid(c) || slot>=R14_CPU_LEASES || !retry)return RN_ARGUMENT;
 if(c->error)return (int)c->error;
 if(c->binding[slot] || c182_binding_valid(b))return RN_BUSY_ERROR;
 rc=r14_cpu_lease_open_v179(c->queue,slot,frame,input,retry,100u+2u*slot,101u+2u*slot,&epoch);if(rc)return rc;
 memset(b,0,sizeof(*b));b->magic=C182_MAGIC;b->slot=slot;b->frame=frame;b->input_sequence=input;b->retry=retry;b->epoch=epoch;b->owner=c;c->binding[slot]=b;return RN_OK;
}
int r14_cpu_coordinator_service_v182(void *s){
 c182_coordinator *c=(c182_coordinator*)s;r14_cpu_completion_v179 r;c182_binding *b;unsigned i;int rc;
 if(!c182_coordinator_valid(c))return RN_ARGUMENT;
 if(c->error)return (int)c->error;
 for(i=0;i<R14_CPU_LEASES;i++){
  rc=r14_cpu_completion_take_v179(c->queue,&r);if(rc==RN_PENDING)return RN_OK;
  if(rc){c->error=(uint32_t)rc;return rc;}
  b=c->binding[r.lease_slot];
  if(!c182_binding_valid(b) || !b->active || b->ready || b->job!=r.job_id || b->task!=r.task || b->epoch!=r.region_epoch || b->retry!=r.retry_epoch || b->frame!=r.frame_id || b->input_sequence!=r.input_pair_sequence || r.core_id!=1){c->error=RN_HARDWARE;return RN_HARDWARE;}
  b->completion=r;b->status=r.status;b->ready=1;c->completed++;
 }
 return RN_OK;
}
static int c182_begin(void *s,const r14_cpu_math_v182 *m){
 c182_binding *b=(c182_binding*)s;c182_coordinator *c;r14_cpu_request_v179 r;unsigned i;int rc;
 if(!c182_binding_valid(b) || !m || (m->task!=CPU_DECODE_FINISH && m->task!=CPU_ATTRIBUTES_POOL))return RN_ARGUMENT;
 c=b->owner;if(c->error)return (int)c->error;if(b->active || b->cancelled)return RN_BUSY_ERROR;
 if(c->next_job==UINT32_MAX)return RN_HARDWARE;
 memset(&r,0,sizeof(r));r.frame_id=b->frame;r.input_pair_sequence=b->input_sequence;r.job_id=c->next_job+1;r.task=m->task;
 r.lease_slot=b->slot;r.region_epoch=b->epoch;r.retry_epoch=b->retry;r.input_region=100u+2u*b->slot;r.output_region=101u+2u*b->slot;
 r.dependencies=m->task==CPU_DECODE_FINISH?R14_CPU_COLOR_DONE:R14_CPU_COARSE_DONE;r.last=r14_cpu_task_extent_v179(m->task);r.state=R14_CPU_READY;
 for(i=0;i<4;i++){r.address[i]=(uint64_t)(uintptr_t)m->source[i];}
 r.address[4]=(uint64_t)(uintptr_t)m->destination;
 rc=r14_cpu_submit_v179(c->queue,&r);if(rc)return rc;
 c->next_job=r.job_id;c->submitted++;b->job=r.job_id;b->task=r.task;b->active=1;b->ready=0;b->status=RN_PENDING;memset(&b->completion,0,sizeof(b->completion));return RN_OK;
}
static int c182_poll(void *s){
 c182_binding *b=(c182_binding*)s;int rc;
 if(!c182_binding_valid(b) || !b->active)return RN_ARGUMENT;
 rc=r14_cpu_coordinator_service_v182(b->owner);if(rc)return rc;if(!b->ready)return RN_PENDING;
 rc=(int)b->status;b->ready=0;b->active=0;return rc;
}
static int c182_cancel(void *s){
 c182_binding *b=(c182_binding*)s;int rc;
 if(!c182_binding_valid(b))return RN_ARGUMENT;
 b->cancelled=1;if(!b->active || b->ready)return RN_CANCELLED_ERROR;
 rc=r14_cpu_cancel_v179(b->owner->queue,b->slot,b->epoch);return rc;
}
static int c182_quiescent(void *s){
 c182_binding *b=(c182_binding*)s;
 if(!c182_binding_valid(b) || r14_cpu_coordinator_service_v182(b->owner))return 0;
 /* A cancelled graph has no later poll: consume its terminal receipt before
  * permitting release. An ordinary result stays held for its graph poll. */
 if(b->cancelled && b->ready){b->ready=0;b->active=0;}
 return !b->active && !b->owner->queue->lease[b->slot].pending;
}
int r14_cpu_binding_close_v182(void *s){
 c182_binding *b=(c182_binding*)s;c182_coordinator *c;int rc;
 if(!c182_binding_valid(b))return RN_ARGUMENT;
 if(!c182_quiescent(b))return RN_BUSY_ERROR;
 c=b->owner;
 rc=r14_cpu_lease_close_v179(c->queue,b->slot);if(rc)return rc;c->binding[b->slot]=0;b->magic=0;return RN_OK;
}
r14_cpu_graph_ops_v182 r14_cpu_graph_callbacks_v182(void){r14_cpu_graph_ops_v182 c={c182_begin,c182_poll,c182_cancel,c182_quiescent};return c;}
unsigned r14_cpu_coordinator_submitted_v182(const void *s){const c182_coordinator *c=(const c182_coordinator*)s;return c182_coordinator_valid(c)?c->submitted:0;}
unsigned r14_cpu_coordinator_completed_v182(const void *s){const c182_coordinator *c=(const c182_coordinator*)s;return c182_coordinator_valid(c)?c->completed:0;}
unsigned r14_cpu_binding_pending_v182(const void *s){const c182_binding *b=(const c182_binding*)s;return c182_binding_valid(b)?b->active:0;}
int r14_cpu_binding_completion_v182(const void *s,void *out,size_t bytes){
 const c182_binding *b=(const c182_binding*)s;if(!c182_binding_valid(b) || !out || bytes<sizeof(b->completion))return RN_ARGUMENT;
 if(!b->completion.job_id)return RN_PENDING;
 memcpy(out,&b->completion,sizeof(b->completion));return RN_OK;
}
int r14_cpu_worker_has_work_v182(const void *s){
 const c179_worker *w=(const c179_worker*)s;
 if(!w || w->magic!=C179_MAGIC)return 0;
 /* Called by the worker itself; producer index is an acquire load. */
 return w->active || w->completion_pending || w->queue->request_read!=c179_load(&w->queue->request_write);
}
#if defined(R14_POST_VALIDATION_EXPORTS)
#include "async_graph_probe_v182.inc"
#endif
