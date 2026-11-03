#include "command_trace_v347.h"
#include "r14_color_coop.h"
#include <math.h>
#include <string.h>
#define COOKIE 0x434c3234u
#define JOB_COOKIE 0x434a3234u
#define N 5120u
#define D 17u
#define CHUNK 1280u
#define TEMP 0u
#define GV 40960u
#define GP 409600u
#define COST 782848u /* only5120rounded centering means */
#define CS 823808u
#define MUQ 856576u
#define NORMS 1102336u
#define GATHER 1143296u
#define RESULT 1266176u
#define INDEX 1296896u
#define KEYS 1302016u
#define PACK 1322496u
static int live(const rcl_job *j){return j && j->cookie==JOB_COOKIE && j->owned;}
static double *fp64(rcl_job *j,uint32_t offset){return (double *)(j->arena+offset);}
static uint32_t *indices(rcl_job *j){return (uint32_t *)(j->arena+INDEX);}
static uint32_t *keys(rcl_job *j){return (uint32_t *)(j->arena+KEYS);}
static int clip(int x,int n){return x<0?0:(x>=n?n-1:x);}
/* The original nine clipped taps, for a valid y>=4 and x>=d pixel.
 * d==0 keeps the repeated left-edge taps. For d>0 only a contiguous
 * prefix of taps is masked. Bottom replication remains valid in ny.
 */
