#include "r14_integer_coop.h"
#include <string.h>
#define COOKIE 0x43493234u
static int result(int rc){
 switch(rc){case RC_OK:return RN_OK;case RC_PENDING:case RC_AGAIN:return RN_PENDING;
 case RC_ARGUMENT:case RC_LEASE_CONFLICT:return RN_ARGUMENT;case RC_IO_ERROR:return RN_IO_ERROR;
 case RC_IDENTITY:return RN_IDENTITY;case RC_HARDWARE:return RN_HARDWARE;case RC_STALE:return RN_STALE;
 case RC_TIMEOUT:return RN_TIMEOUT;case RC_CANCELLED:return RN_CANCELLED_ERROR;
 case RC_RESET_REQUIRED:return RN_RESET_REQUIRED;default:return RN_HARDWARE;}
}
static int leased(uint32_t state){return state==R14_LAYER_READY || state==R14_LAYER_WAIT;}
static int location(r14_integer_coop *c,uint32_t f,void *pointer,uint32_t bytes,rc_buffer *b,unsigned write){
 uintptr_t at,p=(uintptr_t)pointer,base=(uintptr_t)c->memory[f];
 if(!pointer || (p&63u) || p<base || !bytes)return 0;
 at=p-base;if(at>c->bytes || bytes>c->bytes-at)return 0;
 b->pointer=pointer;b->address=c->address[f]+(uint32_t)at;b->bytes=bytes;b->write=(uint8_t)write;return 1;
}
int r14_integer_coop_init(r14_integer_coop *c,rc_device *d,void *a,void *b,uint32_t bytes,uint32_t limit){
 unsigned i;
 if(c && c->cookie==COOKIE)for(i=0;i<2;i++)if(c->registered[i] && leased(c->registered[i]->state))return RN_BUSY_ERROR;
 if(!c || !d || !d->opened || d->engine[RC_INTEGER].state || !a || !b || !limit ||
    ((uintptr_t)a&63u) || ((uintptr_t)b&63u) || bytes!=R14_DUAL_TENSOR_BYTES ||
    bytes>UINTPTR_MAX-(uintptr_t)a || bytes>UINTPTR_MAX-(uintptr_t)b ||
    ((uintptr_t)a<=(uintptr_t)b?(uintptr_t)b-(uintptr_t)a<bytes:(uintptr_t)a-(uintptr_t)b<bytes) || !r14_dual_layout_valid())return RN_ARGUMENT;
 memset(c,0,sizeof(*c));c->cookie=COOKIE;c->device=d;c->cycle_limit=limit;c->bytes=bytes;
 c->memory[0]=(uint8_t*)a;c->memory[1]=(uint8_t*)b;c->address[0]=R14_DUAL_TENSOR0;c->address[1]=R14_DUAL_TENSOR1;return RN_OK;
}
int r14_integer_coop_begin(r14_integer_coop *c,r14_integer_job *j,uint32_t f,const r14_integer_request *r){
 r14_integer_job next;uint32_t count,af,bf,df;unsigned n=0;
 if(!c || c->cookie!=COOKIE || !j || !r || f>1)return RN_ARGUMENT;
 if((c->registered[f] && leased(c->registered[f]->state)) || leased(j->state))return RN_BUSY_ERROR;
 if(c->device->engine[RC_INTEGER].state==RC_QUARANTINE || c->device->engine[RC_INTEGER].state==RC_CLEAR)return RN_BUSY_ERROR;
 if(r->op==3){
  if(r->channels!=29 || r->height!=64 || r->width!=80 || r->a_wide || r->b_wide ||
     r->a_fraction!=11 || r->b_fraction!=11 || r->destination_fraction!=11 || r->a_relu || r->b_relu)return RN_ARGUMENT;
  memset(&next,0,sizeof(next));next.request=*r;next.frame=f;
  if(!location(c,f,(void*)r->a,81920u,&next.buffer[0],0) ||
     !location(c,f,(void*)r->b,81920u,&next.buffer[1],0) ||
     !location(c,f,(void*)r->prior,81920u,&next.buffer[2],0) ||
     !location(c,f,r->destination,296960u,&next.buffer[3],1))return RN_ARGUMENT;
  next.buffers=4;next.descriptor[0]=3;next.descriptor[1]=next.buffer[2].address;
  next.descriptor[2]=next.buffer[0].address;next.descriptor[3]=next.buffer[1].address;
  next.descriptor[4]=next.buffer[3].address;next.descriptor[5]=81920u;next.descriptor[6]=81920u;
  next.descriptor[7]=296960u;next.descriptor[8]=c->cycle_limit;
  next.state=R14_LAYER_READY;*j=next;c->registered[f]=j;return RN_OK;
 }
 if((r->op!=1 && r->op!=2) || !r->channels || r->channels>32 || !r->height || r->height>256 || r->width<4 || r->width>320 || (r->width&3u) ||
    r->a_wide<0 || r->a_wide>1 || r->b_wide<0 || r->b_wide>1 || r->a_relu<0 || r->a_relu>1 || r->b_relu<0 || r->b_relu>1 ||
    r->a_fraction<9 || r->a_fraction>16 || (r->op==1 && (r->b_fraction<9 || r->b_fraction>16)) ||
    (r->op==2 && (r->width>160 || r->height>128 || r->a_fraction>12 || r->b)))return RN_ARGUMENT;
 af=(uint32_t)r->a_fraction;bf=r->op==1?(uint32_t)r->b_fraction:af; /* resize second sample belongs to the same source plane */df=r->op==1?(af>bf?af:bf):af+4u;
 if((uint32_t)r->destination_fraction!=df)return RN_ARGUMENT;
 memset(&next,0,sizeof(next));next.request=*r;next.frame=f;count=r->channels*r->height*r->width;
 if(!location(c,f,(void*)r->a,count*(r->a_wide?4u:2u),&next.buffer[n++],0))return RN_ARGUMENT;
 if(r->op==1 && !location(c,f,(void*)r->b,count*(r->b_wide?4u:2u),&next.buffer[n++],0))return RN_ARGUMENT;
 if(!location(c,f,r->destination,count*(r->op==2?16u:4u),&next.buffer[n++],1))return RN_ARGUMENT;
 next.buffers=n;next.descriptor[0]=r->op|((uint32_t)r->a_wide<<8)|((uint32_t)r->b_wide<<9)|1024u|
     ((uint32_t)r->a_relu<<11)|((uint32_t)r->b_relu<<12)|(af<<16)|(bf<<21)|(13u<<26);
 next.descriptor[1]=r->width|(r->height<<10)|(r->channels<<20);
 next.descriptor[2]=next.buffer[0].address;next.descriptor[5]=next.buffer[0].bytes;
 if(r->op==1){next.descriptor[3]=next.buffer[1].address;next.descriptor[6]=next.buffer[1].bytes;}
 next.descriptor[4]=next.buffer[n-1].address;next.descriptor[7]=next.buffer[n-1].bytes;next.descriptor[8]=c->cycle_limit;
 next.state=R14_LAYER_READY;*j=next;c->registered[f]=j;return RN_OK;
}
static int fail(r14_integer_job *j,int rc){j->state=R14_LAYER_FAILED;j->error=(uint32_t)rc;return rc;}
int r14_integer_coop_step(r14_integer_coop *c,r14_integer_job *j){
 rc_engine *e;int rc;
 if(!c || c->cookie!=COOKIE || !j){return RN_ARGUMENT;}e=&c->device->engine[RC_INTEGER];
 if(j->state==R14_LAYER_DONE)return RN_OK;
 if(j->state==R14_LAYER_FAILED || j->state==R14_LAYER_CANCELLED)return j->error?(int)j->error:RN_HARDWARE;
 if(j->state==R14_LAYER_READY){
  if(e->state==RC_QUARANTINE || e->state==RC_CLEAR)return fail(j,e->first_error?result((int)e->first_error):RN_HARDWARE);
  if(c->active || e->state)return RN_PENDING;
  rc=rc_begin(c->device,RC_INTEGER,j->descriptor,10,j->buffer,j->buffers);
  if(rc==RC_AGAIN){return RN_PENDING;}if(rc)return fail(j,result(rc));
  c->active=j;j->tag=e->pending_tag;j->state=R14_LAYER_WAIT;return RN_PENDING;
 }
 if(j->state!=R14_LAYER_WAIT || c->active!=j || j->tag!=e->pending_tag)return RN_STALE;
 rc=result(rc_poll(c->device,RC_INTEGER));if(rc==RN_PENDING)return rc;
 c->active=0;if(rc)return fail(j,rc);j->state=R14_LAYER_DONE;c->jobs++;c->values+=j->descriptor[7]/(j->descriptor[0]==3u?2u:4u);return RN_OK;
}
int r14_integer_coop_cancel(r14_integer_coop *c,r14_integer_job *j){
 int rc;if(!c || c->cookie!=COOKIE || !j)return RN_ARGUMENT;
 if(j->state==R14_LAYER_WAIT){
  if(c->active!=j || c->device->engine[RC_INTEGER].pending_tag!=j->tag)return RN_STALE;
  rc=result(rc_cancel(c->device,RC_INTEGER));c->active=0;j->state=R14_LAYER_CANCELLED;j->error=(uint32_t)rc;return rc;
 }
 if(j->state==R14_LAYER_READY){j->state=R14_LAYER_CANCELLED;j->error=RN_CANCELLED_ERROR;return RN_CANCELLED_ERROR;}
 return RN_ARGUMENT;
}
int r14_integer_coop_recover(r14_integer_coop *c){
 rc_engine *e;int rc;if(!c || c->cookie!=COOKIE || c->active)return RN_ARGUMENT;e=&c->device->engine[RC_INTEGER];
 if(e->state==RC_IDLE)return RN_OK;
 if(e->state==RC_QUARANTINE){rc=rc_recover_begin(c->device,RC_INTEGER);return rc==RC_OK?RN_PENDING:result(rc);}
 if(e->state!=RC_CLEAR){return RN_BUSY_ERROR;}return result(rc_recover_poll(c->device,RC_INTEGER));
}
