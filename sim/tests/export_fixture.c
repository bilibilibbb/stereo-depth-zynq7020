#include "pair_export.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t sgm[SS_PIXELS];
static float model[SS_MODEL_PIXELS];
static uint8_t rgb[SS_PIXELS*3],packet[SS_PACKET_BYTES];
static uint8_t roi[2][SS_PIXELS];
static int finish(ss_pair_job *job){int rc;unsigned calls=0;do{rc=ss_pair_step(job,4);assert(++calls<=121);}while(rc==SS_PENDING);return rc;}
int main(int argc,char **argv){
    ss_pair_job job;ss_pair_source s;const uint8_t *p;size_t n;unsigned i;uint32_t nan=0x7fc00000u;FILE *file;uint64_t saved;
    assert(argc==2);memset(&s,0,sizeof(s));
    s.identity.boot=0x123456789abcdef0ull;s.identity.shot=9;s.identity.epoch=4;s.identity.attempt=2;
    s.rgb_identity=s.sgm_identity=s.model_identity=s.identity;
    s.trigger_us=100000;s.locked_us=130000;s.sgm_done_us=160000;s.model_done_us=900000;s.flags=1;
    s.rgb=rgb;s.rgb_bytes=sizeof(rgb);s.sgm=sgm;s.sgm_pixels=SS_PIXELS;s.model=model;s.model_pixels=SS_MODEL_PIXELS;
    s.roi[0]=roi[0];s.roi[1]=roi[1];s.roi_pixels=SS_PIXELS;
    for(i=0;i<16u*SS_WIDTH;i++)memcpy(model+i,&nan,4);
    for(i=0;i<SS_PIXELS;i++){
        unsigned x=i%SS_WIDTH,y=i/SS_WIDTH,d=(x+y)%64;
        sgm[i]=i%17 ? (uint64_t)d | (UINT64_C(5)<<8) | (UINT64_C(20)<<16) | (UINT64_C(30)<<32) | (UINT64_C(1)<<48)
                         : UINT64_C(255) | (UINT64_C(255)<<8) | (UINT64_C(1020)<<16) | (UINT64_C(65535)<<32);
        model[16u*SS_WIDTH+i]=(float)d+0.25f;rgb[3*i]=(uint8_t)x;rgb[3*i+1]=(uint8_t)y;rgb[3*i+2]=(uint8_t)(x^y);
        roi[0][i]=(uint8_t)(i%29u!=0u);roi[1][i]=(uint8_t)(i%31u!=7u);
    }
    model[16u*SS_WIDTH+1]=0.0f;
    model[16u*SS_WIDTH+10]=2.498046875f; /* Q8 tie rounds to640, shift3. */
    assert((ss_crc32_update(UINT32_MAX,(const uint8_t*)"123456789",9)^UINT32_MAX)==0xcbf43926u);
    assert(ss_pair_begin(&job,&s,packet,sizeof(packet)-1)==SS_ARGUMENT);
    s.model_identity.shot++;assert(ss_pair_begin(&job,&s,packet,sizeof(packet))==SS_STALE);s.model_identity=s.identity;
    assert(ss_pair_begin(&job,&s,model,SS_PACKET_BYTES)==SS_ARGUMENT);
    s.roi[0]=packet;assert(ss_pair_begin(&job,&s,packet,sizeof(packet))==SS_ARGUMENT);s.roi[0]=roi[0];
    assert(ss_pair_begin(&job,&s,packet,sizeof(packet))==SS_OK);
    assert(ss_pair_packet(&job,&p,&n)==SS_PENDING && !p && !n);
    assert(ss_pair_step(&job,0)==SS_ARGUMENT && ss_pair_step(&job,9)==SS_ARGUMENT);
    assert(finish(&job)==SS_OK && ss_pair_packet(&job,&p,&n)==SS_OK && n==SS_PACKET_BYTES);
    file=fopen(argv[1],"wb");assert(file);assert(fwrite(p,1,n,file)==n);assert(fclose(file)==0);
    assert(!(packet[96u+4u*SS_PIXELS+10u]&4u));
    roi[1][SS_PIXELS-1]=2;
    assert(ss_pair_begin(&job,&s,packet,sizeof(packet))==SS_OK && finish(&job)==SS_DATA);roi[1][SS_PIXELS-1]=1;
    /* A late invalid SGM pixel must never expose a partially written pair. */
    saved=sgm[SS_PIXELS-1];sgm[SS_PIXELS-1]|=UINT64_C(1)<<55;
    assert(ss_pair_begin(&job,&s,packet,sizeof(packet))==SS_OK && finish(&job)==SS_DATA);
    assert(ss_pair_packet(&job,&p,&n)==SS_PENDING && !p && !n);sgm[SS_PIXELS-1]=saved;
    memcpy(model+16u*SS_WIDTH+777,&nan,4);
    assert(ss_pair_begin(&job,&s,packet,sizeof(packet))==SS_OK && finish(&job)==SS_DATA);
    model[16u*SS_WIDTH+777]=0.0f;
    assert(ss_pair_begin(&job,&s,packet,sizeof(packet))==SS_OK);ss_pair_cancel(&job);
    assert(ss_pair_packet(&job,&p,&n)==SS_PENDING && ss_pair_step(&job,1)==SS_DATA);
    puts("C export: original FP32/SGM preserved, rectification support masks/Q8 tie, identities, aliases, bounds, invalid data and cancellation PASS");return 0;
}
