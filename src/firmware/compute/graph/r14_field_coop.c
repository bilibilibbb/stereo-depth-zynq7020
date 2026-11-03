#include "r14_field_coop.h"
#include "r14_filter_programs.h"
#include "field_cpu_v274.h"
#include "field_async_v282.h"
#include <string.h>
#define COOKIE 0x46433234u
#define JOB_COOKIE 0x464a3234u
static int live(const rf_job *j){return j && j->cookie==JOB_COOKIE && j->owned;}
static int span(const rc_buffer *b,uint32_t bytes,unsigned write){
 return b->pointer && b->bytes==bytes && b->write==write && !(b->address&7u) &&
        b->address>=0x19000000u && (uint64_t)b->address+bytes<=0x1a000000u &&
        (uintptr_t)b->pointer<=UINTPTR_MAX-bytes-63u;
}
static int overlap(uint64_t a,uint32_t na,uint64_t b,uint32_t nb){
 uint64_t ae=(a+na+63u)&~UINT64_C(63),be=(b+nb+63u)&~UINT64_C(63);
 return (a&~UINT64_C(63))<be && (b&~UINT64_C(63))<ae;
}
static int conflict(const rc_buffer *a,const rc_buffer *b){
 return (a->write || b->write) && (overlap(a->address,a->bytes,b->address,b->bytes) ||
        overlap((uintptr_t)a->pointer,a->bytes,(uintptr_t)b->pointer,b->bytes));
}
static int fail(rf_job *j,int error){j->state=RF_FAILED;j->error=(uint32_t)error;return error;}
int rf_init(rf_coop *c,rc_device *d,uint32_t limit){
 unsigned i;
 if(!c || !d || !d->opened || !limit || d->engine[RC_FIELD].state)return RC_ARGUMENT;
 if(c->cookie==COOKIE)for(i=0;i<2;i++)if(live(c->registered[i]))return RC_AGAIN;
 memset(c,0,sizeof(*c));c->cookie=COOKIE;c->device=d;c->cycle_limit=limit;return RC_OK;
}
int rf_begin(rf_coop *c,rf_job *j,unsigned frame,const rf_program *p,uint32_t count,
             const rc_buffer *inputs,const rc_buffer *outputs){
 rf_job next;unsigned i,k;
 if(!c || c->cookie!=COOKIE || !j || frame>1 || !p || !p->program || !p->input || !p->output ||
    !p->words || p->words>64 || !p->inputs || p->inputs>40 || !p->outputs || p->outputs>8 ||
    !count || count>81920 || !inputs || !outputs)return RC_ARGUMENT;
 if(p!=&r14_program_gray_fields && p!=&r14_program_gray_coeff &&
    p!=&r14_program_gray_tail && p!=&r14_program_color_covariance &&
    p!=&r14_program_color_rhs)return RC_ARGUMENT;
 if(live(j) || live(c->registered[frame]))return RC_AGAIN;
 if(c->device->engine[RC_FIELD].state==RC_QUARANTINE || c->device->engine[RC_FIELD].state==RC_CLEAR)return RC_AGAIN;
 memset(&next,0,sizeof(next));next.cookie=JOB_COOKIE;next.owned=1;next.state=RF_READY;next.program=p;next.count=count;
 for(i=0;i<p->words;i++){
  uint32_t w=p->program[i];
  if((w&15u)>8 || ((w>>4)&31u)>15 || ((w>>9)&7u)>4 || ((w>>12)&31u)>15 ||
     ((w>>18)&7u)>4 || ((w>>21)&31u)>15 || (w>>27) || (((w>>9)&7u)!=0 && ((w>>12)&15u)>=8) ||
     ((w&15u)<=3 && ((w>>18)&7u)!=0 && ((w>>21)&15u)>=8) ||
     ((w&15u)>=2 && (w&15u)<=5 && ((w>>4)&15u)>=8))return RC_ARGUMENT;
 }
 for(i=0;i<p->inputs;i++){
  const rc_config *q=p->input+i;
  if(q->kind!=1 || q->index!=i || q->bank>4 || q->slot>15 || (q->bank && q->slot>=8) || q->wide>1 || q->scalar>1)return RC_ARGUMENT;
  if(q->scalar){if(((q->data>>52)&2047u)==2047u)return RC_ARGUMENT;}
  else{
   if(!span(inputs+i,count*(q->wide?8u:4u),0))return RC_ARGUMENT;
   next.input[i]=inputs[i];next.buffers[next.buffer_count++]=inputs[i];
  }
 }
 for(i=0;i<p->outputs;i++){
  const rc_config *q=p->output+i;
  if(q->kind!=2 || q->index!=i || q->bank>4 || q->slot>15 || (q->bank && q->slot>=8) || q->wide>1 || q->scalar ||
     !span(outputs+i,count*(q->wide?8u:4u),1))return RC_ARGUMENT;
  next.output[i]=outputs[i];next.buffers[next.buffer_count++]=outputs[i];
 }
 for(i=0;i<next.buffer_count;i++){
  unsigned other=frame^1u;
  for(k=0;k<i;k++)if(conflict(next.buffers+i,next.buffers+k))return RC_LEASE_CONFLICT;
  if(live(c->registered[other]))for(k=0;k<c->registered[other]->buffer_count;k++)
   if(conflict(next.buffers+i,c->registered[other]->buffers+k))return RC_LEASE_CONFLICT;
 }
 *j=next;c->registered[frame]=j;return RC_OK;
}
int rf_native_begin(rf_coop *c,rf_job *j,unsigned frame,uint32_t epoch,const rc_buffer *buffers){
 rf_job next;unsigned i,k;static const uint32_t sizes[4]={614400u,307200u,115200u,307200u};
 if(!c || c->cookie!=COOKIE || !j || frame>1 || !epoch || (epoch&0x80000000u) || !buffers)return RC_ARGUMENT;
 if(live(j) || live(c->registered[frame]))return RC_AGAIN;
 if(c->device->engine[RC_FIELD].state==RC_QUARANTINE || c->device->engine[RC_FIELD].state==RC_CLEAR)return RC_AGAIN;
 memset(&next,0,sizeof(next));next.cookie=JOB_COOKIE;next.owned=1;next.state=RF_READY;
 next.count=76800u;next.native_mode=1;next.buffer_count=4;next.native_address[4]=epoch;
 for(i=0;i<4;i++){
  if(!span(buffers+i,sizes[i],i==3))return RC_ARGUMENT;
  next.buffers[i]=buffers[i];next.native_address[i]=buffers[i].address;
  for(k=0;k<i;k++)if(conflict(next.buffers+i,next.buffers+k))return RC_LEASE_CONFLICT;
  if(live(c->registered[frame^1u]))for(k=0;k<c->registered[frame^1u]->buffer_count;k++)
   if(conflict(next.buffers+i,c->registered[frame^1u]->buffers+k))return RC_LEASE_CONFLICT;
 }
 *j=next;c->registered[frame]=j;return RC_OK;
}
int rf_color_native_begin(rf_coop *c,rf_job *j,unsigned frame,unsigned disparity,const rc_buffer *buffers){
 rf_job next;unsigned i,k;uint32_t sizes[3],count=60u*(80u-disparity);
 if(!c || c->cookie!=COOKIE || !j || frame>1 || disparity>16 || !buffers)return RC_ARGUMENT;
 if(live(j) || live(c->registered[frame]))return RC_AGAIN;
 if(c->device->engine[RC_FIELD].state==RC_QUARANTINE || c->device->engine[RC_FIELD].state==RC_CLEAR)return RC_AGAIN;
 sizes[0]=163840u;sizes[1]=count*32u;sizes[2]=count*4u;
 memset(&next,0,sizeof(next));next.cookie=JOB_COOKIE;next.owned=1;next.state=RF_READY;
 next.count=count;next.native_mode=2;next.buffer_count=3;next.native_address[4]=0x80000000u|disparity;
 for(i=0;i<3;i++){
  if(!span(buffers+i,sizes[i],i==2))return RC_ARGUMENT;
  next.buffers[i]=buffers[i];next.native_address[i]=buffers[i].address;
  for(k=0;k<i;k++)if(conflict(next.buffers+i,next.buffers+k))return RC_LEASE_CONFLICT;
  if(live(c->registered[frame^1u]))for(k=0;k<c->registered[frame^1u]->buffer_count;k++)
   if(conflict(next.buffers+i,c->registered[frame^1u]->buffers+k))return RC_LEASE_CONFLICT;
 }
 *j=next;c->registered[frame]=j;return RC_OK;
}
/* Exact typed CPU FIELD; the wait state permits cancellation before any
 * CPU stores. Numeric execution is foreground in this integration prototype.
 * Real CPU1 submission, interrupts and execution time are separate work. */
