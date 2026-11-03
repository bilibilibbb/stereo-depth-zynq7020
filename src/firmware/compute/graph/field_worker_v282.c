#include "command_trace_v347.h"
/* The exact frozen arithmetic is compiled once in this translation unit.
 * Chunk boundaries change scheduling only; each pixel retains its original
 * operation order and rounding, including native Q40 transport. */
#include "field_cpu_v274.c"
#include "field_async_v282.h"
#include <stddef.h>
#define FIELD_MAILBOX_MAGIC 0x46413282u
enum {FQ_EMPTY,FQ_READY,FQ_RUNNING,FQ_COMPLETE};
typedef struct {
 uint32_t magic,state,ticket,cancel,status,next_ticket,completed,chunks;
 uint8_t pad[32];field_request_v282 request;
} field_mailbox_v282;
typedef struct {
 uint32_t magic,active,ticket,phase,cursor,row,output_index,chunks;
 field_mailbox_v282 *queue;field_cache_v282 cache;rf_job job;
 union {int64_t gray[2][3][320];int64_t color[9][320];} rows;
 uint32_t tags[9];int64_t columns[320];
} field_worker_v282;
_Static_assert(sizeof(field_desc_v282)==24,"Pointer-neutral descriptor ABI");
_Static_assert(sizeof(field_request_v282)==1280,"Pointer-neutral request ABI");
_Static_assert(offsetof(field_mailbox_v282,request)==64,"Mailbox separation");
_Static_assert(__atomic_always_lock_free(4,0),"A9 lock-free mailbox atomics");
static uint32_t fq_load(const uint32_t *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void fq_store(uint32_t *p,uint32_t v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static int fq_memory(const void *p,size_t n,size_t need){return p && !((uintptr_t)p&63u) && n>=need;}
static int fq_cache(const field_cache_v282 *c){return c && c->clean && c->invalidate && c->barrier;}
static unsigned fq_kind(const rf_job *j){
 if(j->native_mode==1)return 5;
 if(j->native_mode==2)return 6;
 if(j->native_mode)return 7;
 if(j->program==&r14_program_gray_fields)return 0;
 if(j->program==&r14_program_gray_coeff)return 1;
 if(j->program==&r14_program_gray_tail)return 2;
 if(j->program==&r14_program_color_covariance)return 3;
 if(j->program==&r14_program_color_rhs)return 4;
 return 7;
}
static const rf_program *fq_program(unsigned kind){
 static const rf_program *const p[5]={&r14_program_gray_fields,&r14_program_gray_coeff,&r14_program_gray_tail,&r14_program_color_covariance,&r14_program_color_rhs};
 return kind<5?p[kind]:0;
}
static field_desc_v282 fq_encode(const rc_buffer *b){
 field_desc_v282 v={(uint64_t)(uintptr_t)b->pointer,b->address,b->bytes,b->write,0};return v;
}
static int fq_decode(const field_desc_v282 *d,uint32_t bytes,unsigned write,rc_buffer *b){
 if(!d->pointer || d->pointer>UINTPTR_MAX || (d->pointer&7u) || d->reserved ||
    d->bytes!=bytes || d->write!=write || (d->address&7u) ||
    d->address<0x19000000u || (uint64_t)d->address+bytes>0x1a000000u ||
    d->pointer>UINTPTR_MAX-bytes-63u)return RC_ARGUMENT;
 *b=(rc_buffer){(void *)(uintptr_t)d->pointer,d->address,d->bytes,d->write};return RC_OK;
}
static int fq_overlap(uint64_t a,uint32_t na,uint64_t b,uint32_t nb){
 return (a&~UINT64_C(63))<((b+nb+63u)&~UINT64_C(63)) && (b&~UINT64_C(63))<((a+na+63u)&~UINT64_C(63));
}
static int fq_job(const field_request_v282 *r,rf_job *j){
 unsigned i;uint32_t sizes[4];
 if(r->kind>=7 || !r->count || r->count>81920 || !r->ticket || r->reserved[0] || r->reserved[1] || r->reserved[2])return RC_ARGUMENT;
 memset(j,0,sizeof(*j));j->count=r->count;j->native_mode=r->native_mode;j->native_address[4]=r->native_epoch;
 if(r->kind<5){
  if(r->native_mode || r->native_epoch)return RC_ARGUMENT;
  j->program=fq_program(r->kind);
  for(i=0;i<j->program->inputs;i++)if(!j->program->input[i].scalar){
   if(fq_decode(r->input+i,r->count*(j->program->input[i].wide?8u:4u),0,j->input+i))return RC_ARGUMENT;
   j->buffers[j->buffer_count++]=j->input[i];
  }
  for(i=0;i<j->program->outputs;i++){
   if(fq_decode(r->output+i,r->count*(j->program->output[i].wide?8u:4u),1,j->output+i))return RC_ARGUMENT;
   j->buffers[j->buffer_count++]=j->output[i];
  }
 }else{
  if(r->kind==5){
   if(r->native_mode!=1 || r->count!=76800 || !r->native_epoch || (r->native_epoch&0x80000000u))return RC_ARGUMENT;
   sizes[0]=614400;sizes[1]=307200;sizes[2]=115200;sizes[3]=307200;j->buffer_count=4;
  }else{
   unsigned d=r->native_epoch&31u;
   if(r->native_mode!=2 || d>16 || r->native_epoch!=(0x80000000u|d) || r->count!=60u*(80u-d))return RC_ARGUMENT;
   sizes[0]=163840;sizes[1]=r->count*32u;sizes[2]=r->count*4u;j->buffer_count=3;
  }
  for(i=0;i<j->buffer_count;i++)if(fq_decode(r->native+i,sizes[i],i==j->buffer_count-1,j->buffers+i))return RC_ARGUMENT;
 }
 for(i=0;i<j->buffer_count;i++){
  unsigned k;for(k=0;k<i;k++)if((j->buffers[i].write || j->buffers[k].write) &&
   (fq_overlap(j->buffers[i].address,j->buffers[i].bytes,j->buffers[k].address,j->buffers[k].bytes) ||
    fq_overlap((uintptr_t)j->buffers[i].pointer,j->buffers[i].bytes,(uintptr_t)j->buffers[k].pointer,j->buffers[k].bytes)))return RC_LEASE_CONFLICT;
 }
 return RC_OK;
}
size_t field_mailbox_bytes_v282(void){return sizeof(field_mailbox_v282);}
size_t field_worker_bytes_v282(void){return sizeof(field_worker_v282);}
int field_mailbox_init_v282(void *s,size_t bytes){
 field_mailbox_v282 *q=s;if(!fq_memory(s,bytes,sizeof(*q)))return RC_ARGUMENT;
 if(q->magic==FIELD_MAILBOX_MAGIC && fq_load(&q->state)!=FQ_EMPTY)return RC_AGAIN;
 memset(q,0,sizeof(*q));q->magic=FIELD_MAILBOX_MAGIC;return RC_OK;
}
int field_worker_init_v282(void *s,size_t bytes,void *queue,const field_cache_v282 *cache){
 field_worker_v282 *w=s;field_mailbox_v282 *q=queue;
 if(!fq_memory(s,bytes,sizeof(*w)) || !q || q->magic!=FIELD_MAILBOX_MAGIC || !fq_cache(cache))return RC_ARGUMENT;
 if(w->magic==FIELD_MAILBOX_MAGIC && w->active)return RC_AGAIN;
 memset(w,0,sizeof(*w));w->magic=FIELD_MAILBOX_MAGIC;w->queue=q;w->cache=*cache;return RC_OK;
}
int field_submit_v282(void *queue,const rf_job *j,const field_cache_v282 *cache,uint32_t *ticket){
 field_mailbox_v282 *q=queue;field_request_v282 r;rf_job decoded;unsigned i,kind;
 if(!q || q->magic!=FIELD_MAILBOX_MAGIC || !j || !ticket || !fq_cache(cache))return RC_ARGUMENT;
 if(fq_load(&q->state)!=FQ_EMPTY)return RC_AGAIN;
 kind=fq_kind(j);if(kind>=7)return RC_ARGUMENT;
 memset(&r,0,sizeof(r));r.kind=kind;r.count=j->count;r.native_mode=j->native_mode;r.native_epoch=j->native_address[4];
 r.ticket=q->next_ticket+1u;if(!r.ticket)r.ticket=1;
 if(kind<5){
  for(i=0;i<j->program->inputs;i++)if(!j->program->input[i].scalar)r.input[i]=fq_encode(j->input+i);
  for(i=0;i<j->program->outputs;i++)r.output[i]=fq_encode(j->output+i);
 }else for(i=0;i<j->buffer_count;i++)r.native[i]=fq_encode(j->buffers+i);
 if(fq_job(&r,&decoded))return RC_ARGUMENT;
 for(i=0;i<decoded.buffer_count;i++)cache->clean(cache->user,decoded.buffers[i].pointer,decoded.buffers[i].bytes);
 cache->barrier(cache->user);q->request=r;q->ticket=r.ticket;q->next_ticket=r.ticket;q->cancel=0;q->status=RC_PENDING;q->chunks=0;
 *ticket=r.ticket;fq_store(&q->state,FQ_READY);return RC_OK;
}
static int fq_color_row(field_worker_v282 *w){
 rf_job *j=&w->job;unsigned y=w->row,x,k,z,d=j->native_address[4]&31u;int64_t hsum[4];int rc;
 const double *weight=j->buffers[1].pointer;float *out=j->buffers[2].pointer;
 if(y>=4){
  for(k=0;k<4;k++){hsum[k]=w->columns[k]*5;for(x=1;x<=4;x++)hsum[k]+=w->columns[x*4u+k];}
  for(x=0;x<80;x++){
   if(x>=d){
    unsigned i=w->output_index,base=(i/128u)*128u,lanes=j->count-base,lane=i-base;double a[4],v,t;
    if(lanes>128u)lanes=128u;
    for(k=0;k<4;k++)a[k]=field_q40_to_f64_v317(hsum[k]);
    v=a[0]*weight[base*4u+lane];t=a[1]*weight[base*4u+lanes+lane];v=v+t;
    t=a[2]*weight[base*4u+2u*lanes+lane];v=v+t;t=a[3]*weight[base*4u+3u*lanes+lane];v=v+t;
    if(!isfinite(v))return RC_ARGUMENT;
    out[i]=(float)v;if(!isfinite(out[i]))return RC_ARGUMENT;
    w->output_index++;
   }
   if(x<79){unsigned old=x<4?0:x-4u,next=x+5u<80?x+5u:79u;for(k=0;k<4;k++)hsum[k]=hsum[k]+w->columns[next*4u+k]-w->columns[old*4u+k];}
  }
 }
 if(y<63){
  unsigned old=y<4?0:y-4u,next=y+5u<64?y+5u:63u,new_slot=next%9u,old_slot=old%9u;
  if(w->tags[new_slot]!=next){
   for(z=0;z<320;z++){
    int64_t prior=w->rows.color[old_slot][z],incoming;
    rc=field_q40_v274(load_bits(j->buffers[0].pointer,next*320u+z),&incoming);if(rc)return rc;
    w->columns[z]=w->columns[z]+incoming-prior;w->rows.color[new_slot][z]=incoming;
   }
   w->tags[new_slot]=next;
  }else for(z=0;z<320;z++)w->columns[z]=w->columns[z]+w->rows.color[new_slot][z]-w->rows.color[old_slot][z];
 }
 w->row++;return w->row<64?RC_PENDING:(w->output_index==j->count?RC_OK:RC_ARGUMENT);
}
static int fq_kernel_step(field_worker_v282 *w){
 rf_job *j=&w->job;unsigned end,i;int rc;unsigned kind=w->queue->request.kind;
 if(kind<5){
  rf_job chunk=*j;end=w->cursor+128u;if(end>j->count)end=j->count;chunk.count=end-w->cursor;
  for(i=0;i<j->program->inputs;i++)if(!j->program->input[i].scalar)chunk.input[i].pointer=(uint8_t*)j->input[i].pointer+w->cursor*(j->program->input[i].wide?8u:4u);
  for(i=0;i<j->program->outputs;i++)chunk.output[i].pointer=(uint8_t*)j->output[i].pointer+w->cursor*(j->program->output[i].wide?8u:4u);
  rc=field_cpu_v274(&chunk,&kind);if(rc)return rc;
  w->cursor=end;return end<j->count?RC_PENDING:RC_OK;
 }
 if(!w->phase){
  unsigned total=kind==5?14400u:20480u;const void *source=j->buffers[kind==5?2:0].pointer;
  end=w->cursor+4096u;if(end>total)end=total;
  for(i=w->cursor;i<end;i++)if(!field_rows_domain_v303(load_bits(source,i)))return RC_ARGUMENT;
  w->cursor=end;if(end<total)return RC_PENDING;
  w->phase=1;w->cursor=0;
  if(kind==6){
   unsigned y;
   for(y=0;y<=4;y++){
    for(i=0;i<320;i++){rc=field_q40_v274(load_bits(source,y*320u+i),w->rows.color[y]+i);if(rc)return rc;}
    w->tags[y]=y;
   }
   for(i=0;i<320;i++){unsigned k;w->columns[i]=w->rows.color[0][i]*5;for(k=1;k<=4;k++)w->columns[i]+=w->rows.color[k][i];}
  }
  return RC_PENDING;
 }
 if(kind==6)return fq_color_row(w);
 end=w->cursor+256u;if(end>76800u)end=76800u;
 for(i=w->cursor;i<end;i++){
  unsigned x=i%320u,y=i/320u;
  unsigned lo=y<2?0:(y-2u)/4u,hi=lo<59?lo+1u:59,p;
  unsigned fraction=y<2?0:2u*((y-2u)&3u)+1u,needed[2]={lo,hi};double c[3];
  for(p=0;p<2;p++)if(w->tags[needed[p]&1u]!=needed[p]){
   rc=horizontal_row_v303(j->buffers[2].pointer,needed[p],w->rows.gray[needed[p]&1u]);if(rc)return rc;
   w->tags[needed[p]&1u]=needed[p];
  }
  for(p=0;p<3;p++)c[p]=vertical_v303(w->rows.gray[lo&1u][p][x],w->rows.gray[hi&1u][p][x],fraction);
  rc=gray_pixel(((const double*)j->buffers[0].pointer)[i],((const float*)j->buffers[1].pointer)[i],c[0],c[1],c[2],(float*)j->buffers[3].pointer+i);
  if(rc)return rc;
 }
 w->cursor=end;return end<76800?RC_PENDING:RC_OK;
}
static int field_worker_body_v347(void *s){
 field_worker_v282 *w=s;field_mailbox_v282 *q;unsigned i;int rc;
 if(!w || w->magic!=FIELD_MAILBOX_MAGIC)return RC_ARGUMENT;
 q=w->queue;
 if(!w->active){
  if(fq_load(&q->state)!=FQ_READY)return RC_PENDING;
  w->ticket=q->ticket;w->cursor=w->row=w->output_index=w->phase=w->chunks=0;
  for(i=0;i<9;i++)w->tags[i]=64;
  rc=fq_job(&q->request,&w->job);
  if(rc || q->request.ticket!=w->ticket){q->status=RC_ARGUMENT;fq_store(&q->state,FQ_COMPLETE);return RC_ARGUMENT;}
  for(i=0;i<w->job.buffer_count;i++)w->cache.invalidate(w->cache.user,w->job.buffers[i].pointer,w->job.buffers[i].bytes);
  w->cache.barrier(w->cache.user);w->active=1;fq_store(&q->state,FQ_RUNNING);
 }
 rc=fq_load(&q->cancel)?RC_CANCELLED:fq_kernel_step(w);w->chunks++;q->chunks=w->chunks;
 if(rc==RC_PENDING)return rc;
 for(i=0;i<w->job.buffer_count;i++)if(w->job.buffers[i].write)w->cache.clean(w->cache.user,w->job.buffers[i].pointer,w->job.buffers[i].bytes);
 w->cache.barrier(w->cache.user);q->status=(uint32_t)rc;q->completed++;w->active=0;fq_store(&q->state,FQ_COMPLETE);return rc;
}
int field_cancel_v282(void *queue,uint32_t ticket){
 field_mailbox_v282 *q=queue;if(!q || q->magic!=FIELD_MAILBOX_MAGIC || !ticket || q->ticket!=ticket || fq_load(&q->state)==FQ_EMPTY)return RC_STALE;
 fq_store(&q->cancel,1);return RC_CANCELLED;
}
int field_take_v282(void *queue,uint32_t ticket,const field_cache_v282 *cache){
 field_mailbox_v282 *q=queue;rf_job j;unsigned i;int rc;
 if(!q || q->magic!=FIELD_MAILBOX_MAGIC || !ticket || q->ticket!=ticket || !fq_cache(cache) || fq_load(&q->state)==FQ_EMPTY)return RC_STALE;
 if(fq_load(&q->state)!=FQ_COMPLETE)return RC_PENDING;
 rc=fq_job(&q->request,&j);
 if(!rc){cache->barrier(cache->user);for(i=0;i<j.buffer_count;i++)if(j.buffers[i].write)cache->invalidate(cache->user,j.buffers[i].pointer,j.buffers[i].bytes);}
 if(!rc)rc=fq_load(&q->cancel)?RC_CANCELLED:(int)q->status;
 fq_store(&q->state,FQ_EMPTY);return rc;
}
uint32_t field_chunks_v282(const void *s){const field_mailbox_v282 *q=s;return q && q->magic==FIELD_MAILBOX_MAGIC?q->chunks:0;}
uint32_t field_completed_v282(const void *s){const field_mailbox_v282 *q=s;return q && q->magic==FIELD_MAILBOX_MAGIC?q->completed:0;}
uint32_t field_mailbox_state_v282(const void *s){const field_mailbox_v282 *q=s;return q && q->magic==FIELD_MAILBOX_MAGIC?fq_load(&q->state):UINT32_MAX;}

int field_worker_step_v282(void *p){
 field_worker_v282 *w=p;unsigned phase=w?w->phase:0,cursor=w?w->cursor:0,row=w?w->row:0;int result=field_worker_body_v347(p);
 if(w && w->magic==FIELD_MAILBOX_MAGIC && (phase!=w->phase || cursor!=w->cursor || row!=w->row))trace_cpu_phase_v347("CPU1_FIELD",w->ticket,phase,w->phase,cursor,w->cursor,row,w->row,w->queue->request.kind,result);
 return result;
}
