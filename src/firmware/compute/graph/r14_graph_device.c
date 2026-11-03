#include "r14_graph_device.h"
#include <string.h>
#define COOKIE 0x47443434u
static int live(uint32_t s){return s==R14_LAYER_READY || s==R14_LAYER_WAIT;}
int r14_graph_device_init(r14_graph_device *d,uint32_t frame,r14_nn_coop *n,r14_integer_coop *i){
 if(!d || frame>1 || !n || !i || !n->neural || !n->neural->device || n->neural->device!=i->device ||
    !i->device->opened || n->bytes!=i->bytes || n->memory[0]!=i->memory[0] || n->memory[1]!=i->memory[1] ||
    n->address[0]!=i->address[0] || n->address[1]!=i->address[1])return RN_ARGUMENT;
 if(d->cookie==COOKIE && (live(d->layer.state) || live(d->integer_job.state) || d->color_job.owned || d->gray_job.owned))return RN_BUSY_ERROR;
 if(n->neural->device->engine[0].state || i->device->engine[1].state)return RN_BUSY_ERROR;
 memset(d,0,sizeof(*d));d->cookie=COOKIE;d->frame=frame;d->filter_frame=frame;d->neural=n;d->integer=i;return RN_OK;
}
static int begin(void *u,int id,const rn_parameter *p,const void *src,int ih,int iw,int16_t *dst,int oh,int ow,int wide,int fraction,int relu){
 r14_graph_device *d=(r14_graph_device*)u;
 if(!d || d->cookie!=COOKIE)return RN_ARGUMENT;
 return r14_nn_coop_begin(d->neural,&d->layer,d->frame,id,p,src,ih,iw,dst,oh,ow,wide,fraction,relu);
}
static int poll(void *u){r14_graph_device *d=(r14_graph_device*)u;return !d || d->cookie!=COOKIE?RN_ARGUMENT:r14_nn_coop_step(d->neural,&d->layer);}
static int cancel(void *u){r14_graph_device *d=(r14_graph_device*)u;return !d || d->cookie!=COOKIE?RN_ARGUMENT:r14_nn_coop_cancel(d->neural,&d->layer);}
/* Buffer leases, rather than another frame's busy bit, determine whether
 * this private arena can be reused. A failed/cancelled owner retains its
 * buffers in the common registry until physical drain and clear. Weights are
 * shared read-only and do not block release of a completed private frame. */
