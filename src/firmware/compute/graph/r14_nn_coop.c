#include "r14_nn_coop.h"
#include <string.h>
#define COOP_COOKIE 0x434e3234u
static int common_result(int rc){
 switch(rc){case RC_OK:return RN_OK;case RC_PENDING:case RC_AGAIN:return RN_PENDING;
 case RC_ARGUMENT:case RC_LEASE_CONFLICT:return RN_ARGUMENT;case RC_IO_ERROR:return RN_IO_ERROR;
 case RC_IDENTITY:return RN_IDENTITY;case RC_HARDWARE:return RN_HARDWARE;case RC_STALE:return RN_STALE;
 case RC_TIMEOUT:return RN_TIMEOUT;case RC_CANCELLED:return RN_CANCELLED_ERROR;
 case RC_RESET_REQUIRED:return RN_RESET_REQUIRED;default:return RN_HARDWARE;}
}

static int overlap(uintptr_t a,size_t n,uintptr_t b,size_t m){
 return n && m && (a<=b?b-a<n:a-b<m);
}
int r14_dual_layout_valid(void){
 size_t i,j;for(i=0;i<sizeof(r14_dual_regions)/sizeof(r14_dual_regions[0]);i++){
  const r14_region *a=r14_dual_regions+i;
  if(!a->bytes || (a->address&63u) || (a->bytes&63u) || a->address<R14_DUAL_DMA_BEGIN || (uint64_t)a->address+a->bytes>R14_DUAL_DMA_END)return 0;
  for(j=0;j<i;j++)if(overlap(a->address,a->bytes,r14_dual_regions[j].address,r14_dual_regions[j].bytes))return 0;
 }return 1;
}
int r14_nn_coop_init(r14_nn_coop *c,r14_shared_parameters *nn,void *a,void *b,uint32_t bytes){
 int i;
 if(c && c->cookie==COOP_COOKIE)for(i=0;i<2;i++)if(c->registered[i] &&
   (c->registered[i]->state==R14_LAYER_READY || c->registered[i]->state==R14_LAYER_WAIT))return RN_BUSY_ERROR;
 if(!c || !nn || !nn->device || !nn->device->opened || nn->device->engine[RC_NN].state || !a || !b || ((uintptr_t)a&63u) || ((uintptr_t)b&63u) ||
    bytes!=R14_DUAL_TENSOR_BYTES || bytes>UINTPTR_MAX-(uintptr_t)a || bytes>UINTPTR_MAX-(uintptr_t)b ||
    overlap((uintptr_t)a,bytes,(uintptr_t)b,bytes) || !r14_dual_layout_valid() ||
    !nn->packed_weights || ((uintptr_t)nn->packed_weights&63u) || !nn->cycle_limit ||
    nn->weight_address!=R14_DUAL_WEIGHTS || nn->weight_capacity!=R14_DUAL_WEIGHT_BYTES ||
    overlap((uintptr_t)a,bytes,(uintptr_t)nn->packed_weights,nn->weight_capacity) ||
    overlap((uintptr_t)b,bytes,(uintptr_t)nn->packed_weights,nn->weight_capacity))return RN_ARGUMENT;
 memset(c,0,sizeof(*c));c->cookie=COOP_COOKIE;c->neural=nn;c->preferred_frame=-1;c->memory[0]=(uint8_t*)a;c->memory[1]=(uint8_t*)b;
 c->address[0]=R14_DUAL_TENSOR0;c->address[1]=R14_DUAL_TENSOR1;c->bytes=bytes;
 for(i=0;i<64;i++)nn->cached_parameter[i]=0;
 nn->weight_used=0;nn->layer_calls=0;nn->dma_jobs=0;return RN_OK;
}
static int location(const r14_nn_coop *c,uint32_t frame,const void *p,uint32_t bytes,uint32_t *address){
 uintptr_t base,point=(uintptr_t)p,at;
 if(frame>1 || !p || (point&63u) || !bytes)return 0;
 base=(uintptr_t)c->memory[frame];if(point<base)return 0;
 at=point-base;if(at>c->bytes || bytes>c->bytes-at)return 0;
 *address=c->address[frame]+(uint32_t)at;return 1;
}
static int valid(const rn_parameter *p,int id,int ih,int iw,int oh,int ow,int wide,int fraction,int relu){
 return p && p->weights && p->bias && id>=0 && id<64 && ih>=2 && ih<=256 && iw>=4 && iw<=320 && !(ih&1) && !(iw&3) &&
  oh>=2 && oh<=256 && ow>=4 && ow<=320 && p->ci>0 && p->ci<=32 && p->co>0 && p->co<=32 &&
  p->right_shift>=0 && p->right_shift<=63 && (p->k==1 || p->k==3) && (p->stride==1 || p->stride==2) &&
  oh==ih/p->stride && ow==iw/p->stride && (p->groups==1 || (p->groups==p->ci && p->ci==p->co)) &&
  ((p->groups==1 && p->dilation==1 && p->ci*p->k*p->k<=32) ||
   (p->groups!=1 && p->k==3 && (p->dilation==1 || p->dilation==2 || p->dilation==4) && (p->stride==1 || p->dilation==1))) &&
  wide>=0 && wide<=1 && fraction>=0 && fraction<=30 && relu>=0 && relu<=1 && (!relu || (fraction>=9 && fraction<=16)) &&
  p->input_frac>=0 && p->input_frac<=30;
}
int r14_nn_coop_begin(r14_nn_coop *c,r14_layer_job *job,uint32_t frame,int id,const rn_parameter *p,
 const void *source,int ih,int iw,int16_t *destination,int oh,int ow,int wide,int fraction,int relu){
 uint32_t ia,oa,ib,ob;
 if(!c || c->cookie!=COOP_COOKIE || !c->neural || !job || frame>1 || !valid(p,id,ih,iw,oh,ow,wide,fraction,relu))return RN_ARGUMENT;
 if(c->registered[frame] && (c->registered[frame]->state==R14_LAYER_READY || c->registered[frame]->state==R14_LAYER_WAIT))return RN_BUSY_ERROR;
 if(job->state==R14_LAYER_READY || job->state==R14_LAYER_WAIT)return RN_BUSY_ERROR;
 if(c->active==job || c->neural->device->engine[RC_NN].state==RC_QUARANTINE || c->neural->device->engine[RC_NN].state==RC_CLEAR)return RN_BUSY_ERROR;
 ib=(uint32_t)(p->ci*ih*iw*(wide?4:2));ob=(uint32_t)(p->co*oh*ow*2);
 if(!location(c,frame,source,ib,&ia) || !location(c,frame,destination,ob,&oa) || overlap(ia,ib,oa,ob))return RN_ARGUMENT;
 memset(job,0,sizeof(*job));job->state=R14_LAYER_READY;job->frame=frame;job->id=id;job->parameter=p;
 job->source=source;job->destination=destination;job->ih=ih;job->iw=iw;job->oh=oh;job->ow=ow;
 job->wide=wide;job->fraction=fraction;job->relu=relu;job->source_address=ia;job->destination_address=oa;
 job->source_bytes=ib;job->destination_bytes=ob;c->registered[frame]=job;return RN_OK;
}
static int pack_weights(r14_shared_parameters *c,int id,const rn_parameter *p,uint32_t *offset){
 uint32_t needed;int base,k,lane,terms=p->ci*p->k*p->k;int16_t *packed;
 if(c->cached_parameter[id]){if(c->cached_parameter[id]!=p)return RN_IDENTITY;*offset=c->parameter_offset[id];return RN_OK;}
 needed=(uint32_t)((p->co+15)/16*terms*32);
 if((uint64_t)c->weight_used+needed>c->weight_capacity)return RN_ARGUMENT;
 *offset=c->weight_used;packed=(int16_t*)((uint8_t*)c->packed_weights+*offset);
 for(base=0;base<p->co;base+=16)for(k=0;k<terms;k++)for(lane=0;lane<16;lane++){
  int channel=base+lane;packed[((base/16)*terms+k)*16+lane]=channel<p->co?p->weights[channel*terms+k]:0;
 }
 c->cached_parameter[id]=p;c->parameter_offset[id]=*offset;c->weight_used=(*offset+needed+63u)&~63u;return RN_OK;
}
static void bits(rn_descriptor *d,uint32_t word,uint32_t bit,uint64_t value,uint32_t count){
 uint32_t i;for(i=0;i<count;i++)if(value&((uint64_t)1<<i))d->word[word+(bit+i)/32u]|=1u<<((bit+i)%32u);
}
static int submit(r14_nn_coop *c,r14_layer_job *job){
 r14_shared_parameters *nn=c->neural;const rn_parameter *p=job->parameter;rn_descriptor d;
 uint32_t *w=d.word,channel=job->channel,weight_at=0;int co=p->groups==1?(p->co-(int)channel<16?p->co-(int)channel:16):(p->co-(int)channel<4?p->co-(int)channel:4),lane,rc;
 const void *source=job->source,*weights=0;void *destination=job->destination;
 if(!channel && p->groups==1){rc=pack_weights(nn,job->id,p,&job->weight_offset);if(rc)return rc;}
 memset(&d,0,sizeof(d));
 w[RN_D_SOURCE_ADDRESS]=job->source_address;w[RN_D_SOURCE_BYTES]=job->source_bytes;
 w[RN_D_SOURCE_FRACTION]=(uint32_t)job->fraction|(job->wide?RN_SOURCE_WIDE:0u)|(job->relu?RN_SOURCE_RELU6:0u);
 w[RN_D_TARGET_FRACTION]=(uint32_t)p->input_frac;
 w[RN_D_DESTINATION_ADDRESS]=job->destination_address;w[RN_D_DESTINATION_BYTES]=job->destination_bytes;
 w[RN_D_TIMEOUT_CYCLES]=nn->cycle_limit;w[RN_D_INPUT_WIDTH]=(uint32_t)job->iw;w[RN_D_INPUT_HEIGHT]=(uint32_t)job->ih;
 w[RN_D_INPUT_CHANNELS]=(uint32_t)p->ci;w[RN_D_OUTPUT_WIDTH]=(uint32_t)job->ow;w[RN_D_OUTPUT_HEIGHT]=(uint32_t)job->oh;w[RN_D_OUTPUT_CHANNELS]=(uint32_t)co;
 w[RN_D_KERNEL_WIDTH]=(uint32_t)p->k;w[RN_D_KERNEL_HEIGHT]=(uint32_t)p->k;w[RN_D_STRIDE2]=(uint32_t)(p->stride==2);
 w[RN_D_DESTINATION_WIDTH]=(uint32_t)job->ow;w[RN_D_DESTINATION_HEIGHT]=(uint32_t)job->oh;w[RN_D_DESTINATION_CHANNELS]=(uint32_t)p->co;
 w[RN_D_PADDING_X]=(uint32_t)(p->dilation*(p->k-1)/2);w[RN_D_PADDING_Y]=w[RN_D_PADDING_X];
 for(lane=0;lane<co;lane++){
  bits(&d,RN_D_BIAS_START,(uint32_t)lane*48u,(uint64_t)p->bias[channel+(uint32_t)lane],48);
  bits(&d,RN_D_SHIFT_START,(uint32_t)lane*6u,(uint64_t)p->right_shift,6);
 }
 if(p->groups==1){
  uint32_t page_bytes=(uint32_t)(p->ci*p->k*p->k*32);weight_at=job->weight_offset+(channel/16)*page_bytes;
  w[RN_D_CHANNEL_START]=channel;w[RN_D_WEIGHT_ADDRESS]=nn->weight_address+weight_at;w[RN_D_WEIGHT_BYTES]=page_bytes;
  weights=(uint8_t*)nn->packed_weights+weight_at;
 }else{
  uint32_t is=(uint32_t)(job->ih*job->iw*(job->wide?4:2)),os=(uint32_t)(job->oh*job->ow*2);
  w[RN_D_CMD_DEPTHWISE]=1;w[RN_D_DILATION_SHIFT]=(uint32_t)(p->dilation==4?2:(p->dilation==2?1:0));
  w[RN_D_SOURCE_ADDRESS]+=channel*is;w[RN_D_SOURCE_BYTES]=(uint32_t)co*is;w[RN_D_INPUT_CHANNELS]=(uint32_t)co;
  w[RN_D_DESTINATION_ADDRESS]+=channel*os;w[RN_D_DESTINATION_BYTES]=(uint32_t)co*os;w[RN_D_DESTINATION_CHANNELS]=(uint32_t)co;
  for(lane=0;lane<co*9;lane++){
   uint32_t base=lane<9?RN_D_DW_WEIGHTS_START:RN_D_DW_EXTRA_WEIGHTS_START;
   uint32_t bit=(uint32_t)(lane<9?lane:lane-9)*16u;
   bits(&d,base,bit,(uint16_t)p->weights[channel*9u+(uint32_t)lane],16);
  }
  source=(const uint8_t*)job->source+channel*is;destination=(uint8_t*)job->destination+channel*os;
 }
 {
  rc_buffer b[3];unsigned nbuf=0;
  b[nbuf].pointer=(void*)source;b[nbuf].address=w[1];b[nbuf].bytes=w[2];b[nbuf++].write=0;
  if(weights){b[nbuf].pointer=(void*)weights;b[nbuf].address=w[5];b[nbuf].bytes=w[6];b[nbuf++].write=0;}
  b[nbuf].pointer=destination;b[nbuf].address=w[3];b[nbuf].bytes=w[4];b[nbuf++].write=1;
  rc=common_result(rc_begin(nn->device,RC_NN,w,RN_DESCRIPTOR_WORDS,b,nbuf));
 }
 if(rc)return rc;
 job->tag=nn->device->engine[RC_NN].pending_tag;job->state=R14_LAYER_WAIT;c->active=job;return RN_PENDING;
}
static int fail_job(r14_layer_job *job,int rc){job->state=R14_LAYER_FAILED;job->error=(uint32_t)rc;return rc;}
int r14_nn_coop_step(r14_nn_coop *c,r14_layer_job *job){
 r14_shared_parameters *nn;int rc;
 if(!c || !c->neural || !job)return RN_ARGUMENT;
 nn=c->neural;
 if(job->state==R14_LAYER_DONE)return RN_OK;
 if(job->state==R14_LAYER_FAILED || job->state==R14_LAYER_CANCELLED)return job->error?(int)job->error:RN_HARDWARE;
 if(job->state==R14_LAYER_READY){
  if(nn->device->engine[RC_NN].state==RC_QUARANTINE || nn->device->engine[RC_NN].state==RC_CLEAR)return fail_job(job,nn->device->engine[RC_NN].first_error?common_result((int)nn->device->engine[RC_NN].first_error):RN_HARDWARE);
  if(c->active || nn->device->engine[RC_NN].state)return RN_PENDING;
  if(c->preferred_frame>=0 && job->frame!=(uint32_t)c->preferred_frame)return RN_PENDING;
  rc=submit(c,job);return rc==RN_PENDING?rc:fail_job(job,rc);
 }
 if(job->state!=R14_LAYER_WAIT || c->active!=job || job->tag!=nn->device->engine[RC_NN].pending_tag)return RN_STALE;
 rc=common_result(rc_poll(nn->device,RC_NN));if(rc==RN_PENDING)return rc;
 c->active=0;if(rc)return fail_job(job,rc);
 job->channel+=(uint32_t)(job->parameter->groups==1?16:4);c->jobs++;nn->dma_jobs++;
 if(job->channel>=(uint32_t)job->parameter->co){
  job->state=R14_LAYER_DONE;c->calls++;nn->layer_calls++;
  c->source_bytes+=job->source_bytes;c->destination_bytes+=job->destination_bytes;return RN_OK;
 }
 job->state=R14_LAYER_READY;return RN_PENDING;
}
int r14_nn_coop_cancel(r14_nn_coop *c,r14_layer_job *job){
 int rc;if(!c || !c->neural || !job)return RN_ARGUMENT;
 if(job->state==R14_LAYER_WAIT){
  if(c->active!=job || c->neural->device->engine[RC_NN].pending_tag!=job->tag)return RN_STALE;
  rc=common_result(rc_cancel(c->neural->device,RC_NN));c->active=0;
  job->state=R14_LAYER_CANCELLED;job->error=(uint32_t)rc;return rc;
 }
 if(job->state==R14_LAYER_READY){job->state=R14_LAYER_CANCELLED;job->error=RN_CANCELLED_ERROR;return RN_CANCELLED_ERROR;}
 return RN_ARGUMENT;
}
int r14_nn_coop_recover(r14_nn_coop *c){
 if(!c || !c->neural || c->active)return RN_ARGUMENT;
 {
  rc_device *d=c->neural->device;int rc;
  if(d->engine[RC_NN].state==RC_IDLE)return RN_OK;
  if(d->engine[RC_NN].state==RC_QUARANTINE){
   rc=rc_recover_begin(d,RC_NN);return rc==RC_OK?RN_PENDING:common_result(rc);
  }
  if(d->engine[RC_NN].state!=RC_CLEAR)return RN_BUSY_ERROR;
  return common_result(rc_recover_poll(d,RC_NN));
 }
}

int r14_nn_coop_prefer_frame(r14_nn_coop *c,int frame){
 if(!c || c->cookie!=COOP_COOKIE || frame< -1 || frame>1)return RN_ARGUMENT;
 c->preferred_frame=frame;return RN_OK;
}
