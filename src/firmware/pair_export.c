#include "pair_export.h"
#include <float.h>
#include <string.h>

_Static_assert(sizeof(float)==4 && FLT_RADIX==2 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128,"IEEE754 binary32 required");
enum { JOB_ROWS=1,JOB_CRC=2,JOB_READY=3,JOB_FAILED=4 };
static int span(const void *p,size_t n){return p && n && n-1u<=UINTPTR_MAX-(uintptr_t)p;}
static int overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;}
static int same(ss_identity a,ss_identity b){return a.boot==b.boot && a.shot==b.shot && a.epoch==b.epoch && a.attempt==b.attempt;}
static void be16(uint8_t *p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void be32(uint8_t *p,uint32_t v){unsigned k;for(k=0;k<4;k++)p[k]=(uint8_t)(v>>(24u-8u*k));}
static void be64(uint8_t *p,uint64_t v){be32(p,(uint32_t)(v>>32));be32(p+4,(uint32_t)v);}
static void le32(uint8_t *p,uint32_t v){unsigned k;for(k=0;k<4;k++)p[k]=(uint8_t)(v>>(8u*k));}
uint32_t ss_crc32_update(uint32_t crc,const uint8_t *p,size_t n){
    static const uint32_t nibble[16]={0x00000000u,0x1db71064u,0x3b6e20c8u,0x26d930acu,
        0x76dc4190u,0x6b6b51f4u,0x4db26158u,0x5005713cu,0xedb88320u,0xf00f9344u,
        0xd6d6a3e8u,0xcb61b38cu,0x9b64c2b0u,0x86d3d2d4u,0xa00ae278u,0xbdbdf21cu};
    size_t k;for(k=0;k<n;k++){crc^=p[k];crc=(crc>>4)^nibble[crc&15u];crc=(crc>>4)^nibble[crc&15u];}return crc;
}
static void header(ss_pair_job *j){
    uint8_t *p=j->packet;const ss_pair_source *s=&j->source;
    memset(p,0,SS_HEADER_BYTES);memcpy(p,"SDP1",4);p[4]=1;p[5]=2;be16(p+6,SS_HEADER_BYTES);
    be32(p+8,SS_PAYLOAD_BYTES);be32(p+12,j->crc^UINT32_MAX);
    be64(p+16,s->identity.boot);be64(p+24,s->identity.shot);be32(p+32,s->identity.epoch);be32(p+36,s->identity.attempt);
    be64(p+40,s->trigger_us);be64(p+48,s->locked_us);be64(p+56,s->sgm_done_us);be64(p+64,s->model_done_us);
    be32(p+72,11459);be16(p+76,SS_WIDTH);be16(p+78,SS_HEIGHT);be16(p+80,4);be16(p+82,s->flags);
}
int ss_pair_begin(ss_pair_job *j,const ss_pair_source *s,void *packet,size_t capacity){
    const void *v[8];size_t n[8];unsigned k,l;
    if(!j || !s || capacity<SS_PACKET_BYTES || s->rgb_bytes!=SS_PIXELS*3u || s->sgm_pixels!=SS_PIXELS || s->model_pixels!=SS_MODEL_PIXELS || s->roi_pixels!=SS_PIXELS || s->flags>1u)return SS_ARGUMENT;
    if(((uintptr_t)s->sgm&7u) || ((uintptr_t)s->model&3u))return SS_ARGUMENT;
    if(!s->identity.boot || !s->identity.shot || !same(s->identity,s->rgb_identity) || !same(s->identity,s->sgm_identity) || !same(s->identity,s->model_identity))return SS_STALE;
    if(s->trigger_us>s->locked_us || s->locked_us>s->sgm_done_us || s->sgm_done_us>s->model_done_us)return SS_ARGUMENT;
    v[0]=j;n[0]=sizeof(*j);v[1]=s;n[1]=sizeof(*s);v[2]=packet;n[2]=SS_PACKET_BYTES;
    v[3]=s->rgb;n[3]=SS_PIXELS*3u;v[4]=s->sgm;n[4]=SS_PIXELS*8u;v[5]=s->model;n[5]=SS_MODEL_PIXELS*4u;
    v[6]=s->roi[0];v[7]=s->roi[1];n[6]=n[7]=SS_PIXELS;
    for(k=0;k<8;k++){if(!span(v[k],n[k]))return SS_ARGUMENT;for(l=0;l<k;l++)if(overlap(v[k],n[k],v[l],n[l]))return SS_ARGUMENT;}
    memset(j,0,sizeof(*j));j->source=*s;j->packet=packet;j->capacity=capacity;j->crc=UINT32_MAX;j->state=JOB_ROWS;
    memset(packet,0,SS_HEADER_BYTES);return SS_OK;
}
int ss_pair_step(ss_pair_job *j,unsigned rows){
    unsigned end,y,x;uint8_t *rgb,*d,*valid,*model;
    if(!j || !j->packet || !rows || rows>8u)return SS_ARGUMENT;
    if(j->state==JOB_READY)return SS_OK;
    if(j->state!=JOB_ROWS && j->state!=JOB_CRC)return SS_DATA;
    if(j->state==JOB_CRC){
        size_t left=SS_PAYLOAD_BYTES-j->crc_offset,take=(size_t)rows*SS_WIDTH*9u;
        if(take>left)take=left;
        j->crc=ss_crc32_update(j->crc,j->packet+SS_HEADER_BYTES+j->crc_offset,take);j->crc_offset+=take;
        if(j->crc_offset!=SS_PAYLOAD_BYTES)return SS_PENDING;
        header(j);j->state=JOB_READY;return SS_OK;
    }
    rgb=j->packet+SS_HEADER_BYTES;d=rgb+SS_PIXELS*3u;valid=d+SS_PIXELS;model=valid+SS_PIXELS;
    end=j->row+rows;if(end>SS_HEIGHT)end=SS_HEIGHT;
    for(y=j->row;y<end;y++){
        for(x=0;x<SS_WIDTH;x++){
            unsigned i=y*SS_WIDTH+x;uint64_t p=j->source.sgm[i];
            unsigned disp=(unsigned)p&255u,raw=(unsigned)(p>>8)&255u,best=(unsigned)(p>>16)&65535u,second=(unsigned)(p>>32)&65535u,good=(unsigned)(p>>48)&1u;
            float f=j->source.model[16u*SS_WIDTH+i];uint32_t bits;memcpy(&bits,&f,4);
            if(p>>49 || (good?(disp>=64u || raw>24u || best>192u || (second!=65535u && (second<best || second>192u))):(disp!=255u || raw!=255u || best!=1020u || second!=65535u)) ||
               (bits&0x7f800000u)==0x7f800000u || f<0.0f || f>63.99609375f){j->state=JOB_FAILED;memset(j->packet,0,SS_HEADER_BYTES);return SS_DATA;}
            /* Preserve both original result values. Match the old display's
             * Q8 nearest-even then integer-shift ROI lookup, without rounding
             * the FP32 transmitted model output or feeding masks into inference. */
            {
                float scaled=f*256.0f;unsigned q=(unsigned)scaled,shift,sgm_roi,model_roi;
                float fraction=scaled-(float)q;
                if(fraction>0.5f || (fraction==0.5f && (q&1u)))q++;
                shift=(q+128u)/256u;
                if(j->source.roi[0][i]>1u || j->source.roi[1][i]>1u){j->state=JOB_FAILED;memset(j->packet,0,SS_HEADER_BYTES);return SS_DATA;}
                sgm_roi=good && j->source.roi[0][i] && disp<=x && j->source.roi[1][i-disp];
                model_roi=j->source.roi[0][i] && shift<=x && j->source.roi[1][i-shift];
                d[i]=(uint8_t)disp;valid[i]=(uint8_t)(good | (sgm_roi<<1) | (model_roi<<2));le32(model+4u*i,bits);
            }
        }
        memcpy(rgb+y*SS_WIDTH*3u,j->source.rgb+y*SS_WIDTH*3u,SS_WIDTH*3u);
    }
    j->row=end;if(end==SS_HEIGHT)j->state=JOB_CRC;return SS_PENDING;
}
int ss_pair_packet(const ss_pair_job *j,const uint8_t **p,size_t *n){
    if(!p || !n)return SS_ARGUMENT;
    *p=NULL;*n=0;if(!j || j->state!=JOB_READY)return SS_PENDING;
    *p=j->packet;*n=SS_PACKET_BYTES;return SS_OK;
}
void ss_pair_cancel(ss_pair_job *j){if(j){j->state=JOB_FAILED;if(j->packet)memset(j->packet,0,SS_HEADER_BYTES);}}