static int arena_quiet(r14_graph_device *d,unsigned page){
 const rc_engine *e;uint64_t first,last;unsigned i;
 if(!d || d->cookie!=COOKIE)return 0;
 first=d->neural->address[d->frame];last=first+d->neural->bytes;
 e=d->neural->neural->device->engine+page;
 for(i=0;i<e->buffer_count;i++){
  uint64_t begin=e->buffers[i].address,end=begin+e->buffers[i].bytes;
  if(begin<last && first<end)return 0;
 }
 return 1;
}
static int quiet(void *u){
 r14_graph_device *d=(r14_graph_device *)u;
 return d && d->cookie==COOKIE && !live(d->layer.state) && arena_quiet(d,0);
}
static int integer_begin(void *u,const r14_integer_request *r){
 r14_graph_device *d=(r14_graph_device*)u;return !d || d->cookie!=COOKIE?RN_ARGUMENT:r14_integer_coop_begin(d->integer,&d->integer_job,d->frame,r);
}
static int integer_poll(void *u){r14_graph_device *d=(r14_graph_device*)u;return !d || d->cookie!=COOKIE?RN_ARGUMENT:r14_integer_coop_step(d->integer,&d->integer_job);}
static int integer_cancel(void *u){r14_graph_device *d=(r14_graph_device*)u;return !d || d->cookie!=COOKIE?RN_ARGUMENT:r14_integer_coop_cancel(d->integer,&d->integer_job);}
static int integer_quiet(void *u){
 r14_graph_device *d=(r14_graph_device *)u;
 return d && d->cookie==COOKIE && !live(d->integer_job.state) && arena_quiet(d,1);
}
static int filter_result(int rc){
 if(rc==RC_OK)return RN_OK;
 if(rc==RC_PENDING || rc==RC_AGAIN)return RN_PENDING;
 if(rc==RC_CANCELLED)return RN_CANCELLED_ERROR;
 return RN_HARDWARE;
}
int r14_graph_device_filter_init(r14_graph_device *d,rcl_coop *color,rg_coop *gray,void *arena,uint32_t bytes){
 if(!d || d->cookie!=COOKIE || !color || !gray || !arena || ((uintptr_t)arena&63u) || bytes<RG_ARENA_BYTES ||
    color->device!=d->integer->device || gray->device!=d->integer->device)return RN_ARGUMENT;
 if(live(d->layer.state) || live(d->integer_job.state) || d->color_job.owned || d->gray_job.owned)return RN_BUSY_ERROR;
 /* Configuration words share one physical FIELD program/map. A task owns
  * all of them until completion/drained release, not just one MMIO word. */
 if(gray->field_service && gray->field_service!=&color->fields)return RN_ARGUMENT;
 if(!gray->field_service){
  unsigned f;if(gray->fields.active)return RN_BUSY_ERROR;
  for(f=0;f<2;f++)if(gray->fields.registered[f] && gray->fields.registered[f]->owned)return RN_BUSY_ERROR;
  gray->field_service=&color->fields;
 }
 d->color=color;d->gray=gray;d->filter_arena=arena;d->filter_bytes=bytes;return RN_OK;
}
static int color_begin(void *u,const float *logits,const float *guide,float *output,void *arena,size_t bytes){
 r14_graph_device *d=(r14_graph_device *)u;
 if(!d || d->cookie!=COOKIE || !d->color || d->gray_job.owned || arena!=d->filter_arena || bytes!=d->filter_bytes)return RN_ARGUMENT;
 return filter_result(rcl_begin(d->color,&d->color_job,d->filter_frame,logits,guide,output,d->filter_arena,d->filter_bytes));
}
static int color_poll(void *u){
 r14_graph_device *d=(r14_graph_device *)u;int rc;
 if(!d || d->cookie!=COOKIE || !d->color)return RN_ARGUMENT;
 rc=rcl_step(d->color,&d->color_job);
 if(rc==RC_OK)rc=rcl_release(d->color,&d->color_job);
 return filter_result(rc);
}
static int color_cancel(void *u){
 r14_graph_device *d=(r14_graph_device *)u;
 return !d || d->cookie!=COOKIE || !d->color?RN_ARGUMENT:filter_result(rcl_cancel(d->color,&d->color_job));
}
static int filter_arena_quiet(r14_graph_device *d){
 unsigned page,i;uint64_t first,last;
 if(!d || d->cookie!=COOKIE || !d->filter_arena)return 0;
 first=d->filter_frame?RG_ARENA1:RG_ARENA0;last=first+RG_ARENA_BYTES;
 for(page=RC_FIELD;page<=RC_BOX;page++){
  const rc_engine *e=d->integer->device->engine+page;
  for(i=0;i<e->buffer_count;i++){
   uint64_t start=e->buffers[i].address,end=start+e->buffers[i].bytes;
   if(start<last && first<end)return 0;
  }
 }
 return 1;
}
static int color_quiet(void *u){r14_graph_device *d=(r14_graph_device *)u;return d && d->cookie==COOKIE && !d->color_job.owned && filter_arena_quiet(d);}
static int gray_begin(void *u,const uint8_t *rgb,const float *median,float *output,void *arena,size_t bytes){
 r14_graph_device *d=(r14_graph_device *)u;
 if(!d || d->cookie!=COOKIE || !d->gray || d->color_job.owned || arena!=d->filter_arena || bytes!=d->filter_bytes)return RN_ARGUMENT;
 return filter_result(rg_begin(d->gray,&d->gray_job,d->filter_frame,rgb,median,output,d->filter_arena,d->filter_bytes));
}
static int gray_poll(void *u){
 r14_graph_device *d=(r14_graph_device *)u;int rc;
 if(!d || d->cookie!=COOKIE || !d->gray)return RN_ARGUMENT;
 rc=rg_step(d->gray,&d->gray_job);
 if(rc==RC_OK)rc=rg_release(d->gray,&d->gray_job);
 return filter_result(rc);
}
static int gray_cancel(void *u){
 r14_graph_device *d=(r14_graph_device *)u;
 return !d || d->cookie!=COOKIE || !d->gray?RN_ARGUMENT:filter_result(rg_cancel(d->gray,&d->gray_job));
}
static int gray_quiet(void *u){r14_graph_device *d=(r14_graph_device *)u;return d && d->cookie==COOKIE && !d->gray_job.owned && filter_arena_quiet(d);}
int r14_graph_device_filter_recover(r14_graph_device *d){
 int rc;
 if(!d || d->cookie!=COOKIE || !d->color || !d->gray)return RN_ARGUMENT;
 /* These explicit recovery calls are legal only for this terminal owner.
  * Another frame's busy engine yields PENDING; its job is never cancelled. */
 if(d->color_job.owned){
  if(d->color_job.state<RCL_DONE)return RN_BUSY_ERROR;
  rc=rcl_recover(d->color);if(rc!=RC_OK)return filter_result(rc);
  rc=rcl_release(d->color,&d->color_job);if(rc!=RC_OK)return filter_result(rc);
 }
 if(d->gray_job.owned){
  if(d->gray_job.state<RG_DONE)return RN_BUSY_ERROR;
  rc=rg_recover(d->gray);if(rc!=RC_OK)return filter_result(rc);
  rc=rg_release(d->gray,&d->gray_job);if(rc!=RC_OK)return filter_result(rc);
 }
 return RN_OK;
}
const r14_graph_ops *r14_graph_device_callbacks(void){
 static const r14_graph_ops ops={begin,poll,cancel,quiet,integer_begin,integer_poll,integer_cancel,integer_quiet,
  color_begin,color_poll,color_cancel,color_quiet,gray_begin,gray_poll,gray_cancel,gray_quiet};return &ops;
}

