#include "compute_namespace.h"
#include "stream_dispatch.h"
/* Owned A9 ABI adaptation of frozen v351a. Arithmetic/order and row storage
 * are retained; wire addresses and local cache context replace shared pointers. */
/* One frozen arithmetic lowering, with separate producer/consumer entry
 * points. The existing queued FIELD still executes covariance and solves. */
#include "field_worker_v282.c"
#include "field_stream_v346.h"
/* Both A9s use the same normal-WB SHAREABLE translation table, SCU and
 * ACTLR.SMP/FW checked at platform join. These first two cache lines are
 * PS-only live control: acquire/release communicates them through coherence.
 * Invalidating either live line can discard the other core's dirty progress.
 * Keep maintenance on leased payload/DMA ranges and pre-publication init. */
#define FS_MAGIC 0x46533346u
typedef struct {
 uint32_t magic,active,cancel,produced,error;
 uint8_t pad[44];
} fs_producer_ctl;
typedef struct {
 uint32_t consumed,center,tail,error,terminal,published,output;
 uint8_t pad[36];
} fs_consumer_ctl;
typedef struct {
 double value[320];uint32_t row;uint8_t pad[60];
} fs_packet;
typedef struct __attribute__((aligned(64))) {
 fs_producer_ctl producer;fs_consumer_ctl consumer;
 uint64_t cost_address,weight_address,mean_address,guide_address,logits_address;
 uint64_t private_output_address,output_address;uint32_t disparity,count;
 fs_packet packet[2] __attribute__((aligned(64)));
 int64_t rows[9][320] __attribute__((aligned(64))),columns[320];
} fs_color;
_Static_assert(sizeof(fs_producer_ctl)==64 && sizeof(fs_consumer_ctl)==64,"Separate cache ownership");
_Static_assert(sizeof(fs_packet)==2624,"Two row leases");
_Static_assert(sizeof(fs_color)<=32768,"Stream storage fits reclaimed gather space");
static const field_cache_v282 *fs_cache(void){return ss_lg_stream_cache();}
static void fs_control_order(void){fs_cache()->barrier(fs_cache()->user);}
static int fs_cache_valid(const field_cache_v282 *c){const field_cache_v282 *own=fs_cache();return c && own && c->user==own->user && c->clean==own->clean && c->invalidate==own->invalidate && c->barrier==own->barrier;}
static int fs_regions_v346(const void *const *p,const uint32_t *n,unsigned count,unsigned writes){
 unsigned i,k;for(i=0;i<count;i++){
  if(!p[i] || (uintptr_t)p[i]>UINTPTR_MAX-n[i]-63u)return RC_ARGUMENT;
  for(k=0;k<i;k++)if(((writes>>i)|(writes>>k))&1u)
   if(fq_overlap((uintptr_t)p[i],n[i],(uintptr_t)p[k],n[k]))return RC_LEASE_CONFLICT;
 }return RC_OK;
}
static void fs_clean(fs_color *s,const void *p,uint32_t n){(void)s;fs_cache()->clean(fs_cache()->user,p,n);fs_cache()->barrier(fs_cache()->user);}
static void fs_invalidate(fs_color *s,void *p,uint32_t n){(void)s;fs_cache()->invalidate(fs_cache()->user,p,n);fs_cache()->barrier(fs_cache()->user);}
size_t fs_color_bytes_v346(void){return sizeof(fs_color);}
static int fs_color_prepare_v346(void *memory,size_t bytes,const double *cost,const float *guide,const double *weight,unsigned d,float *scratch,float *out,const field_cache_v282 *cache){
 fs_color *s=memory;const void *regions[6]={memory,cost,guide,weight,scratch,out};uint32_t sizes[6];int error;
 if(!fq_memory(memory,bytes,sizeof(*s)) || !cost || !guide || !weight || !scratch || !out || d>16 || !fs_cache_valid(cache))return RC_ARGUMENT;
 if(!ss_lg_stream_available())return RC_AGAIN;
 if(s->producer.magic==FS_MAGIC && fq_load(&s->producer.active))return RC_AGAIN;
 sizes[0]=(uint32_t)sizeof(*s);sizes[1]=40960;sizes[2]=61440;sizes[3]=163840;sizes[4]=sizes[5]=60u*(80u-d)*4u;
 error=fs_regions_v346(regions,sizes,6,49u);if(error)return error;
 memset(s,0,offsetof(fs_color,packet));s->producer.magic=FS_MAGIC;s->producer.active=1;
 s->cost_address=(uint64_t)(uintptr_t)cost;s->guide_address=(uint64_t)(uintptr_t)guide;s->weight_address=(uint64_t)(uintptr_t)weight;s->disparity=d;s->count=60u*(80u-d);
 s->private_output_address=(uint64_t)(uintptr_t)scratch;s->output_address=(uint64_t)(uintptr_t)out;
 /* Only weight is read by CPU1. CPU0 alone reads cost/guide/logits/mean.
  * Publish the four legal y>=4 spans, including harmless clipped x padding.
  * Scratch rows/columns are first written by CPU1, never initialized by CPU0. */
 {unsigned k;for(k=0;k<4;k++)fs_cache()->clean(fs_cache()->user,weight+k*5120u+320u,60u*80u*8u);fs_cache()->barrier(fs_cache()->user);}
 return RC_OK;
}
int fs_color_begin_v346(void *memory,size_t bytes,const double *cost,const float *guide,const double *weight,unsigned d,float *scratch,float *out,const field_cache_v282 *cache){
 int error=fs_color_prepare_v346(memory,bytes,cost,guide,weight,d,scratch,out,cache);if(error)return error;
 fs_clean(memory,memory,(uint32_t)offsetof(fs_color,packet));return ss_lg_stream_publish(memory,SS_LG_STREAM_COLOR);
}
int fs_color_logits_begin_v348(void *memory,size_t bytes,const float *logits,const double *mean,const float *guide,const double *weight,unsigned d,float *scratch,float *out,const field_cache_v282 *cache){
 fs_color *s=memory;const void *regions[7]={memory,mean,guide,weight,scratch,out,logits};uint32_t sizes[7]={sizeof(*s),40960,61440,163840,0,0,20480};int error;
 if(d>16)return RC_ARGUMENT;
 sizes[4]=sizes[5]=60u*(80u-d)*4u;error=fs_regions_v346(regions,sizes,7,49u);if(error)return error;
 error=fs_color_prepare_v346(memory,bytes,mean,guide,weight,d,scratch,out,cache);if(error)return error;
 s->logits_address=(uint64_t)(uintptr_t)logits;s->mean_address=(uint64_t)(uintptr_t)mean;fs_clean(s,s,(uint32_t)offsetof(fs_color,packet));return ss_lg_stream_publish(s,SS_LG_STREAM_COLOR);
}
unsigned fs_color_rows_v346(const void *p){return ((const fs_color*)p)->producer.produced;}
const double *fs_color_last_row_v346(const void *p){const fs_color *s=p;return s->producer.produced?s->packet[(s->producer.produced-1u)&1u].value:0;}
unsigned fs_color_output_v346(const void *p){return ((const fs_color*)p)->consumer.output;}
int fs_color_producer_v346(void *memory){
 fs_color *s=memory;unsigned row,x,k;fs_packet *packet;
 if(!s || s->producer.magic!=FS_MAGIC || !fq_load(&s->producer.active))return RC_STALE;
 fs_control_order();
 if(fq_load(&s->consumer.terminal))return (int)s->consumer.error;
 if(fq_load(&s->producer.cancel))return RC_PENDING;
 row=s->producer.produced;if(row==64)return RC_PENDING;
 if(row-fq_load(&s->consumer.consumed)>=2)return RC_AGAIN;
 packet=s->packet+(row&1u);
 for(x=0;x<80;x++){
  unsigned p=row*80u+x;double value=((const float *)(uintptr_t)s->logits_address)?((row>=4u && x>=s->disparity)?(double)((const float *)(uintptr_t)s->logits_address)[p]-((const double *)(uintptr_t)s->mean_address)[p]:0.0):((const double *)(uintptr_t)s->cost_address)[p];packet->value[x*4u]=value;
  for(k=0;k<3;k++)packet->value[x*4u+k+1u]=value*(double)((const float *)(uintptr_t)s->guide_address)[k*5120u+p];
 }
 packet->row=row;fs_clean(s,packet,sizeof(*packet));
 fq_store(&s->producer.produced,row+1u);fs_control_order();return RC_PENDING;
}
static int fs_color_emit(fs_color *s,unsigned y){
 int64_t hsum[4];unsigned x,k;
 if(y<4)return RC_OK;
 for(k=0;k<4;k++){hsum[k]=s->columns[k]*5;for(x=1;x<=4;x++)hsum[k]+=s->columns[x*4u+k];}
 for(x=0;x<80;x++){
  if(x>=s->disparity){
   unsigned p=y*80u+x;double a[4],value,t;
   for(k=0;k<4;k++)a[k]=field_q40_to_f64_v317(hsum[k]);
   value=a[0]*((const double *)(uintptr_t)s->weight_address)[p];t=a[1]*((const double *)(uintptr_t)s->weight_address)[5120u+p];value=value+t;
   t=a[2]*((const double *)(uintptr_t)s->weight_address)[10240u+p];value=value+t;t=a[3]*((const double *)(uintptr_t)s->weight_address)[15360u+p];value=value+t;
   if(!isfinite(value))return RC_ARGUMENT;
   ((float *)(uintptr_t)s->private_output_address)[s->consumer.output]=(float)value;
   if(!isfinite(((float *)(uintptr_t)s->private_output_address)[s->consumer.output]))return RC_ARGUMENT;
   s->consumer.output++;
  }
  if(x<79){unsigned old=x<4?0:x-4u,next=x+5u<80?x+5u:79u;
   for(k=0;k<4;k++)hsum[k]=hsum[k]+s->columns[next*4u+k]-s->columns[old*4u+k];
  }
 }
 return RC_OK;
}
static int fs_color_terminal(fs_color *s,int error){
 /* Retire every CPU1-written temporary before the terminal receipt. Dirty
  * scratch must not later evict over a new CPU0/gray lease of this arena.
  * The success path has already copied and cleaned the shared result. */
 fs_invalidate(s,s->rows,(uint32_t)(sizeof(s->rows)+sizeof(s->columns)));
 fs_invalidate(s,((float *)(uintptr_t)s->private_output_address),s->count*4u);
 s->consumer.error=(uint32_t)error;fq_store(&s->consumer.terminal,1);fs_control_order();return error;
}
int fs_color_consumer_v346(void *memory){
 fs_color *s=memory;unsigned row,z;int error;
 if(!s || !fs_cache())return RC_ARGUMENT;
 fs_control_order();
 if(!s || s->producer.magic!=FS_MAGIC || !fq_load(&s->producer.active))return RC_STALE;
 fs_control_order();
 fs_control_order();
 if(fq_load(&s->consumer.terminal))return (int)s->consumer.error;
 if(fq_load(&s->producer.cancel))return fs_color_terminal(s,RC_CANCELLED);
 if(fq_load(&s->producer.error))return fs_color_terminal(s,(int)fq_load(&s->producer.error));
 row=s->consumer.consumed;
 if(row<64){
  fs_packet *packet;if(fq_load(&s->producer.produced)<=row)return RC_PENDING;
  if(!row){fs_invalidate(s,(uint8_t*)s+128,(uint32_t)offsetof(fs_color,packet)-128u);{unsigned k;for(k=0;k<4;k++)fs_cache()->invalidate(fs_cache()->user,(void*)(((const double *)(uintptr_t)s->weight_address)+k*5120u+320u),60u*80u*8u);fs_cache()->barrier(fs_cache()->user);}}
  packet=s->packet+(row&1u);fs_invalidate(s,packet,sizeof(*packet));
  if(packet->row!=row)return fs_color_terminal(s,RC_IDENTITY);
  /* Validate and convert each received pair once. The public output stays
   * untouched even if a later row fails the original full-domain check. */
  for(z=0;z<320;z+=2){
   uint64_t b0=load_bits(packet->value,z),b1=load_bits(packet->value,z+1u);
   if(!field_rows_domain_v303(b0) || !field_rows_domain_v303(b1))return fs_color_terminal(s,RC_ARGUMENT);
   if(row<=4){
    qp_v345 incoming=field_q40_pair_value_v345(qp_load_v345(packet->value+z));
    qp_store_v345(s->rows[row]+z,incoming);
    if(!row)qp_store_v345(s->columns+z,qp_add_v345(incoming,qp_shift_v345(incoming,qp_const_v345(2))));
    else qp_store_v345(s->columns+z,qp_add_v345(qp_load_v345(s->columns+z),incoming));
   }else{
    unsigned old=row<9?0:row-9u,slot=row%9u;
    field_columns_incoming_pair_v345(packet->value+z,s->rows[old%9u]+z,s->rows[slot]+z,s->columns+z);
   }
  }
  if(row>=4){error=fs_color_emit(s,row-4u);if(error)return fs_color_terminal(s,error);}
  fq_store(&s->consumer.consumed,row+1u);fs_control_order();return RC_PENDING;
 }
 if(s->consumer.tail<4){
  unsigned y=60u+s->consumer.tail,old=y-5u;
  for(z=0;z<320;z+=2)field_columns_cached_pair_v345(s->rows[63u%9u]+z,s->rows[old%9u]+z,s->columns+z);
  error=fs_color_emit(s,y);if(error)return fs_color_terminal(s,error);s->consumer.tail++;fs_control_order();
  return RC_PENDING;
 }
 if(s->consumer.output!=s->count)return fs_color_terminal(s,RC_IDENTITY);
 fs_control_order();if(fq_load(&s->producer.cancel))return fs_color_terminal(s,RC_CANCELLED);
 memcpy(((float *)(uintptr_t)s->output_address),((float *)(uintptr_t)s->private_output_address),s->count*4u);fs_clean(s,((float *)(uintptr_t)s->output_address),s->count*4u);
 s->consumer.published=1;return fs_color_terminal(s,RC_OK);
}
int fs_color_cancel_v346(void *memory){
 fs_color *s=memory;if(!s || s->producer.magic!=FS_MAGIC || !fq_load(&s->producer.active))return RC_STALE;
 fq_store(&s->producer.cancel,1);fs_control_order();return RC_CANCELLED;
}
int fs_color_take_v346(void *memory){
 fs_color *s=memory;int error;if(!s || s->producer.magic!=FS_MAGIC || !fq_load(&s->producer.active))return RC_STALE;
 fs_control_order();if(!fq_load(&s->consumer.terminal))return RC_PENDING;
 {int retired=ss_lg_stream_retire(s,SS_LG_STREAM_COLOR);if(retired)return retired;}
 error=(int)s->consumer.error;fq_store(&s->producer.active,0);fs_control_order();return error;
}

