/* Execution-only candidate. The frozen reference keeps all FP evaluation
 * order, validity checks and clamp semantics in this translation unit. */
#define field_cpu_v274 field_cpu_reference_v303
#include "field_cpu_reference_v303.c"
#undef field_cpu_v274
#include "q40_pack_v317.h"
#include "q40_pair_v345.h"

static int reference_mode_v303;
void field_rows_reference_mode_v303(int enabled){reference_mode_v303=enabled!=0;}

/* This is exactly the domain accepted by field_q40_v274: above 65536 the
 * next representable double already exceeds the Q40 bound by 16 units.
 * NaNs and infinities are above the absolute-bit bound as well. Preflight
 * preserves the reference's rejection before any output is written. */
int field_rows_domain_v303(uint64_t bits){
 return (bits&UINT64_C(0x7fffffffffffffff))<=UINT64_C(0x40f0000000000000);
}
static int preflight_v303(const void *source,unsigned count){
 unsigned i;
 for(i=0;i<count;i++)if(!field_rows_domain_v303(load_bits(source,i)))return RC_ARGUMENT;
 return RC_OK;
}

static int horizontal_row_v303(const void *coeff,unsigned row,int64_t h[3][320]){
 int64_t q[80];unsigned p,c,i;
 for(p=0;p<3;p++){
  for(c=0;c<80;c+=2)field_q40_pair_valid_v345((const unsigned char*)coeff+8u*(p*4800u+row*80u+c),q+c);
  h[p][0]=h[p][1]=q[0]*8;
  for(c=0;c<79;c++){
   int64_t delta=q[c+1]-q[c],u=q[c]*8+delta,step=delta*2;
   for(i=0;i<4;i++){h[p][2u+c*4u+i]=u;u+=step;}
  }
  h[p][318]=h[p][319]=q[79]*8;
 }
 return RC_OK;
}
static double vertical_v303(int64_t a,int64_t b,unsigned fraction){
 int64_t n=a*8+(b-a)*(int64_t)fraction;
 uint64_t magnitude=n<0?(uint64_t)-n:(uint64_t)n,rounded=magnitude>>6;
 if((magnitude&63u)>32u || ((magnitude&63u)==32u && (rounded&1u)))rounded++;
 return field_q40_to_f64_v317(n<0?-(int64_t)rounded:(int64_t)rounded);
}
static int gray_rows_v303(rf_job *j){
 int64_t rows[2][3][320];unsigned tag[2]={60,60},y,x,p;int rc;
 const double *guide=j->buffers[0].pointer;const float *median=j->buffers[1].pointer;
 const void *coeff=j->buffers[2].pointer;float *out=j->buffers[3].pointer;
 rc=preflight_v303(coeff,14400);if(rc)return rc;
 for(y=0;y<240;y++){
  unsigned lo=y<2?0:(y-2u)/4u,hi=lo<59?lo+1u:59;
  unsigned fraction=y<2?0:2u*((y-2u)&3u)+1u,needed[2]={lo,hi};
  for(p=0;p<2;p++)if(tag[needed[p]&1u]!=needed[p]){
   rc=horizontal_row_v303(coeff,needed[p],rows[needed[p]&1u]);if(rc)return rc;
   tag[needed[p]&1u]=needed[p];
  }
  for(x=0;x<320;x++){
   unsigned i=y*320u+x;double c[3];
   for(p=0;p<3;p++)c[p]=vertical_v303(rows[lo&1u][p][x],rows[hi&1u][p][x],fraction);
   rc=gray_pixel(guide[i],median[i],c[0],c[1],c[2],out+i);if(rc)return rc;
  }
 }
 return RC_OK;
}