/* Rebind a released phase to a free compute pool. Another pool may be busy.
 * No READY/WAIT job, DMA owner or target registry may be overwritten. */
int r14_graph_device_bind_compute_frame(r14_graph_device *d,unsigned pool,unsigned filter){
 unsigned old;
 if(!d || d->cookie!=COOKIE || pool>1 || filter>1)return RN_ARGUMENT;
 if(live(d->layer.state) || live(d->integer_job.state) || d->neural->active==&d->layer || d->integer->active==&d->integer_job || d->color_job.owned || d->gray_job.owned)return RN_BUSY_ERROR;
 if((d->neural->registered[pool] && live(d->neural->registered[pool]->state)) || (d->integer->registered[pool] && live(d->integer->registered[pool]->state)))return RN_BUSY_ERROR;
 if(d->integer->device->engine[RC_NN].state==RC_QUARANTINE || d->integer->device->engine[RC_NN].state==RC_CLEAR || d->integer->device->engine[RC_INTEGER].state==RC_QUARANTINE || d->integer->device->engine[RC_INTEGER].state==RC_CLEAR)return RN_BUSY_ERROR;
 old=d->frame;
 if(d->neural->registered[old]==&d->layer)d->neural->registered[old]=0;
 if(d->integer->registered[old]==&d->integer_job)d->integer->registered[old]=0;
 memset(&d->layer,0,sizeof(d->layer));memset(&d->integer_job,0,sizeof(d->integer_job));d->frame=pool;d->filter_frame=filter;return RN_OK;
}