typedef struct __attribute__((aligned(64))) {
 fs_producer_ctl producer;fs_consumer_ctl consumer;
 uint64_t coeff_address,q_address,guide_address,median_address,output_address;
 int64_t rows[2][3][320] __attribute__((aligned(64)));unsigned tags[2];
} fs_gray;
_Static_assert(sizeof(fs_gray)<=16384,"Gray rows fit reused gather storage");
size_t fs_gray_bytes_v346(void){return sizeof(fs_gray);}
int fs_gray_begin_v346(void *memory,size_t bytes,double *coeff,int64_t *q,const double *guide,const float *median,float *output,const field_cache_v282 *cache){
 fs_gray *s=memory;const void *regions[6]={memory,coeff,q,guide,median,output};uint32_t sizes[6]={sizeof(*s),115200,115200,614400,307200,307200};int error;
 if(!fq_memory(memory,bytes,sizeof(*s)) || !coeff || !q || !guide || !median || !output || !fs_cache_valid(cache))return RC_ARGUMENT;
 error=fs_regions_v346(regions,sizes,6,39u);if(error)return error;
 if(!ss_lg_stream_available())return RC_AGAIN;
 if(s->producer.magic==FS_MAGIC && fq_load(&s->producer.active))return RC_AGAIN;
 memset(s,0,offsetof(fs_gray,rows));s->producer.magic=FS_MAGIC;s->producer.active=1;s->coeff_address=(uint64_t)(uintptr_t)coeff;s->q_address=(uint64_t)(uintptr_t)q;s->guide_address=(uint64_t)(uintptr_t)guide;s->median_address=(uint64_t)(uintptr_t)median;s->output_address=(uint64_t)(uintptr_t)output;
 cache->clean(cache->user,guide,614400);cache->clean(cache->user,median,307200);cache->clean(cache->user,s,(uint32_t)offsetof(fs_gray,rows));cache->barrier(cache->user);return ss_lg_stream_publish(s,SS_LG_STREAM_GRAY);
}
unsigned fs_gray_prepared_v346(const void *p){return ((const fs_gray*)p)->producer.produced;}
unsigned fs_gray_output_v346(const void *p){return ((const fs_gray*)p)->consumer.output;}
int fs_gray_producer_v346(void *memory){
 fs_gray *s=memory;unsigned i,end;if(!s || s->producer.magic!=FS_MAGIC || !fq_load(&s->producer.active))return RC_STALE;
 if(s->producer.error)return (int)s->producer.error;
 if(s->producer.cancel)return RC_PENDING;
 i=s->producer.produced;if(i==14400)return RC_PENDING;
 end=i+1280u;if(end>14400u)end=14400u;
 for(;i<end;i+=2){
  if(i<9600){((double *)(uintptr_t)s->coeff_address)[i]*=1.0/81.0;((double *)(uintptr_t)s->coeff_address)[i+1u]*=1.0/81.0;}
  if(!field_rows_domain_v303(load_bits(((double *)(uintptr_t)s->coeff_address),i)) || !field_rows_domain_v303(load_bits(((double *)(uintptr_t)s->coeff_address),i+1u))){fq_store(&s->producer.error,RC_ARGUMENT);break;}
  field_q40_pair_valid_v345(((double *)(uintptr_t)s->coeff_address)+i,((int64_t *)(uintptr_t)s->q_address)+i);
 }
 if(i==14400)fs_cache()->clean(fs_cache()->user,((int64_t *)(uintptr_t)s->q_address),115200);
 fq_store(&s->producer.produced,i);fs_cache()->barrier(fs_cache()->user);
 return s->producer.error?(int)s->producer.error:RC_PENDING;
}
static void fs_gray_horizontal(const int64_t *q,unsigned row,int64_t h[3][320]){
 unsigned p,c,i;for(p=0;p<3;p++){
  const int64_t *a=q+p*4800u+row*80u;h[p][0]=h[p][1]=a[0]*8;
  for(c=0;c<79;c++){int64_t delta=a[c+1u]-a[c],u=a[c]*8+delta,step=delta*2;
   for(i=0;i<4;i++){h[p][2u+c*4u+i]=u;u+=step;}}
  h[p][318]=h[p][319]=a[79]*8;
 }
}
static int fs_gray_terminal_v350(fs_gray *s,int error){
 fs_cache()->invalidate(fs_cache()->user,s->rows,(uint32_t)(sizeof(s->rows)+sizeof(s->tags)));
 if(error)fs_cache()->invalidate(fs_cache()->user,((float *)(uintptr_t)s->output_address),307200u);
 fs_cache()->barrier(fs_cache()->user);
 s->consumer.error=(uint32_t)error;fq_store(&s->consumer.terminal,1);
 fs_cache()->barrier(fs_cache()->user);return error;
}
int fs_gray_consumer_v346(void *memory){
 fs_gray *s=memory;unsigned y,x,p,lo,hi,fraction,needed[2];int error=RC_PENDING;
 if(!s || !fs_cache())return RC_ARGUMENT;
 fs_cache()->barrier(fs_cache()->user);
 if(!s || s->producer.magic!=FS_MAGIC || !fq_load(&s->producer.active))return RC_STALE;
 fs_cache()->barrier(fs_cache()->user);
 fs_cache()->barrier(fs_cache()->user);
 if(fq_load(&s->consumer.terminal))return (int)s->consumer.error;
 if(fq_load(&s->producer.cancel))error=RC_CANCELLED;else if(fq_load(&s->producer.error))error=(int)fq_load(&s->producer.error);
 else if(fq_load(&s->producer.produced)<14400)return RC_PENDING;
 else if(s->consumer.output==76800)error=RC_OK;
 if(error!=RC_PENDING){
  if(!error){fs_cache()->clean(fs_cache()->user,((float *)(uintptr_t)s->output_address),307200);s->consumer.published=1;}
  return fs_gray_terminal_v350(s,error);
 }
 if(!s->consumer.output){fs_cache()->invalidate(fs_cache()->user,(uint8_t*)s+128,(uint32_t)offsetof(fs_gray,rows)-128u);s->tags[0]=s->tags[1]=60;fs_cache()->invalidate(fs_cache()->user,((int64_t *)(uintptr_t)s->q_address),115200);fs_cache()->invalidate(fs_cache()->user,(void*)((const double *)(uintptr_t)s->guide_address),614400);fs_cache()->invalidate(fs_cache()->user,(void*)((const float *)(uintptr_t)s->median_address),307200);fs_cache()->barrier(fs_cache()->user);}
 y=s->consumer.output/320u;lo=y<2?0:(y-2u)/4u;hi=lo<59?lo+1u:59;fraction=y<2?0:2u*((y-2u)&3u)+1u;needed[0]=lo;needed[1]=hi;
 for(p=0;p<2;p++)if(s->tags[needed[p]&1u]!=needed[p]){fs_gray_horizontal(((int64_t *)(uintptr_t)s->q_address),needed[p],s->rows[needed[p]&1u]);s->tags[needed[p]&1u]=needed[p];}
 for(x=0;x<320;x++){
  unsigned i=y*320u+x;double c[3];for(p=0;p<3;p++)c[p]=vertical_v303(s->rows[lo&1u][p][x],s->rows[hi&1u][p][x],fraction);
  error=gray_pixel(((const double *)(uintptr_t)s->guide_address)[i],((const float *)(uintptr_t)s->median_address)[i],c[0],c[1],c[2],((float *)(uintptr_t)s->output_address)+i);
  if(error)return fs_gray_terminal_v350(s,error);
  s->consumer.output++;
 }
 fs_cache()->barrier(fs_cache()->user);return RC_PENDING;
}
int fs_gray_cancel_v346(void *memory){
 fs_gray *s=memory;if(!s || s->producer.magic!=FS_MAGIC || !fq_load(&s->producer.active))return RC_STALE;
 fq_store(&s->producer.cancel,1);fs_cache()->barrier(fs_cache()->user);return RC_CANCELLED;
}
int fs_gray_take_v346(void *memory){
 fs_gray *s=memory;int error;if(!s || s->producer.magic!=FS_MAGIC || !fq_load(&s->producer.active))return RC_STALE;
 fs_cache()->barrier(fs_cache()->user);if(!fq_load(&s->consumer.terminal))return RC_PENDING;
 {int retired=ss_lg_stream_retire(s,SS_LG_STREAM_GRAY);if(retired)return retired;}
 error=(int)s->consumer.error;fq_store(&s->producer.active,0);fs_cache()->barrier(fs_cache()->user);return error;
}

