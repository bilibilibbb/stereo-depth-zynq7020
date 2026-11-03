#include "fw_observations.h"
#include <math.h>
#include <string.h>
#include "fw_rgb_normalization.inc"
static int obs_overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;}
static int obs_spans(const void *const *p,const size_t *n,unsigned count){unsigned i,j;for(i=0;i<count;i++){if(!p[i] || n[i]-1u>UINTPTR_MAX-(uintptr_t)p[i])return 0;for(j=0;j<i;j++)if(obs_overlap(p[i],n[i],p[j],n[j]))return 0;}return 1;}
int fw_observations_prepare_begin(fw_observation_prepare *p,const uint8_t *rgb,const float *norm,float *gray,uint32_t *census){
 const void *v[4]={p,rgb?(const void*)rgb:(const void*)norm,gray,census};size_t n[4]={sizeof(*p),rgb?230400u:983040u,327680u,307200u};
 if(!p || (!rgb)==(!norm) || ((uintptr_t)gray&3u) || ((uintptr_t)census&3u) || (norm && ((uintptr_t)norm&3u)) || !obs_spans(v,n,4u))return FW_ARGUMENT;
 memset(p,0,sizeof(*p));p->rgb=rgb;p->normalized=norm;p->gray=gray;p->census=census;return FW_OK;
}
int fw_observations_prepare_rgb_begin(fw_observation_prepare *p,const uint8_t *rgb,float *image,float *gray,uint32_t *census){
 const void *v[5]={p,rgb,image,gray,census};size_t n[5]={sizeof(*p),230400u,983040u,327680u,307200u};
 if(((uintptr_t)image&3u) || ((uintptr_t)gray&3u) || ((uintptr_t)census&3u) || !obs_spans(v,n,5u))return FW_ARGUMENT;
 memset(p,0,sizeof(*p));p->rgb=rgb;p->model_image=image;p->gray=gray;p->census=census;return FW_OK;
}
static int obs_rgb(const float *norm,unsigned at,unsigned c,unsigned *out){
 static const float mean[3]={.485f,.456f,.406f},std[3]={.229f,.224f,.225f};float z=norm[c*FW_OBS_N+at],r;unsigned v;
 if(!isfinite(z))return FW_PACKET;
 z=z*std[c];z=z+mean[c];z=z*255.0f;
 if(z<=0.0f){*out=0u;return FW_OK;}if(z>=255.0f){*out=255u;return FW_OK;}
 v=(unsigned)z;r=z-(float)v;if(r>0.5f || (r==0.5f && (v&1u)))v++;*out=v;return FW_OK;
}
int fw_observations_prepare_step(fw_observation_prepare *p){
 unsigned y,x,c,z[3];int dx,dy,rc;
 if(!p || (!p->rgb && !p->normalized) || !p->gray || !p->census)return FW_ARGUMENT;
 if(p->error)return (int)p->error;
 y=p->row;if(y>=240u)return FW_OK;
 if(!y)memset(p->gray,0,16u*320u*sizeof(float));
 if(y<2u || y>=238u)memset(p->census+y*320u,0,320u*sizeof(uint32_t));
 for(x=0;x<320u;x++){
  if(p->rgb)for(c=0;c<3u;c++)z[c]=p->rgb[(y*320u+x)*3u+c];
  else for(c=0;c<3u;c++){rc=obs_rgb(p->normalized,(y+16u)*320u+x,c,z+c);if(rc){p->error=(uint32_t)rc;return rc;}}
  if(p->model_image)for(c=0;c<3u;c++)p->model_image[c*FW_OBS_N+(y+16u)*320u+x]=obs_normalized_rgb[c][z[c]];
  p->luma[y%5u][x]=(uint8_t)((77u*z[0]+150u*z[1]+29u*z[2]+128u)>>8u);
  p->gray[(y+16u)*320u+x]=(float)p->luma[y%5u][x]*(1.0f/255.0f);
 }
 if(!y && p->model_image)for(c=0;c<3u;c++){
  unsigned row;float *plane=p->model_image+c*FW_OBS_N;
  for(row=0;row<16u;row++)memcpy(plane+row*320u,plane+16u*320u,320u*sizeof(float));
 }
 if(y>=4u){
  unsigned center=y-2u;uint32_t *dst=p->census+center*320u;
  dst[0]=dst[1]=dst[318]=dst[319]=0u;
  for(x=2u;x<318u;x++){
   uint32_t bits=0u;uint8_t mid=p->luma[center%5u][x];
   for(dy=-2;dy<=2;dy++)for(dx=-2;dx<=2;dx++)if(dy || dx)bits=(bits<<1u)|(p->luma[(unsigned)((int)center+dy)%5u][(unsigned)((int)x+dx)]<mid);
   dst[x]=bits|0x1000000u;
  }
 }
 p->row=y+1u;return p->row==240u?FW_OK:FW_PENDING;
}
int fw_observations_decode_begin(fw_observation_decode *p,const uint64_t *packets,const uint16_t *records,float *native,float *curve){
 const void *v[5]={p,packets,records,native,curve};size_t n[5]={sizeof(*p),614400u,368640u,1310720u,696320u};
 if(!p || ((uintptr_t)packets&7u) || ((uintptr_t)records&1u) || ((uintptr_t)native&3u) || ((uintptr_t)curve&3u) || !obs_spans(v,n,5u))return FW_ARGUMENT;
 memset(p,0,sizeof(*p));p->packets=packets;p->records=records;p->native=native;p->curve=curve;return FW_OK;
}
int fw_observations_decode_step(fw_observation_decode *p){
 unsigned q,i,k,base;
 if(!p || !p->packets || !p->records || !p->native || !p->curve)return FW_ARGUMENT;
 if(p->error)return (int)p->error;
 q=p->quarter_row;if(q>=64u)return FW_OK;
 base=q*1280u;
 if(q<4u){for(k=0;k<4u;k++)memset(p->native+k*81920u+base,0,1280u*sizeof(float));}
 else for(i=0;i<1280u;i++){
  uint64_t v=p->packets[(q-4u)*1280u+i];unsigned d=(unsigned)v&255u,raw=(unsigned)(v>>8u)&255u,best=(unsigned)(v>>16u)&65535u,second=(unsigned)(v>>32u)&65535u,valid=(unsigned)(v>>48u)&1u;
  if(v>>49u || (valid?(d>=64u || raw>24u || best>192u || (second!=65535u && (second<best || second>192u))):(d!=255u || raw!=255u || best!=1020u || second!=65535u))){p->error=FW_PACKET;return FW_PACKET;}
  p->native[base+i]=valid?(float)d/64.0f:0.0f;
  p->native[81920u+base+i]=valid && second<65535u?(float)(second-best)/192.0f:0.0f;
  p->native[163840u+base+i]=(float)valid;
  p->native[245760u+base+i]=valid?(float)raw/24.0f:1.0f;
 }
 for(i=q*80u;i<(q+1u)*80u;i++){
  const uint16_t *r=p->records+i*36u;
  if(r[34] || r[35]){p->error=FW_PACKET;return FW_PACKET;}
  for(k=0;k<17u;k++){
   unsigned num=r[k],mass=r[17u+k];
   if(mass>256u || num>mass*192u || (i<320u && (num || mass))){p->error=FW_PACKET;return FW_PACKET;}
   p->curve[k*5120u+i]=mass?(float)num/(float)(192u*mass):1.0f;
   p->curve[(17u+k)*5120u+i]=(float)mass*(1.0f/256.0f);
  }
 }
 p->quarter_row=q+1u;return p->quarter_row==64u?FW_OK:FW_PENDING;
}