static uint32_t cpu_jobs[7],cpu_values[7];
void field_coop_counts_v280(uint32_t jobs[7],uint32_t values[7]){
 memcpy(jobs,cpu_jobs,sizeof(cpu_jobs));memcpy(values,cpu_values,sizeof(cpu_values));
}
int rf_bind_cpu_queue_v284(rf_coop *c,void *queue,const struct field_cache_v282 *cache){
 if(!c || c->cookie!=COOKIE || c->active || live(c->registered[0]) || live(c->registered[1]) ||
    !queue || field_mailbox_state_v282(queue)!=0 || !cache || !cache->clean || !cache->invalidate || !cache->barrier)return RC_ARGUMENT;
 c->cpu_queue=queue;c->cpu_cache=cache;c->cpu_ticket=0;return RC_OK;
}
static unsigned cpu_kind_v284(const rf_job *j){
 if(j->native_mode)return j->native_mode==1?5u:6u;
 if(j->program==&r14_program_gray_fields)return 0;
 if(j->program==&r14_program_gray_coeff)return 1;
 if(j->program==&r14_program_gray_tail)return 2;
 if(j->program==&r14_program_color_covariance)return 3;
 return 4;
}
int rf_step(rf_coop *c,rf_job *j){
 unsigned kind;int error;
 if(!c || c->cookie!=COOKIE || !live(j))return RC_ARGUMENT;
 if(j->state==RF_DONE)return RC_OK;
 if(j->state==RF_FAILED || j->state==RF_CANCELLED)return (int)j->error;
 if(j->state==RF_READY){
  if(c->active || c->device->engine[RC_FIELD].state)return RC_PENDING;
  if(c->cpu_queue){
   error=field_submit_v282(c->cpu_queue,j,c->cpu_cache,&j->tag);
   if(error==RC_AGAIN)return RC_PENDING;
   if(error)return fail(j,error);
   c->cpu_ticket=j->tag;
  }
  c->active=j;j->state=RF_WAIT;return RC_PENDING;
 }
 if(c->active!=j || j->state!=RF_WAIT)return fail(j,RC_STALE);
 if(c->cpu_queue){
  if(!c->cpu_ticket || j->tag!=c->cpu_ticket)return fail(j,RC_STALE);
  error=field_take_v282(c->cpu_queue,j->tag,c->cpu_cache);
  if(error==RC_PENDING)return error;
  if(error==RC_STALE)return fail(j,error);
  c->cpu_ticket=0;if(error)return fail(j,error);kind=cpu_kind_v284(j);
 }else{error=field_cpu_v274(j,&kind);if(error)return fail(j,error);}

 j->state=RF_DONE;c->jobs++;c->values+=(uint64_t)j->count*(j->native_mode?1u:j->program->outputs);
 cpu_jobs[kind]++;cpu_values[kind]+=j->count*(j->native_mode?1u:j->program->outputs);
 return RC_OK;
}
int rf_cancel(rf_coop *c,rf_job *j){
 int error=RC_CANCELLED;
 if(!c || c->cookie!=COOKIE || !live(j) || j->state>=RF_DONE)return RC_ARGUMENT;
 if(c->active==j && c->cpu_queue && c->cpu_ticket){
  error=field_cancel_v282(c->cpu_queue,c->cpu_ticket);if(error!=RC_CANCELLED)return fail(j,error);
 }
 if(c->active==j && c->device->engine[RC_FIELD].state){
  error=rc_cancel(c->device,RC_FIELD);if(error!=RC_CANCELLED)return fail(j,error);
 }
 j->state=RF_CANCELLED;j->error=RC_CANCELLED;return RC_CANCELLED;
}
int rf_recover(rf_coop *c){
 int error;rc_engine *e;
 if(!c || c->cookie!=COOKIE)return RC_ARGUMENT;
 if(c->active && c->active->state<RF_DONE)return RC_AGAIN;
 if(c->cpu_queue && c->cpu_ticket){
  error=field_take_v282(c->cpu_queue,c->cpu_ticket,c->cpu_cache);
  if(error==RC_PENDING || error==RC_STALE)return error;
  c->cpu_ticket=0;
 }
 e=c->device->engine+RC_FIELD;if(e->state==RC_IDLE)return RC_OK;
 if(e->state==RC_QUARANTINE){error=rc_recover_begin(c->device,RC_FIELD);return error==RC_OK?RC_PENDING:error;}
 if(e->state!=RC_CLEAR)return RC_AGAIN;
 return rc_recover_poll(c->device,RC_FIELD);
}
int rf_release(rf_coop *c,rf_job *j){
 unsigned i;
 if(!c || c->cookie!=COOKIE || !live(j) || j->state<RF_DONE)return RC_ARGUMENT;
 if(c->active==j){if(c->cpu_ticket || c->device->engine[RC_FIELD].state)return RC_AGAIN;c->active=0;}
 for(i=0;i<2;i++)if(c->registered[i]==j)c->registered[i]=0;
 j->owned=0;return RC_OK;
}