_Static_assert(offsetof(fs_color,rows)%64==0,"Colour consumer scratch cache isolation");
_Static_assert(offsetof(fs_gray,rows)%64==0,"Gray consumer scratch cache isolation");
size_t fs_color_scratch_offset_v350(void){return offsetof(fs_color,rows);}
size_t fs_gray_scratch_offset_v350(void){return offsetof(fs_gray,rows);}
size_t fs_color_init_bytes_v350(void){return offsetof(fs_color,packet);}
size_t fs_gray_init_bytes_v350(void){return offsetof(fs_gray,rows);}

unsigned fs_color_consumed_v349(const void *p){return ((const fs_color*)p)->consumer.consumed;}
int fs_color_ready_v349(void *p){
 fs_color *v=p;if(!v || v->producer.magic!=FS_MAGIC || !fq_load(&v->producer.active))return 0;
 fs_control_order();fs_control_order();
 return !fq_load(&v->consumer.terminal) && (fq_load(&v->producer.cancel) || fq_load(&v->producer.error) || fq_load(&v->producer.produced)>fq_load(&v->consumer.consumed) || fq_load(&v->consumer.consumed)==64);
}
int fs_gray_ready_v349(void *p){
 fs_gray *v=p;if(!v || v->producer.magic!=FS_MAGIC || !fq_load(&v->producer.active))return 0;
 fs_cache()->barrier(fs_cache()->user);
 return !fq_load(&v->consumer.terminal) && (fq_load(&v->producer.cancel) || fq_load(&v->producer.error) || fq_load(&v->producer.produced)==14400);
}

_Static_assert(offsetof(fs_color,packet)==192,"same ARM/host color descriptor");
_Static_assert(offsetof(fs_color,rows)==5440 && sizeof(fs_color)==31040,"same ARM/host color storage");
_Static_assert(offsetof(fs_gray,rows)==192 && sizeof(fs_gray)==15616,"same ARM/host gray storage");