static int color_rows_v303(rf_job *j){
 int64_t rows[9][320],columns[320],hsum[4];unsigned tag[9];
 const void *s=j->buffers[0].pointer;const double *w=j->buffers[1].pointer;
 float *out=j->buffers[2].pointer;unsigned i=0,d=j->native_address[4]&31u,y,x,k,z;int rc;
 rc=preflight_v303(s,20480);if(rc)return rc;
 for(y=0;y<9;y++)tag[y]=64;
 for(y=0;y<=4;y++){
  for(z=0;z<320;z+=2)field_q40_pair_valid_v345((const unsigned char*)s+8u*(y*320u+z),rows[y]+z);
  tag[y]=y;
 }
 for(z=0;z<320;z++){columns[z]=rows[0][z]*5;for(k=1;k<=4;k++)columns[z]+=rows[k][z];}
 for(y=0;y<64;y++){
  if(y>=4){
   for(k=0;k<4;k++){hsum[k]=columns[k]*5;for(x=1;x<=4;x++)hsum[k]+=columns[x*4u+k];}
   for(x=0;x<80;x++){
    if(x>=d){
     unsigned base=(i/128u)*128u,lanes=j->count-base,lane=i-base;
     double v,t,a[4];if(lanes>128u)lanes=128u;
     for(k=0;k<4;k++)a[k]=field_q40_to_f64_v317(hsum[k]);
     v=a[0]*w[base*4u+lane];t=a[1]*w[base*4u+lanes+lane];v=v+t;
     t=a[2]*w[base*4u+2u*lanes+lane];v=v+t;
     t=a[3]*w[base*4u+3u*lanes+lane];v=v+t;
     if(!isfinite(v))return RC_ARGUMENT;
     out[i]=(float)v;if(!isfinite(out[i]))return RC_ARGUMENT;
     i++;
    }
    if(x<79){unsigned old=x<4?0:x-4u,next=x+5u<80?x+5u:79u;
     for(k=0;k<4;k++)hsum[k]=hsum[k]+columns[next*4u+k]-columns[old*4u+k];
    }
   }
  }
  if(y<63){
   unsigned old=y<4?0:y-4u,next=y+5u<64?y+5u:63u;
   unsigned new_slot=next%9u,old_slot=old%9u;
   if(tag[new_slot]!=next){
    for(z=0;z<320;z+=2)field_columns_incoming_pair_v345((const unsigned char*)s+8u*(next*320u+z),rows[old_slot]+z,rows[new_slot]+z,columns+z);
    tag[new_slot]=next;
   }else for(z=0;z<320;z+=2)field_columns_cached_pair_v345(rows[new_slot]+z,rows[old_slot]+z,columns+z);
  }
 }
 return i==j->count?RC_OK:RC_ARGUMENT;
}
int field_cpu_v274(rf_job *j,unsigned *kind){
 if(!j || !kind || !j->count)return RC_ARGUMENT;
 if(reference_mode_v303)return field_cpu_reference_v303(j,kind);
 if(j->native_mode==1){*kind=5;return gray_rows_v303(j);}
 if(j->native_mode==2){*kind=6;return color_rows_v303(j);}
 return field_cpu_reference_v303(j,kind);
}

/* Standalone probes expose the exact geometry and error contracts to the
 * verifier. Production calls still use the original FIELD interface. */
int field_gray_probe_v303(int reference,const void *coeff,const double *guide,const float *median,float *out){
 rf_job j;unsigned kind;memset(&j,0,sizeof(j));j.native_mode=1;j.count=76800;
 j.buffers[0].pointer=(void*)guide;j.buffers[1].pointer=(void*)median;
 j.buffers[2].pointer=(void*)coeff;j.buffers[3].pointer=out;
 return reference?field_cpu_reference_v303(&j,&kind):gray_rows_v303(&j);
}
int field_color_probe_v303(int reference,unsigned disparity,const void *stats,const double *weights,float *out){
 rf_job j;unsigned kind;if(disparity>16u)return RC_ARGUMENT;
 memset(&j,0,sizeof(j));j.native_mode=2;j.count=60u*(80u-disparity);j.native_address[4]=disparity;
 j.buffers[0].pointer=(void*)stats;j.buffers[1].pointer=(void*)weights;j.buffers[2].pointer=out;
 return reference?field_cpu_reference_v303(&j,&kind):color_rows_v303(&j);
}

uint64_t field_q40_bits_probe_v317(int64_t value){return field_q40_bits_v317(value);}
uint64_t field_q40_original_probe_v317(int64_t value){double d=(double)value*0x1p-40;uint64_t bits;memcpy(&bits,&d,8);return bits;}

int field_q40_pair_probe_v345(uint64_t first,uint64_t second,int64_t *out){
 uint64_t bits[2]={first,second};
 if(!out || !field_rows_domain_v303(first) || !field_rows_domain_v303(second))return RC_ARGUMENT;
 field_q40_pair_valid_v345(bits,out);return RC_OK;
}

uint64_t field_q40_split_bits_probe_v338(int64_t value){double v=field_q40_to_f64_v317(value);uint64_t b;memcpy(&b,&v,8);return b;}
