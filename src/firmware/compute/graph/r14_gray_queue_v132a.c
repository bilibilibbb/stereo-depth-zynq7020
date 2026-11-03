#include "r14_gray_queue_v132a.h"
#include <string.h>
#define QCOOKIE 0x47513132u
static int result(int x){
 if(x==RC_OK)return RN_OK;
 if(x==RC_PENDING || x==RC_AGAIN)return RN_PENDING;
 if(x==RC_CANCELLED)return RN_CANCELLED_ERROR;
 return RN_HARDWARE;
}
static int span(const void *p,uint32_t bytes){return p && !((uintptr_t)p&63u) && (uintptr_t)p<=UINTPTR_MAX-bytes;}
static int overlap(const void *a,uint32_t na,const void *b,uint32_t nb){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<y+nb && y<x+na;
}
static int valid(const rgq_queue *q,unsigned f){return q && q->cookie==QCOOKIE && f<2;}
static int error(rgq_slot *s,int x){s->state=RGQ_FAILED;s->error=(uint32_t)x;return x;}
int rgq_owned(const rgq_queue *q,unsigned f){return valid(q,f) && q->slot[f].state!=RGQ_EMPTY;}
int rgq_init(rgq_queue *q,rg_coop *g,uint8_t *const rgb[2],float *const median[2],void *const arena[2],uint32_t bytes){
 const void *p[6];uint32_t n[6];unsigned f,i,j;
 if(!q || !g || !g->device || !rgb || !median || !arena || bytes<RG_ARENA_BYTES)return RN_ARGUMENT;
 if(q->cookie==QCOOKIE && (rgq_owned(q,0) || rgq_owned(q,1)))return RN_BUSY_ERROR;
 for(f=0;f<2;f++){
  p[f*3]=rgb[f];n[f*3]=RGQ_RGB_BYTES;p[f*3+1]=median[f];n[f*3+1]=RGQ_MEDIAN_BYTES;
  p[f*3+2]=arena[f];n[f*3+2]=bytes;
 }
 for(i=0;i<6;i++){
  if(!span(p[i],n[i]) || overlap(q,(uint32_t)sizeof(*q),p[i],n[i]))return RN_ARGUMENT;
  for(j=0;j<i;j++)if(overlap(p[i],n[i],p[j],n[j]))return RN_ARGUMENT;
 }
 memset(q,0,sizeof(*q));q->cookie=QCOOKIE;q->gray=g;
 for(f=0;f<2;f++){q->slot[f].rgb=rgb[f];q->slot[f].median=median[f];q->slot[f].arena=arena[f];q->slot[f].arena_bytes=bytes;}
 return RN_OK;
}
static int output_allowed(const rgq_queue *q,unsigned f,float *output){
 unsigned i;const rgq_slot *s;
 if(!span(output,RGQ_OUTPUT_BYTES) || overlap(output,RGQ_OUTPUT_BYTES,q,(uint32_t)sizeof(*q)))return 0;
 for(i=0;i<2;i++){
  s=q->slot+i;
  if(overlap(output,RGQ_OUTPUT_BYTES,s->rgb,RGQ_RGB_BYTES) ||
     overlap(output,RGQ_OUTPUT_BYTES,s->median,RGQ_MEDIAN_BYTES) ||
     overlap(output,RGQ_OUTPUT_BYTES,s->arena,s->arena_bytes))return 0;
  if(i!=f && s->state!=RGQ_EMPTY && overlap(output,RGQ_OUTPUT_BYTES,s->output,RGQ_OUTPUT_BYTES))return 0;
 }
 return 1;
}
int rgq_enqueue(rgq_queue *q,unsigned f,unsigned sequence,void *front,float *output){
 rgq_slot *s;int x;
 if(!valid(q,f) || !front || !output_allowed(q,f,output))return RN_ARGUMENT;
 s=q->slot+f;if(s->state!=RGQ_EMPTY)return RN_BUSY_ERROR;
 x=r14_graph_coop_detach_gray(front,s->rgb,RGQ_RGB_BYTES,s->median,RGQ_MEDIAN_BYTES,output);
 if(x)return x;
 s->sequence=sequence;s->output=output;s->front=front;s->state=RGQ_FRONT_HOLD;s->error=0;
 q->snapshot_copy_bytes+=RGQ_RGB_BYTES+RGQ_MEDIAN_BYTES;q->handoffs++;return RN_OK;
}
int rgq_submit_snapshots(rgq_queue *q,unsigned f,unsigned sequence,const uint8_t *rgb,const float *median,float *output){
 rgq_slot *s;unsigned i;
 if(!valid(q,f) || !span(rgb,RGQ_RGB_BYTES) || !span(median,RGQ_MEDIAN_BYTES) || !output_allowed(q,f,output))return RN_ARGUMENT;
 s=q->slot+f;if(s->state!=RGQ_EMPTY)return RN_BUSY_ERROR;
 /* Validate every source before either copy. Cross-type overlap could make
  * the first copy silently change the second source. Queued outputs and
  * arenas also remain exclusively owned until drained release. */
 if(overlap(rgb,RGQ_RGB_BYTES,median,RGQ_MEDIAN_BYTES) ||
    overlap(rgb,RGQ_RGB_BYTES,output,RGQ_OUTPUT_BYTES) ||
    overlap(median,RGQ_MEDIAN_BYTES,output,RGQ_OUTPUT_BYTES) ||
    overlap(rgb,RGQ_RGB_BYTES,q,(uint32_t)sizeof(*q)) ||
    overlap(median,RGQ_MEDIAN_BYTES,q,(uint32_t)sizeof(*q)))return RN_ARGUMENT;
 for(i=0;i<2;i++){
  const rgq_slot *o=q->slot+i;
  if(overlap(rgb,RGQ_RGB_BYTES,o->arena,o->arena_bytes) ||
     overlap(median,RGQ_MEDIAN_BYTES,o->arena,o->arena_bytes))return RN_ARGUMENT;
  if((i!=f || rgb!=s->rgb) && overlap(rgb,RGQ_RGB_BYTES,o->rgb,RGQ_RGB_BYTES))return RN_ARGUMENT;
  if(overlap(rgb,RGQ_RGB_BYTES,o->median,RGQ_MEDIAN_BYTES) ||
     overlap(median,RGQ_MEDIAN_BYTES,o->rgb,RGQ_RGB_BYTES))return RN_ARGUMENT;
  if((i!=f || median!=s->median) && overlap(median,RGQ_MEDIAN_BYTES,o->median,RGQ_MEDIAN_BYTES))return RN_ARGUMENT;
  if(o->state!=RGQ_EMPTY && (overlap(rgb,RGQ_RGB_BYTES,o->output,RGQ_OUTPUT_BYTES) ||
     overlap(median,RGQ_MEDIAN_BYTES,o->output,RGQ_OUTPUT_BYTES)))return RN_BUSY_ERROR;
 }
 if(rgb!=s->rgb){memcpy(s->rgb,rgb,RGQ_RGB_BYTES);q->snapshot_copy_bytes+=RGQ_RGB_BYTES;}
 if(median!=s->median){memcpy(s->median,median,RGQ_MEDIAN_BYTES);q->snapshot_copy_bytes+=RGQ_MEDIAN_BYTES;}
 s->sequence=sequence;s->output=output;s->front=0;s->state=RGQ_READY;s->error=0;return RN_OK;
}
int rgq_step(rgq_queue *q,unsigned f){
 rgq_slot *s;int x;unsigned y;
 if(!valid(q,f))return RN_ARGUMENT;
 s=q->slot+f;
 if(s->state==RGQ_DONE)return RN_OK;
 if(s->state==RGQ_FAILED || s->state==RGQ_CANCELLED)return (int)s->error;
 if(s->state==RGQ_FRONT_HOLD){
  x=r14_graph_coop_release_frontend(s->front);if(x==RN_BUSY_ERROR)return RN_PENDING;
  if(x)return error(s,x);
  s->front=0;s->state=RGQ_READY;return RN_PENDING;
 }
 if(s->state==RGQ_READY){
  x=rg_begin(q->gray,&s->job,f,s->rgb,s->median,s->output+5120u,s->arena,s->arena_bytes);
  if(x==RC_AGAIN)return RN_PENDING;
  if(x)return error(s,result(x));
  s->state=RGQ_RUNNING;return RN_PENDING;
 }
 if(s->state!=RGQ_RUNNING)return RN_ARGUMENT;
 x=rg_step(q->gray,&s->job);if(x==RC_PENDING || x==RC_AGAIN)return RN_PENDING;
 if(x)return error(s,result(x));
 x=rg_release(q->gray,&s->job);if(x)return error(s,result(x));
 for(y=0;y<16;y++)memcpy(s->output+y*320u,s->output+5120u,320u*sizeof(float));
 s->state=RGQ_DONE;q->completed++;return RN_OK;
}
int rgq_release(rgq_queue *q,unsigned f){
 rgq_slot *s;if(!valid(q,f))return RN_ARGUMENT;s=q->slot+f;
 if(s->state!=RGQ_DONE || s->job.owned || s->front)return RN_BUSY_ERROR;
 s->state=RGQ_EMPTY;s->output=0;s->error=0;return RN_OK;
}
int rgq_cancel(rgq_queue *q,unsigned f){
 rgq_slot *s;int x;if(!valid(q,f))return RN_ARGUMENT;s=q->slot+f;
 if(s->state==RGQ_EMPTY || s->state>=RGQ_DONE)return RN_ARGUMENT;
 if(s->front){x=r14_graph_coop_cancel(s->front);if(x!=RN_CANCELLED_ERROR)return error(s,x);}
 if(s->job.owned){x=rg_cancel(q->gray,&s->job);if(x!=RC_CANCELLED)return error(s,result(x));}
 s->state=RGQ_CANCELLED;s->error=RN_CANCELLED_ERROR;q->cancelled++;return RN_CANCELLED_ERROR;
}
int rgq_recover(rgq_queue *q,unsigned f){
 rgq_slot *s;int x;if(!valid(q,f))return RN_ARGUMENT;s=q->slot+f;
 if(s->state!=RGQ_FAILED && s->state!=RGQ_CANCELLED)return RN_ARGUMENT;
 if(s->job.owned){
  x=rg_recover(q->gray);if(x==RC_AGAIN || x==RC_PENDING)return RN_PENDING;if(x)return result(x);
  x=rg_release(q->gray,&s->job);if(x==RC_AGAIN)return RN_PENDING;if(x)return result(x);
 }
 if(s->front){x=r14_graph_coop_release(s->front);if(x==RN_BUSY_ERROR)return RN_PENDING;if(x)return x;s->front=0;}
 s->state=RGQ_EMPTY;s->output=0;s->error=0;q->recovered++;return RN_OK;
}
