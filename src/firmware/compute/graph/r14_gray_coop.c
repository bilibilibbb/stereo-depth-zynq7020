#include "command_trace_v347.h"
#include "r14_gray_coop.h"
#include <math.h>
#include <string.h>
#define COOKIE 0x47433234u
#define JOB_COOKIE 0x474a3234u
#define PN 76800u
#define LN 4800u
#define CHUNK 1280u
#define GUIDE 0u
#define FIELDS 614400u
#define MEANS 768000u
#define COEFF 921600u
#define A 1036800u
#define B 1075200u
#define GATHER 1113600u
#define OUTPUT 1245184u
static rf_coop *field_service(rg_coop *c){return c->field_service?c->field_service:&c->fields;}
static int live(const rg_job *j){return j && j->cookie==JOB_COOKIE && j->owned;}
static double *doubles(rg_job *j,uint32_t offset){return (double *)(j->arena+offset);}
static int overlap(const void *a,uint32_t na,const void *b,uint32_t nb){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<y+nb && y<x+na;
}
static int fail(rg_job *j,int error){j->state=RG_FAILED;j->error=(uint32_t)error;return error;}
static rc_buffer buffer(rg_job *j,uint32_t offset,uint32_t bytes,unsigned write){
 rc_buffer b={j->arena+offset,j->address+offset,bytes,(uint8_t)write};return b;
}
static int field_start(rg_coop *c,rg_job *j,const rf_program *p,uint32_t count,const uint32_t *dest){
 rc_buffer in[40],out[8];unsigned i;memset(in,0,sizeof(in));memset(out,0,sizeof(out));
 for(i=0;i<p->inputs;i++)if(!p->input[i].scalar)in[i]=buffer(j,MEANS+i*LN*8u,count*8u,0);
 for(i=0;i<p->outputs;i++)out[i]=buffer(j,dest[i],count*(p->output[i].wide?8u:4u),1);
 return rf_begin(field_service(c),&j->field,j->frame,p,count,in,out);
}
static int field_finish(rg_coop *c,rg_job *j){
 int error=rf_step(field_service(c),&j->field);if(error!=RC_OK)return error;return rf_release(field_service(c),&j->field);
}
int rg_init(rg_coop *c,rc_device *d,uint32_t limit){
 unsigned i;int error;
 if(!c || !d || !d->opened || !limit || d->engine[RC_FIELD].state || d->engine[RC_BOX].state)return RC_ARGUMENT;
 if(c->cookie==COOKIE)for(i=0;i<2;i++)if(live(c->registered[i]))return RC_AGAIN;
 memset(c,0,sizeof(*c));c->cookie=COOKIE;c->device=d;c->cycle_limit=limit;
 error=rf_init(&c->fields,d,limit);return error;
}
int rg_begin(rg_coop *c,rg_job *j,unsigned frame,const uint8_t *rgb,const float *disparity,float *output,void *arena,uint32_t bytes){
 rg_job next;unsigned i;const void *p[4];uint32_t n[4]={3u*PN,4u*PN,4u*PN,RG_ARENA_BYTES};
 if(!c || c->cookie!=COOKIE || !j || frame>1 || !rgb || !disparity || !output || !arena ||
    ((uintptr_t)arena&63u) || bytes<RG_ARENA_BYTES)return RC_ARGUMENT;
 if(live(j) || live(c->registered[frame]))return RC_AGAIN;
 p[0]=rgb;p[1]=disparity;p[2]=output;p[3]=arena;
 for(i=0;i<4;i++)if((uintptr_t)p[i]>UINTPTR_MAX-n[i]-63u)return RC_ARGUMENT;
 if(overlap(output,4u*PN,rgb,3u*PN) || overlap(output,4u*PN,disparity,4u*PN))return RC_LEASE_CONFLICT;
 for(i=0;i<3;i++)if(overlap(arena,RG_ARENA_BYTES,p[i],n[i]))return RC_LEASE_CONFLICT;
 if(live(c->registered[frame^1u])){
  const rg_job *other=c->registered[frame^1u];const void *q[4]={other->rgb,other->disparity,other->output,other->arena};unsigned k;
  for(i=0;i<4;i++)for(k=0;k<4;k++)if((i>=2 || k>=2) && overlap(p[i],n[i],q[k],n[k]))return RC_LEASE_CONFLICT;
 }
 memset(&next,0,sizeof(next));next.cookie=JOB_COOKIE;next.owned=1;next.state=RG_GUIDE;next.frame=frame;
 next.rgb=rgb;next.disparity=disparity;next.output=output;next.arena=(uint8_t *)arena;next.address=frame?RG_ARENA1:RG_ARENA0;
 if((uint64_t)next.address+RG_ARENA_BYTES>0x1a000000u)return RC_ARGUMENT;
 *j=next;c->registered[frame]=j;return RC_OK;
}
static int rg_body_v349(rg_coop *c,rg_job *j){
 uint32_t at,end;unsigned i;int error;
 if(!c || c->cookie!=COOKIE || !live(j))return RC_ARGUMENT;
 if(j->state==RG_DONE)return RC_OK;
 if(j->state==RG_FAILED || j->state==RG_CANCELLED)return (int)j->error;
 if(j->state==RG_GUIDE){
  double *guide=doubles(j,GUIDE);end=j->cursor+CHUNK;if(end>PN)end=PN;
  for(at=j->cursor;at<end;at++){
   if(!isfinite(j->disparity[at]))return fail(j,RC_ARGUMENT);
   guide[at]=(double)((77*(int)j->rgb[at]+150*(int)j->rgb[PN+at]+29*(int)j->rgb[2*PN+at]+128)/256)*(1.0/255.0);
  }
  c->cpu_guide_values+=end-j->cursor;j->cursor=end;
  if(end==PN){j->cursor=0;j->state=RG_FIELDS_GATHER;}return RC_PENDING;
 }
 if(j->state==RG_FIELDS_GATHER){
  end=j->cursor+128u;if(end>LN)end=LN;
  for(at=j->cursor;at<end;at++){
   double guide_sum=0.0,median_sum=0.0,g,m,product,square;
   for(i=0;i<16;i++){
    uint32_t native=(4u*(at/80u)+i/4u)*320u+4u*(at%80u)+i%4u;double v;
    guide_sum=guide_sum+doubles(j,GUIDE)[native];if(!isfinite(guide_sum))return fail(j,RC_ARGUMENT);
    v=(double)j->disparity[native];if(!isfinite(v))return fail(j,RC_ARGUMENT);
    median_sum=median_sum+v;if(!isfinite(median_sum))return fail(j,RC_ARGUMENT);
   }
   g=guide_sum/16.0;m=median_sum/16.0;product=g*m;square=g*g;
   if(!isfinite(g) || !isfinite(m) || !isfinite(product) || !isfinite(square))return fail(j,RC_ARGUMENT);
   doubles(j,FIELDS)[at]=m;doubles(j,FIELDS+LN*8u)[at]=g;
   doubles(j,FIELDS+LN*16u)[at]=product;doubles(j,FIELDS+LN*24u)[at]=square;
  }
  j->cursor=end;if(end==LN){j->cursor=0;j->state=RG_FIELDS_WAIT;
   if(c->stream_fields_done && (error=c->stream_fields_done(c->stream_user,j)))return fail(j,error);}
  return RC_PENDING;
 }
 if(j->state==RG_FIELDS_WAIT || j->state==RG_COEFF_WAIT || j->state==RG_TAIL_WAIT){
  uint32_t phase=j->state;if(phase==RG_COEFF_WAIT){error=field_finish(c,j);if(error==RC_PENDING || error==RC_AGAIN){return RC_PENDING;}if(error){return fail(j,error);}}
  if(phase==RG_FIELDS_WAIT){j->box_index=0;j->state=RG_BOX_START;}
  else if(phase==RG_COEFF_WAIT){j->box_index=4;j->state=RG_BOX_START;}
  else{
   memcpy(j->output+j->cursor,j->arena+OUTPUT+j->cursor*4u,j->tail_count*4u);j->cursor+=j->tail_count;
   if(j->cursor==PN){j->state=RG_DONE;c->frames++;return RC_OK;}j->state=RG_TAIL_GATHER;
  }
  return RC_PENDING;
 }
 if(j->state==RG_BOX_START){
  rc_buffer b[2];uint32_t w[7]={2u|(80u<<8)|(60u<<16),0,0,0,LN*8u,LN*8u,c->cycle_limit};
  if(c->box_active || c->device->engine[RC_BOX].state)return RC_PENDING;
  b[0]=buffer(j,j->box_index<4?FIELDS+j->box_index*LN*8u:(j->box_index==4?A:B),LN*8u,0);
  b[1]=buffer(j,j->box_index<4?MEANS+j->box_index*LN*8u:COEFF+(j->box_index-4u)*LN*8u,LN*8u,1);
  w[2]=b[0].address;w[3]=b[1].address;
  /* rc_begin may lease buffers and then fail an MMIO write. Retain the owner
   * even in that path, until its physical drain and clear are confirmed. */
  c->box_active=j;error=rc_begin(c->device,RC_BOX,w,7,b,2);
  if(error==RC_AGAIN){c->box_active=0;return RC_PENDING;}if(error){return fail(j,error);}
  j->tag=c->device->engine[RC_BOX].pending_tag;j->state=RG_BOX_WAIT;return RC_PENDING;
 }
 if(j->state==RG_BOX_WAIT){
  if(c->box_active!=j || j->tag!=c->device->engine[RC_BOX].pending_tag)return fail(j,RC_STALE);
  error=rc_poll(c->device,RC_BOX);if(error==RC_PENDING){return error;}if(error){return fail(j,error);}
  c->box_active=0;c->box_jobs++;c->box_values+=LN;j->box_index++;
  if(j->box_index==4){j->cursor=0;j->state=RG_COEFF_START;}
  else if(j->box_index==6){j->cursor=0;j->state=RG_NORMALIZE;}else j->state=RG_BOX_START;
  return RC_PENDING;
 }
 if(j->state==RG_COEFF_START){
  const rf_program *p=&r14_program_gray_coeff;uint32_t dest[3]={A,B,COEFF+LN*16u};
  /* Only a native4x4800 field copy; the solve consumes actual BOX output. */
  /* The coefficient solve reads its immutable MEANS planes directly. */
  error=field_start(c,j,p,LN,dest);if(error==RC_AGAIN){return RC_PENDING;}if(error){return fail(j,error);}
  j->state=RG_COEFF_WAIT;return RC_PENDING;
 }
 if(j->state==RG_NORMALIZE){
  void *stream=j->arena+GATHER+115200u;int result;
  if(!j->stream_active){
   error=fs_gray_begin_v346(stream,16384u,doubles(j,COEFF),(int64_t*)(j->arena+GATHER),doubles(j,GUIDE),j->disparity,(float*)(j->arena+OUTPUT),&c->stream_cache);
   if(error)return fail(j,error);
   j->stream_active=1;
  }
  error=fs_gray_producer_v346(stream);if(error!=RC_PENDING && error!=RC_OK)return fail(j,error);
  result=fs_gray_take_v346(stream);if(result==RC_PENDING)return RC_PENDING;
  j->stream_active=0;if(result)return fail(j,result);
  j->cursor=0;j->tail_count=PN;j->state=RG_TAIL_WAIT;
  if(c->stream_done && (error=c->stream_done(c->stream_user,j)))return fail(j,error);
  return RC_PENDING;
 }
 return fail(j,RC_ARGUMENT);
}
int rg_cancel(rg_coop *c,rg_job *j){
 int error;
 if(!c || c->cookie!=COOKIE || !live(j) || j->state>=RG_DONE)return RC_ARGUMENT;
 if(j->stream_active)fs_gray_cancel_v346(j->arena+GATHER+115200u);
 if(j->field.owned && j->field.state<RF_DONE){error=rf_cancel(field_service(c),&j->field);if(error!=RC_CANCELLED)return fail(j,error);}
 if(c->box_active==j && c->device->engine[RC_BOX].state==RC_JOB){error=rc_cancel(c->device,RC_BOX);if(error!=RC_CANCELLED)return fail(j,error);}
 j->state=RG_CANCELLED;j->error=RC_CANCELLED;return RC_CANCELLED;
}
int rg_recover(rg_coop *c){
 int a,b=RC_OK;rc_engine *e;
 if(!c || c->cookie!=COOKIE)return RC_ARGUMENT;
 a=rf_recover(field_service(c));if(a!=RC_OK && a!=RC_PENDING)return a;
 e=c->device->engine+RC_BOX;
 if(c->box_active && c->box_active->state<RG_DONE)return RC_AGAIN;
 if(e->state==RC_QUARANTINE){b=rc_recover_begin(c->device,RC_BOX);if(b==RC_OK)b=RC_PENDING;}
 else if(e->state==RC_CLEAR)b=rc_recover_poll(c->device,RC_BOX);else if(e->state)b=RC_AGAIN;
 if(b!=RC_OK && b!=RC_PENDING){return b;}return a==RC_PENDING || b==RC_PENDING?RC_PENDING:RC_OK;
}
int rg_release(rg_coop *c,rg_job *j){
 unsigned i;int error;
 if(!c || c->cookie!=COOKIE || !live(j) || j->state<RG_DONE)return RC_ARGUMENT;
 if(j->stream_active){error=fs_gray_take_v346(j->arena+GATHER+115200u);if(error==RC_PENDING)return RC_AGAIN;j->stream_active=0;}
 if(j->field.owned){error=rf_release(field_service(c),&j->field);if(error)return error;}
 if(c->box_active==j){if(c->device->engine[RC_BOX].state)return RC_AGAIN;c->box_active=0;}
 for(i=0;i<2;i++)if(c->registered[i]==j)c->registered[i]=0;
 j->owned=0;return RC_OK;
}

int rg_consumer_step_v346(rg_coop *c,rg_job *j){
 if(!c || !live(j))return RC_ARGUMENT;
 return j->stream_active?fs_gray_consumer_v346(j->arena+GATHER+115200u):RC_PENDING;
}

int rg_step(rg_coop *c,rg_job *j){
 unsigned phase=j?j->state:0,cursor=j?j->cursor:0,count=j?j->tail_count:0;
 unsigned before=j && j->stream_active?fs_gray_prepared_v346(j->arena+RG_STREAM_OFFSET_V349):0;
 int result=rg_body_v349(c,j);
 if(j && (phase!=j->state || cursor!=j->cursor || count!=j->tail_count))trace_cpu_phase_v347("rg",j->frame,phase,j->state,cursor,j->cursor,count,j->tail_count,j->box_index,result);
 if(j && j->stream_active){unsigned after=fs_gray_prepared_v346(j->arena+RG_STREAM_OFFSET_V349);if(after!=before)trace_cpu_phase_v347("rg_stream_producer",j->frame,phase,j->state,before,after,count,j->tail_count,j->box_index,result);}
 return result;
}