static void geometry_v195(unsigned d,unsigned x,unsigned y,unsigned *key,unsigned *nx,unsigned *ny){
 unsigned missing=d && x<d+4u?d+4u-x:0u;
 *key=511u&~((1u<<missing)-1u);*nx=9u-missing;*ny=y<8u?y+1u:9u;
}
int rcl_geometry_directed_v195(void){
 unsigned d,x,y;
 for(d=0;d<D;d++)for(y=4;y<64;y++)for(x=d;x<80;x++){
  unsigned key=0,nx=0,ny=0,actual_key,actual_nx,actual_ny;int t;
  for(t=-4;t<=4;t++){unsigned keep=(unsigned)clip((int)x+t,80)>=d;key|=keep<<(t+4);nx+=keep;ny+=(unsigned)clip((int)y+t,64)>=4u;}
  geometry_v195(d,x,y,&actual_key,&actual_nx,&actual_ny);
  if(key!=actual_key || nx!=actual_nx || ny!=actual_ny)return 1;
 }
 return 0;
}
static int overlap(const void *a,uint32_t na,const void *b,uint32_t nb){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<y+nb && y<x+na;
}
static int fail(rcl_job *j,int error){j->state=RCL_FAILED;j->error=(uint32_t)error;return error;}
static rc_buffer buffer(rcl_job *j,uint32_t offset,uint32_t bytes,unsigned write){
 rc_buffer b={j->arena+offset,j->address+offset,bytes,(uint8_t)write};return b;
}
static int field_begin(rcl_coop *c,rcl_job *j,const rf_program *p){
 rc_buffer in[40],out[8];unsigned i;
 memset(in,0,sizeof(in));memset(out,0,sizeof(out));
 for(i=0;i<p->inputs;i++)if(!p->input[i].scalar)in[i]=buffer(j,GATHER+i*CHUNK*8u,j->count*8u,0);
 for(i=0;i<p->outputs;i++)out[i]=buffer(j,RESULT+i*CHUNK*8u,j->count*(p->output[i].wide?8u:4u),1);
 return rf_begin(&c->fields,&j->field,j->frame,p,j->count,in,out);
}
static int field_finish(rcl_coop *c,rcl_job *j){
 int e=rf_step(&c->fields,&j->field);if(e!=RC_OK)return e;return rf_release(&c->fields,&j->field);
}
static int box_begin(rcl_coop *c,rcl_job *j,unsigned mode,uint32_t src,uint32_t dst){
 uint32_t out=(mode==3?5184u:N)*8u;
 uint32_t w[7]={mode|(80u<<8)|(64u<<16),0,j->address+src,j->address+dst,N*8u,out,c->cycle_limit};
 rc_buffer b[2]={buffer(j,src,N*8u,0),buffer(j,dst,out,1)};int e;
 if(c->box_active || c->device->engine[RC_BOX].state)return RC_AGAIN;
 /* Keep owner after partial submission failure until real drain/clear. */
 c->box_active=j;e=rc_begin(c->device,RC_BOX,w,7,b,2);
 if(e==RC_AGAIN)c->box_active=0;else if(e==RC_OK)j->tag=c->device->engine[RC_BOX].pending_tag;
 return e;
}
static int box_finish(rcl_coop *c,rcl_job *j){
 int e;
 if(c->box_active!=j || j->tag!=c->device->engine[RC_BOX].pending_tag)return RC_STALE;
 e=rc_poll(c->device,RC_BOX);if(e!=RC_OK)return e;
 c->box_active=0;c->box_jobs++;c->box_values+=j->state==RCL_PREFIX_WAIT?5184u:N;return RC_OK;
}
int rcl_init(rcl_coop *c,rc_device *d,uint32_t limit){
 unsigned i;
 if(!c || !d || !d->opened || !limit || d->engine[RC_FIELD].state || d->engine[RC_BOX].state)return RC_ARGUMENT;
 if(c->cookie==COOKIE)for(i=0;i<2;i++)if(live(c->registered[i]))return RC_AGAIN;
 memset(c,0,sizeof(*c));c->cookie=COOKIE;c->device=d;c->cycle_limit=limit;return rf_init(&c->fields,d,limit);
}
int rcl_begin(rcl_coop *c,rcl_job *j,unsigned frame,const float *logits,const float *guide,
              float *output,void *arena,uint32_t bytes){
 rcl_job next;unsigned i,k;const void *p[4]={logits,guide,output,arena};
 const uint32_t n[4]={D*N*4u,3u*N*4u,D*N*4u,RCL_ARENA_BYTES};
 if(!c || c->cookie!=COOKIE || !j || frame>1 || !logits || !guide || !output || !arena ||
    ((uintptr_t)arena&63u) || bytes<RCL_ARENA_BYTES)return RC_ARGUMENT;
 if(live(j) || live(c->registered[frame]))return RC_AGAIN;
 for(i=0;i<4;i++)if((uintptr_t)p[i]>UINTPTR_MAX-n[i]-63u)return RC_ARGUMENT;
 if(overlap(output,n[2],logits,n[0]) || overlap(output,n[2],guide,n[1]))return RC_LEASE_CONFLICT;
 for(i=0;i<3;i++)if(overlap(arena,n[3],p[i],n[i]))return RC_LEASE_CONFLICT;
 if(live(c->registered[frame^1u])){
  rcl_job *o=c->registered[frame^1u];const void *q[4]={o->logits,o->guide,o->output,o->arena};
  for(i=0;i<4;i++)for(k=0;k<4;k++)if((i>=2 || k>=2) && overlap(p[i],n[i],q[k],n[k]))return RC_LEASE_CONFLICT;
 }
 memset(&next,0,sizeof(next));next.cookie=JOB_COOKIE;next.owned=1;next.state=RCL_CENTER;next.frame=frame;
 next.logits=logits;next.guide=guide;next.output=output;next.arena=(uint8_t *)arena;next.address=frame?RCL_ARENA1:RCL_ARENA0;
 if((uint64_t)next.address+RCL_ARENA_BYTES>0x1a000000u)return RC_ARGUMENT;
 for(i=0;i<82;i++)next.inv[i]=1.0/(i?i:1u);
 *j=next;c->registered[frame]=j;return RC_OK;
}
static int rcl_body_v349(rcl_coop *c,rcl_job *j){
 uint32_t p,end;unsigned k,i;int e;
 if(!c || c->cookie!=COOKIE || !live(j))return RC_ARGUMENT;
 if(j->state==RCL_DONE)return RC_OK;
 if(j->state==RCL_FAILED || j->state==RCL_CANCELLED)return (int)j->error;
 if(j->state==RCL_CENTER){
  end=j->cursor+128u;if(end>N)end=N;
  for(p=j->cursor;p<end;p++){
   unsigned y=p/80u,x=p%80u,legal=0;double sum=0.0;
   for(k=0;k<3;k++)if(!isfinite(j->guide[k*N+p]))return fail(j,RC_ARGUMENT);
   for(k=0;k<D;k++){
    if(!isfinite(j->logits[k*N+p]))return fail(j,RC_ARGUMENT);
    if(y>=4u && x>=k){sum=sum+(double)j->logits[k*N+p];legal++;}
   }
   /* All legal channels use the same already-rounded FP64 mean. */
   double mean=sum*j->inv[legal];
   fp64(j,COST)[p]=mean;
   for(k=0;k<D;k++)j->output[k*N+p]=j->logits[k*N+p];
   keys(j)[p]=UINT32_MAX;
  }
  c->cpu_center_values+=(end-j->cursor)*D;j->cursor=end;
  if(end==N){j->cursor=0;j->plane=0;j->state=RCL_GUIDE_FIELDS;}return RC_PENDING;
 }
 if(j->state==RCL_GUIDE_FIELDS){
  static const unsigned a[6]={0,0,0,1,1,2},b[6]={0,1,2,1,2,2};
  end=j->cursor+CHUNK;if(end>N)end=N;
  for(p=j->cursor;p<end;p++){
   double v;unsigned keep=p/80u>=4u;
   if(j->plane<3)v=(double)j->guide[j->plane*N+p]*(double)keep;
   else v=((double)j->guide[a[j->plane-3]*N+p]*(double)j->guide[b[j->plane-3]*N+p])*(double)keep;
   fp64(j,TEMP)[p]=v;
  }
  c->cpu_guide_products+=end-j->cursor;j->cursor=end;
  if(end==N){j->cursor=0;j->state=RCL_VERTICAL_START;}return RC_PENDING;
 }
 if(j->state==RCL_VERTICAL_START || j->state==RCL_PREFIX_START || j->state==RCL_BOX_START){
  unsigned phase=j->state;
  if(phase==RCL_VERTICAL_START)e=box_begin(c,j,0,TEMP,GV+j->plane*N*8u);
  else if(phase==RCL_PREFIX_START)e=box_begin(c,j,3,GV+j->plane*N*8u,GP+j->plane*5184u*8u);
  else e=box_begin(c,j,2,TEMP,CS+j->plane*N*8u);
  if(e==RC_AGAIN)return RC_PENDING;
  if(e)return fail(j,e);
  j->state=phase==RCL_VERTICAL_START?RCL_VERTICAL_WAIT:(phase==RCL_PREFIX_START?RCL_PREFIX_WAIT:RCL_BOX_WAIT);
  return RC_PENDING;
 }
 if(j->state==RCL_VERTICAL_WAIT || j->state==RCL_PREFIX_WAIT || j->state==RCL_BOX_WAIT){
  unsigned phase=j->state;e=box_finish(c,j);
  if(e==RC_PENDING)return e;
  if(e)return fail(j,e);
  if(phase==RCL_VERTICAL_WAIT)j->state=RCL_PREFIX_START;
  else if(phase==RCL_PREFIX_WAIT){
   j->plane++;j->cursor=0;
   if(j->plane==9){j->plane=0;j->disparity=0;j->state=RCL_COV_GATHER;}
   else j->state=RCL_GUIDE_FIELDS;
  }else{
   j->plane++;j->cursor=0;
   if(j->plane==4){j->count=0;j->state=RCL_COV_GATHER;}
   else j->state=RCL_COV_GATHER;
  }
  return RC_PENDING;
 }
 if(j->state==RCL_COST_FIELDS){
  void *stream=j->arena+CS;unsigned before,after;int result;
  if(!j->stream_active){
   e=fs_color_logits_begin_v348(stream,32768u,j->logits+j->disparity*N,fp64(j,COST),j->guide,fp64(j,MUQ),j->disparity,(float*)(j->arena+RESULT),(float*)(j->arena+TEMP),&c->stream_cache);
   if(e)return fail(j,e);
   j->stream_active=1;
  }
  before=fs_color_rows_v346(stream);e=fs_color_producer_v346(stream);after=fs_color_rows_v346(stream);
  if(after>before){c->cpu_cost_products+=240u;if(c->stream_row)c->stream_row(c->stream_user,fs_color_last_row_v346(stream),before);}
  if(e!=RC_PENDING && e!=RC_AGAIN && e!=RC_OK)return fail(j,e);
  result=fs_color_take_v346(stream);if(result==RC_PENDING)return RC_PENDING;
  j->stream_active=0;if(result)return fail(j,result);
  j->cursor=N;j->count=60u*(80u-j->disparity);j->state=RCL_RHS_WAIT;
  if(c->stream_done && (e=c->stream_done(c->stream_user,j)))return fail(j,e);
  return RC_PENDING;
 }
 if(j->state==RCL_COV_GATHER){
  unsigned processed=0;
  while(j->cursor<N && j->count<CHUNK && processed++<128u){
   unsigned y,x,key,nx,ny,d=j->disparity;int lo,hi,left=0,right=0;
   double norm;
   p=j->cursor++;y=p/80u;x=p%80u;
   if(y<4u || x<d)continue;
   geometry_v195(d,x,y,&key,&nx,&ny);
   norm=j->inv[nx*ny];
   if(keys(j)[p]==key){
    if(fp64(j,NORMS)[p]!=norm)return fail(j,RC_ARGUMENT);
    c->affine_cache_hits++;continue;
   }
   fp64(j,NORMS)[p]=norm;
   lo=(int)x-4;hi=(int)x+4;if(lo<0)lo=0;if(lo<(int)d)lo=(int)d;if(hi>=80)hi=79;
   if(d==0 && x<4u)left=4-(int)x;
   if(x+4u>=80u)right=(int)x+4-79;
   for(k=0;k<9;k++){
    unsigned base=(k*64u+y)*81u;double v=lo<=hi?fp64(j,GP)[base+(unsigned)hi+1u]-fp64(j,GP)[base+(unsigned)lo]:fp64(j,GP)[base]-fp64(j,GP)[base];
    v=v+fp64(j,GV)[k*N+y*80u]*(double)left;
    v=v+fp64(j,GV)[k*N+y*80u+79u]*(double)right;
    v=v*norm;fp64(j,GATHER)[k*CHUNK+j->count]=v;
    if(k<3)fp64(j,MUQ)[k*N+p]=v;
   }
   for(k=0;k<3;k++)fp64(j,GATHER)[(k+9u)*CHUNK+j->count]=(double)j->guide[k*N+p];
   indices(j)[j->count++]=p;keys(j)[p]=key;
  }
  if(j->count==CHUNK || j->cursor==N){
   if(j->count)j->state=RCL_COV_START;
   else{j->cursor=0;j->state=RCL_COST_FIELDS;}
  }
  return RC_PENDING;
 }
 if(j->state==RCL_COV_START){
  unsigned phase=j->state;
  e=field_begin(c,j,phase==RCL_COV_START?&r14_program_color_covariance:&r14_program_color_rhs);
  if(e==RC_AGAIN)return RC_PENDING;
  if(e)return fail(j,e);
  j->state=phase==RCL_COV_START?RCL_COV_WAIT:RCL_RHS_WAIT;return RC_PENDING;
 }
 if(j->state==RCL_COV_WAIT){
  unsigned phase=j->state;e=field_finish(c,j);
  if(e==RC_PENDING || e==RC_AGAIN)return RC_PENDING;
  if(e)return fail(j,e);
  for(i=0;i<j->count;i++){
   p=indices(j)[i];
   if(phase==RCL_COV_WAIT){
    double mu[3],q[3],w[4],norm=fp64(j,NORMS)[p];
    /* Read every mean before reusing its plane for the cached weights. */
    for(k=0;k<3;k++){mu[k]=fp64(j,MUQ)[k*N+p];q[k]=fp64(j,RESULT)[k*CHUNK+i];}
    w[0]=1.0-q[0]*mu[0];w[0]=w[0]-q[1]*mu[1];w[0]=w[0]-q[2]*mu[2];w[0]=w[0]*norm;
    for(k=0;k<3;k++)w[k+1]=q[k]*norm;
    for(k=0;k<4;k++){if(!isfinite(w[k]))return fail(j,RC_ARGUMENT);fp64(j,MUQ)[k*N+p]=w[k];}
    c->affine_weights++;c->cpu_affine_ops+=10u;
   }
   else j->output[j->disparity*N+p]=((float *)(j->arena+RESULT))[i];
  }
  if(phase==RCL_COV_WAIT){c->covariance_values+=j->count;j->count=0;
   if(j->cursor==N){j->cursor=0;j->state=RCL_COST_FIELDS;}else j->state=RCL_COV_GATHER;
  }else{
   c->rhs_values+=j->count;j->count=0;
   if(j->cursor==N){
    j->disparity++;j->plane=0;j->cursor=0;
    if(j->disparity==D){j->state=RCL_DONE;c->frames++;return RC_OK;}
    j->state=RCL_COV_GATHER;
   }else j->state=RCL_RHS_GATHER;
  }
  return RC_PENDING;
 }
 if(j->state==RCL_RHS_WAIT){
  /* Stream terminal receipt has already committed TEMP. */
  i=0;for(p=0;p<N;p++)if(p/80u>=4u && p%80u>=j->disparity)
   j->output[j->disparity*N+p]=((float *)(j->arena+TEMP))[i++];
  if(i!=j->count)return fail(j,RC_ARGUMENT);
  c->rhs_values+=j->count;j->count=0;j->cursor=0;j->disparity++;
  if(j->disparity==D){j->state=RCL_DONE;c->frames++;return RC_OK;}
  j->state=RCL_COV_GATHER;return RC_PENDING;
 }
 return fail(j,RC_ARGUMENT);
}
int rcl_cancel(rcl_coop *c,rcl_job *j){
 int e;
 if(!c || c->cookie!=COOKIE || !live(j) || j->state>=RCL_DONE)return RC_ARGUMENT;
 if(j->stream_active)fs_color_cancel_v346(j->arena+CS);
 if(j->field.owned && j->field.state<RF_DONE){e=rf_cancel(&c->fields,&j->field);if(e!=RC_CANCELLED)return fail(j,e);}
 if(c->box_active==j && c->device->engine[RC_BOX].state==RC_JOB){e=rc_cancel(c->device,RC_BOX);if(e!=RC_CANCELLED)return fail(j,e);}
 j->state=RCL_CANCELLED;j->error=RC_CANCELLED;return RC_CANCELLED;
}
int rcl_recover(rcl_coop *c){
 int a,b=RC_OK;rc_engine *e;
 if(!c || c->cookie!=COOKIE)return RC_ARGUMENT;
 a=rf_recover(&c->fields);if(a!=RC_OK && a!=RC_PENDING)return a;
 e=c->device->engine+RC_BOX;
 if(c->box_active && c->box_active->state<RCL_DONE)return RC_AGAIN;
 if(e->state==RC_QUARANTINE){b=rc_recover_begin(c->device,RC_BOX);if(b==RC_OK)b=RC_PENDING;}
 else if(e->state==RC_CLEAR)b=rc_recover_poll(c->device,RC_BOX);else if(e->state)b=RC_AGAIN;
 if(b!=RC_OK && b!=RC_PENDING)return b;
 return a==RC_PENDING || b==RC_PENDING?RC_PENDING:RC_OK;
}
int rcl_release(rcl_coop *c,rcl_job *j){
 unsigned i;int e;
 if(!c || c->cookie!=COOKIE || !live(j) || j->state<RCL_DONE)return RC_ARGUMENT;
 if(j->stream_active){e=fs_color_take_v346(j->arena+CS);if(e==RC_PENDING)return RC_AGAIN;j->stream_active=0;}
 if(j->field.owned){e=rf_release(&c->fields,&j->field);if(e)return e;}
 if(c->box_active==j){if(c->device->engine[RC_BOX].state)return RC_AGAIN;c->box_active=0;}
 for(i=0;i<2;i++)if(c->registered[i]==j)c->registered[i]=0;
 j->owned=0;return RC_OK;
}

int rcl_consumer_step_v346(rcl_coop *c,rcl_job *j){
 if(!c || !live(j))return RC_ARGUMENT;
 return j->stream_active?fs_color_consumer_v346(j->arena+CS):RC_PENDING;
}

int rcl_step(rcl_coop *c,rcl_job *j){
 unsigned phase=j?j->state:0,cursor=j?j->cursor:0,count=j?j->count:0;
 unsigned before=j && j->stream_active?fs_color_rows_v346(j->arena+RCL_STREAM_OFFSET_V349):0;
 int result=rcl_body_v349(c,j);
 if(j && (phase!=j->state || cursor!=j->cursor || count!=j->count))trace_cpu_phase_v347("rcl",j->frame,phase,j->state,cursor,j->cursor,count,j->count,j->disparity,result);
 if(j && j->stream_active){unsigned after=fs_color_rows_v346(j->arena+RCL_STREAM_OFFSET_V349);if(after!=before)trace_cpu_phase_v347("rcl_stream_producer",j->frame,phase,j->state,before,after,count,j->count,j->disparity,result);}
 return result;
}
