#include "compute_namespace.h"
#include "stream_dispatch.h"
/* Owned A9 cache/role adaptation; frozen kernels and request ABI retained. */
/* Exact v175 functions, with v179 iteration boundaries, in one translation
 * unit. CPU1 is restricted to decode_finish and attributes_pool. The owner
 * uses the same range API for CPU0 and can service PL between every chunk.
 */
#include "cpu_task_probe.c"
#include "r14_cpu_jobs_v179.h"
#include <stddef.h>

#define C179_MAGIC 0x43503179u
_Static_assert(sizeof(r14_cpu_request_v179)==128,"Request ABI");
_Static_assert(sizeof(r14_cpu_completion_v179)==64,"Completion ABI");
_Static_assert(__atomic_always_lock_free(4,0),"32-bit queue atomics must be lock free");
typedef struct {
 uint32_t magic,cursor,chunks,done;
 r14_cpu_request_v179 request;g_context context;
 r14_linear_axis ax[W],ay[H];
} c179_kernel;
typedef struct {
 r14_cpu_request_v179 request;
 uint32_t epoch,retry,cancel,owned,pending,input_region,output_region,reserved;
 uint64_t frame_id,input_sequence;
 uint8_t pad[16];
} c179_lease;
typedef struct {
 uint32_t magic,request_write;uint8_t pad0[56];
 uint32_t request_read;uint8_t pad1[60];
 uint32_t completion_write;uint8_t pad2[60];
 uint32_t completion_read;uint8_t pad3[60];
 r14_cpu_request_v179 requests[R14_CPU_QUEUE_CAPACITY];
 r14_cpu_completion_v179 completions[R14_CPU_QUEUE_CAPACITY];
 c179_lease lease[R14_CPU_LEASES];
 uint32_t next_job,request_peak,completion_peak,reserved;
} c179_queue;
typedef struct {
 uint32_t magic,active,completion_pending,core_id;
 c179_queue *queue;uint64_t (*tick)(void);
 c179_kernel kernel __attribute__((aligned(64)));r14_cpu_completion_v179 result;
} c179_worker;
_Static_assert(offsetof(c179_queue,request_read)==64,"Producer/consumer false sharing");
_Static_assert(offsetof(c179_queue,completion_write)==128,"Completion producer line");
_Static_assert(offsetof(c179_queue,completion_read)==192,"Completion consumer line");
static uint32_t c179_load(const uint32_t *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void c179_store(uint32_t *p,uint32_t v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static int c179_storage(const void *p,size_t n,size_t need){return p && !((uintptr_t)p&63u) && n>=need;}
size_t r14_cpu_kernel_bytes_v179(void){return sizeof(c179_kernel);}
size_t r14_cpu_queue_bytes_v179(void){return sizeof(c179_queue);}
size_t r14_cpu_worker_bytes_v179(void){return sizeof(c179_worker);}
unsigned r14_cpu_task_extent_v179(unsigned t){
 static const unsigned n[CPU_TASK_COUNT]={CN,CN+64,CN,128u*160u,240,240,240,20u*CN*4u,2u*CN*4u,PN*7u};
 return t<CPU_TASK_COUNT?n[t]:0;
}
static int c179_span(uint64_t address,size_t bytes,unsigned align){
 return address && address<=UINTPTR_MAX && !(address&(align-1u)) && bytes<=UINTPTR_MAX-(uintptr_t)address;
}
/* Exact footprints; all outputs remain private to the current lease until a
 * successful completion is consumed. Arrays may not alias a read footprint.
 */
static void c179_footprints(unsigned t,size_t n[8]){
 memset(n,0,8u*sizeof(*n));
 switch(t){
 case CPU_PRIORS:n[0]=4u*N*4u;n[4]=4u*CN*4u;break;
 case CPU_DECODE_PREPARE:n[0]=3u*N*4u;n[1]=34u*CN*4u;n[4]=17u*CN*4u;n[5]=3u*CN*4u;break;
 case CPU_DECODE_FINISH:n[0]=17u*CN*4u;n[4]=2u*CN*4u;break;
 case CPU_ATTRIBUTES_POOL:n[0]=2u*CN*4u;n[1]=4u*N*4u;n[2]=N*4u;n[3]=N*4u;n[4]=8u*128u*160u*4u;break;
 case CPU_FINAL_PREDICTION:n[0]=r14_graph_tensor_bytes();n[1]=4u*N*4u;n[4]=PN*4u;break;
 case CPU_MEDIAN:n[0]=PN*4u;n[4]=PN*4u;break;
 case CPU_RGB:n[0]=3u*N*4u;n[4]=3u*PN;break;
 case CPU_EXPORT_COPY:n[0]=17u*CN*4u;n[1]=3u*CN*4u;n[4]=20u*CN*4u;break;
 case CPU_COARSE_COPY:n[0]=2u*CN*4u;n[4]=2u*CN*4u;break;
 case CPU_GRAY_SNAPSHOT_COPY:n[0]=3u*PN;n[1]=PN*4u;n[4]=3u*PN;n[5]=PN*4u;break;
 default:break;
 }
}
/* Mailbox is uncached; payloads keep their actual cacheable DDR leases.
 * mode0: CPU0 publishes clean inputs and clean/invalidated output;
 * mode1: CPU1 acquires all spans; mode2/3: successful/failed worker retires;
 * mode4: CPU0 consumes an already published completion. */
static int c179_cache_payload(const r14_cpu_request_v179 *r,unsigned mode){
 const field_cache_v282 *c=ss_lg_stream_cache();size_t bytes[8];unsigned i;
 if(!c || (ss_lg_core_id()!=((mode==0 || mode==4)?0u:1u)))return RN_ARGUMENT;
 c179_footprints(r->task,bytes);
 for(i=0;i<8;i++)if(bytes[i] && (mode<2 || i>=4)){
  void *p=(void *)(uintptr_t)r->address[i];
  if(mode==0 || mode==2)c->clean(c->user,p,(uint32_t)bytes[i]);
  else c->invalidate(c->user,p,(uint32_t)bytes[i]);
 }
 c->barrier(c->user);return RN_OK;
}
int r14_cpu_request_validate_v179(const r14_cpu_request_v179 *r){
 size_t n[8];unsigned i,j;
 if(!r || r->task>=CPU_TASK_COUNT || r->first>=r->last || r->last>r14_cpu_task_extent_v179(r->task) || r->state!=R14_CPU_READY || r->reserved)return RN_ARGUMENT;
 c179_footprints(r->task,n);
 for(i=0;i<8;i++)if(n[i] && !c179_span(r->address[i],n[i],((r->task==CPU_RGB && i==4) || (r->task==CPU_GRAY_SNAPSHOT_COPY && (i==0 || i==4)))?1:4))return RN_ARGUMENT;
 for(i=4;i<8;i++)if(n[i])for(j=0;j<8;j++)if(n[j] && i!=j && span_overlap((void*)(uintptr_t)r->address[i],n[i],(void*)(uintptr_t)r->address[j],n[j]))return RN_ARGUMENT;
 if(r->task==CPU_FINAL_PREDICTION && (r->address[0]&63u))return RN_ARGUMENT;
 return RN_OK;
}
int r14_cpu_kernel_begin_v179(void *s,size_t bytes,const r14_cpu_request_v179 *r){
 c179_kernel *k=(c179_kernel*)s;
 if(!c179_storage(s,bytes,sizeof(*k)) || r14_cpu_request_validate_v179(r))return RN_ARGUMENT;
 if(k->magic==C179_MAGIC && !k->done)return RN_BUSY_ERROR;
 memset(k,0,sizeof(*k));k->magic=C179_MAGIC;k->request=*r;k->cursor=r->first;
 if(r->task==CPU_ATTRIBUTES_POOL){linear_axis(80,W,k->ax);linear_axis(64,H,k->ay);}
 if(r->task==CPU_FINAL_PREDICTION){k->context.arena=(uint8_t*)(uintptr_t)r->address[0];k->context.native=(const float*)(uintptr_t)r->address[1];}
 return RN_OK;
}
static void c179_two_copy(const uint8_t *a,size_t split,const uint8_t *b,uint8_t *da,uint8_t *db,unsigned first,unsigned last){
 unsigned at=first;
 if(at<split){unsigned end=last<split?last:(unsigned)split;memcpy(da+at,a+at,end-at);at=end;}
 if(at<last)memcpy(db+at-split,b+at-split,last-at);
}
int r14_cpu_kernel_step_v179(void *s){
 c179_kernel *k=(c179_kernel*)s;const r14_cpu_request_v179 *r;unsigned a,b,chunk;
 const float *p0,*p1,*p2,*p3;float *d0,*d1;
 if(!k || k->magic!=C179_MAGIC)return RN_ARGUMENT;
 if(k->done)return RN_OK;
 r=&k->request;a=k->cursor;chunk=r->task==CPU_PRIORS?64:(r->task==CPU_DECODE_FINISH?32:(r->task==CPU_ATTRIBUTES_POOL?160:(r->task>=CPU_EXPORT_COPY?4096:1)));
 if(r->task==CPU_DECODE_PREPARE)chunk=a<CN?64:1;
 b=a+chunk;if(b>r->last)b=r->last;if(r->task==CPU_DECODE_PREPARE && a<CN && b>CN)b=CN;
 p0=(const float*)(uintptr_t)r->address[0];p1=(const float*)(uintptr_t)r->address[1];p2=(const float*)(uintptr_t)r->address[2];p3=(const float*)(uintptr_t)r->address[3];
 d0=(float*)(uintptr_t)r->address[4];d1=(float*)(uintptr_t)r->address[5];
 switch(r->task){
 case CPU_PRIORS:priors_range_v179(p0,d0,(int)a,(int)b);break;
 case CPU_DECODE_PREPARE:if(a<CN)g_decode_center_range_v179(d0,p1,(int)a,(int)b);else colour_guide_rows_v179(p0,d1,(int)(a-CN),(int)(b-CN));break;
 case CPU_DECODE_FINISH:g_decode_finish_range_v179(p0,d0,(int)a,(int)b);break;
 case CPU_ATTRIBUTES_POOL:g_attributes_range_v179(p0,p1,p2,p3,d0,k->ax,k->ay,(int)a,(int)b);break;
 case CPU_FINAL_PREDICTION:g_final_prediction_rows_v179(&k->context,d0,(int)a,(int)b);break;
 case CPU_MEDIAN:r14_median_rows_v179(p0,d0,median_scalar_pixel_v2,(int)a,(int)b);break;
 case CPU_RGB:rgb_native_rows_v179(p0,(uint8_t*)d0,(int)a,(int)b);break;
 case CPU_EXPORT_COPY:c179_two_copy((const uint8_t*)p0,17u*CN*4u,(const uint8_t*)p1,(uint8_t*)d0,(uint8_t*)d0+17u*CN*4u,a,b);break;
 case CPU_COARSE_COPY:memcpy((uint8_t*)d0+a,(const uint8_t*)p0+a,b-a);break;
 case CPU_GRAY_SNAPSHOT_COPY:c179_two_copy((const uint8_t*)p0,3u*PN,(const uint8_t*)p1,(uint8_t*)d0,(uint8_t*)d1,a,b);break;
 default:return RN_ARGUMENT;
 }
 k->chunks++;k->cursor=b;k->done=b==r->last;return k->done?RN_OK:RN_PENDING;
}
int r14_cpu_queue_init_v179(void *s,size_t bytes){
 c179_queue *q=(c179_queue*)s;if(!c179_storage(s,bytes,sizeof(*q)))return RN_ARGUMENT;
 if(q->magic==C179_MAGIC){unsigned i;for(i=0;i<R14_CPU_LEASES;i++)if(c179_load(&q->lease[i].owned))return RN_BUSY_ERROR;}
 memset(q,0,sizeof(*q));q->magic=C179_MAGIC;return RN_OK;
}
int r14_cpu_lease_open_v179(void *s,unsigned slot,uint64_t frame,uint64_t input,unsigned retry,unsigned in_region,unsigned out_region,unsigned *epoch){
 c179_queue *q=(c179_queue*)s;c179_lease *l;
 if(!q || q->magic!=C179_MAGIC || slot>=R14_CPU_LEASES || !retry || !in_region || !out_region || !epoch)return RN_ARGUMENT;
 l=q->lease+slot;if(c179_load(&l->owned) || l->pending)return RN_BUSY_ERROR;
 l->epoch++;if(!l->epoch)l->epoch=1;l->retry=retry;l->cancel=0;l->input_region=in_region;l->output_region=out_region;l->frame_id=frame;l->input_sequence=input;
 *epoch=l->epoch;c179_store(&l->owned,1);return RN_OK;
}
int r14_cpu_lease_close_v179(void *s,unsigned slot){
 c179_queue *q=(c179_queue*)s;c179_lease *l;
 if(!q || q->magic!=C179_MAGIC || slot>=R14_CPU_LEASES)return RN_ARGUMENT;
 l=q->lease+slot;if(!c179_load(&l->owned))return RN_ARGUMENT;
 if(l->pending)return RN_BUSY_ERROR;
 c179_store(&l->owned,0);return RN_OK;
}
static int c179_request_owned(const c179_lease *l,const r14_cpu_request_v179 *r){
 return c179_load(&l->owned) && l->epoch==r->region_epoch && l->retry==r->retry_epoch && l->frame_id==r->frame_id && l->input_sequence==r->input_pair_sequence && l->input_region==r->input_region && l->output_region==r->output_region;
}
int r14_cpu_submit_v179(void *s,const r14_cpu_request_v179 *r){
 c179_queue *q=(c179_queue*)s;c179_lease *l;uint32_t write,read;unsigned i,a,b;size_t n[8],m[8];
 if(!q || q->magic!=C179_MAGIC || !r || r->lease_slot>=R14_CPU_LEASES || r14_cpu_request_validate_v179(r) ||
    (r->task!=CPU_DECODE_FINISH && r->task!=CPU_ATTRIBUTES_POOL) || r->first || r->last!=r14_cpu_task_extent_v179(r->task) || !r->job_id)return RN_ARGUMENT;
 if(!(r->dependencies&(r->task==CPU_DECODE_FINISH?R14_CPU_COLOR_DONE:R14_CPU_COARSE_DONE)))return RN_ARGUMENT;
 l=q->lease+r->lease_slot;if(!c179_request_owned(l,r))return RN_ARGUMENT;
 if(l->pending || c179_load(&l->cancel))return RN_BUSY_ERROR;
 if(r->job_id<=q->next_job)return RN_ARGUMENT;
 write=q->request_write;read=c179_load(&q->request_read);if(write-read>=R14_CPU_QUEUE_CAPACITY)return RN_PENDING;
 c179_footprints(r->task,n);
 for(i=0;i<R14_CPU_LEASES;i++)if(q->lease[i].pending){
  const r14_cpu_request_v179 *other=&q->lease[i].request;c179_footprints(other->task,m);
  /* One writer, including read/write conflicts across retained frames. */
  for(a=0;a<8;a++)if(n[a])for(b=0;b<8;b++)if(m[b] && (a>=4 || b>=4) && span_overlap((void*)(uintptr_t)r->address[a],n[a],(void*)(uintptr_t)other->address[b],m[b]))return RN_BUSY_ERROR;
 }
 if(c179_cache_payload(r,0))return RN_ARGUMENT;
 l->request=*r;l->pending=r->job_id;q->requests[write%R14_CPU_QUEUE_CAPACITY]=*r;q->next_job=r->job_id;
 if(write-read+1>q->request_peak)q->request_peak=write-read+1;
 c179_store(&q->request_write,write+1);return RN_OK;
}
int r14_cpu_cancel_v179(void *s,unsigned slot,unsigned epoch){
 c179_queue *q=(c179_queue*)s;c179_lease *l;
 if(!q || q->magic!=C179_MAGIC || slot>=R14_CPU_LEASES)return RN_ARGUMENT;
 l=q->lease+slot;if(!c179_load(&l->owned) || l->epoch!=epoch || !l->pending)return RN_ARGUMENT;
 c179_store(&l->cancel,1);return RN_CANCELLED_ERROR;
}
int r14_cpu_completion_take_v179(void *s,r14_cpu_completion_v179 *r){
 c179_queue *q=(c179_queue*)s;c179_lease *l;uint32_t read,write;
 if(!q || q->magic!=C179_MAGIC || !r)return RN_ARGUMENT;
 read=q->completion_read;write=c179_load(&q->completion_write);if(read==write)return RN_PENDING;
 *r=q->completions[read%R14_CPU_QUEUE_CAPACITY];
 if(r->lease_slot>=R14_CPU_LEASES)return RN_HARDWARE;
 l=q->lease+r->lease_slot;
 if(!c179_load(&l->owned) || l->epoch!=r->region_epoch || l->retry!=r->retry_epoch || l->pending!=r->job_id || l->frame_id!=r->frame_id || l->input_sequence!=r->input_pair_sequence)return RN_HARDWARE;
 if(c179_load(&l->cancel))r->status=RN_CANCELLED_ERROR;
 if(c179_cache_payload(&l->request,4))return RN_ARGUMENT;
 l->pending=0;c179_store(&q->completion_read,read+1);return RN_OK;
}
int r14_cpu_worker_init_v179(void *s,size_t bytes,void *queue,unsigned core_id,uint64_t (*tick)(void)){
 c179_worker *w=(c179_worker*)s;c179_queue *q=(c179_queue*)queue;
 if(!c179_storage(s,bytes,sizeof(*w)) || !q || q->magic!=C179_MAGIC || !tick || core_id!=1)return RN_ARGUMENT;
 if(w->magic==C179_MAGIC && (w->active || w->completion_pending))return RN_BUSY_ERROR;
 memset(w,0,sizeof(*w));w->magic=C179_MAGIC;w->queue=q;w->core_id=core_id;w->tick=tick;return RN_OK;
}
int r14_cpu_worker_step_v179(void *s){
 c179_worker *w=(c179_worker*)s;c179_queue *q;c179_lease *l;uint32_t read,write;int rc;
 if(!w || w->magic!=C179_MAGIC || ss_lg_core_id()!=1 || !ss_lg_stream_cache())return RN_ARGUMENT;
 q=w->queue;
 if(w->completion_pending){
  write=q->completion_write;read=c179_load(&q->completion_read);if(write-read>=R14_CPU_QUEUE_CAPACITY)return RN_PENDING;
  q->completions[write%R14_CPU_QUEUE_CAPACITY]=w->result;
  if(write-read+1>q->completion_peak)q->completion_peak=write-read+1;
  c179_store(&q->completion_write,write+1);w->completion_pending=0;
  return RN_OK;
 }
 if(!w->active){
  read=q->request_read;write=c179_load(&q->request_write);if(read==write)return RN_PENDING;
  w->kernel.done=1;rc=r14_cpu_kernel_begin_v179(&w->kernel,sizeof(w->kernel),q->requests+read%R14_CPU_QUEUE_CAPACITY);if(rc)return rc;
  rc=c179_cache_payload(&w->kernel.request,1);if(rc)return rc;
  c179_store(&q->request_read,read+1);memset(&w->result,0,sizeof(w->result));
  w->result.frame_id=w->kernel.request.frame_id;w->result.input_pair_sequence=w->kernel.request.input_pair_sequence;
  w->result.job_id=w->kernel.request.job_id;w->result.task=w->kernel.request.task;w->result.lease_slot=w->kernel.request.lease_slot;
  w->result.region_epoch=w->kernel.request.region_epoch;w->result.retry_epoch=w->kernel.request.retry_epoch;w->result.core_id=w->core_id;w->result.begin_tick=w->tick();w->active=1;
 }
 l=q->lease+w->kernel.request.lease_slot;
 if(!c179_request_owned(l,&w->kernel.request) || l->pending!=w->kernel.request.job_id)rc=RN_HARDWARE;
 else if(c179_load(&l->cancel))rc=RN_CANCELLED_ERROR;
 else rc=r14_cpu_kernel_step_v179(&w->kernel);
 if(rc==RN_PENDING)return rc;
 w->result.status=(uint32_t)rc;w->result.chunks=w->kernel.chunks;w->result.end_tick=w->tick();
 if(w->result.end_tick<w->result.begin_tick)w->result.status=RN_HARDWARE;
 rc=c179_cache_payload(&w->kernel.request,w->result.status?3u:2u);if(rc)return rc;
 w->active=0;w->completion_pending=1;return RN_PENDING;
}
unsigned r14_cpu_request_peak_v179(const void *s){const c179_queue *q=(const c179_queue*)s;return q && q->magic==C179_MAGIC?q->request_peak:0;}
unsigned r14_cpu_completion_peak_v179(const void *s){const c179_queue *q=(const c179_queue*)s;return q && q->magic==C179_MAGIC?q->completion_peak:0;}
int r14_cpu_probe_seed_v179(const cpu_probe_input *p,cpu_probe_output *o){
 g_context g;
 if(!p || !o || !p->coarse || !p->final_i32 || !p->raw_logits || !o->logits || !o->tensor_memory || ((uintptr_t)o->tensor_memory&63u) || o->tensor_bytes<r14_graph_tensor_bytes())return RN_ARGUMENT;
 memset(&g,0,sizeof(g));g.arena=(uint8_t*)o->tensor_memory;memcpy(g_fp(&g,RN_COARSE),p->coarse,2u*CN*4u);memcpy(g_i32(&g,RN_FINAL),p->final_i32,2u*N*4u);memcpy(o->logits,p->raw_logits,17u*CN*4u);return RN_OK;
}
int r14_cpu_probe_request_v179(const cpu_probe_input *p,cpu_probe_output *o,unsigned task,r14_cpu_request_v179 *r){
 if(!p || !o || !r || task>=CPU_TASK_COUNT)return RN_ARGUMENT;
 memset(r,0,sizeof(*r));r->task=task;r->last=r14_cpu_task_extent_v179(task);r->state=R14_CPU_READY;
#define A179(i,pointer) r->address[i]=(uint64_t)(uintptr_t)(pointer)
 switch(task){
 case CPU_PRIORS:A179(0,p->native);A179(4,o->priors);break;
 case CPU_DECODE_PREPARE:A179(0,p->left);A179(1,p->curve);A179(4,o->logits);A179(5,o->guide);break;
 case CPU_DECODE_FINISH:A179(0,p->filtered);A179(4,o->decoded);break;
 case CPU_ATTRIBUTES_POOL:A179(0,p->coarse);A179(1,p->native);A179(2,p->gray);A179(3,p->right_gray);A179(4,o->attributes);break;
 case CPU_FINAL_PREDICTION:A179(0,o->tensor_memory);A179(1,p->native);A179(4,o->prediction);break;
 case CPU_MEDIAN:A179(0,o->prediction);A179(4,o->median);break;
 case CPU_RGB:A179(0,p->left);A179(4,o->rgb);break;
 case CPU_EXPORT_COPY:A179(0,o->logits);A179(1,o->guide);A179(4,o->copy);break;
 case CPU_COARSE_COPY:A179(0,p->coarse);A179(4,o->copy+20u*CN);break;
 case CPU_GRAY_SNAPSHOT_COPY:A179(0,o->rgb);A179(1,o->median);A179(4,o->snapshot_rgb);A179(5,o->snapshot_median);break;
 default:return RN_ARGUMENT;
 }
#undef A179
 return r14_cpu_request_validate_v179(r);
}
int r14_cpu_probe_chunked_v179(const cpu_probe_input *p,cpu_probe_output *o,uint64_t (*tick)(void),cpu_probe_sample *samples,void (*service)(void *),void *user){
 c179_kernel kernel __attribute__((aligned(64)));r14_cpu_request_v179 r;unsigned i;int rc;
 if(!tick || !samples || r14_cpu_probe_seed_v179(p,o))return RN_ARGUMENT;
 memset(&kernel,0,sizeof(kernel));
 for(i=0;i<CPU_TASK_COUNT;i++){
  if(r14_cpu_probe_request_v179(p,o,i,&r))return RN_ARGUMENT;
  probe_start(samples+i,i,tick);if(r14_cpu_kernel_begin_v179(&kernel,sizeof(kernel),&r))return RN_ARGUMENT;
  do{rc=r14_cpu_kernel_step_v179(&kernel);if(service)service(user);}while(rc==RN_PENDING);
  if(rc || probe_end(samples+i,tick))return RN_ARGUMENT;
 }
 return RN_OK;
}
