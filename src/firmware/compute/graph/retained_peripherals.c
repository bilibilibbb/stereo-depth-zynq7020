#include "retained_peripherals.h"
#include "r14_profile.h"
#include <string.h>
#include <limits.h>
#define FB_BYTES (640u*480u*3u)
static int cr(r14_camera *c,uint32_t reg,uint32_t *v){return c->io.read32(c->io.user,c->base+reg,v);}
static int cw(r14_camera *c,uint32_t reg,uint32_t v){return c->io.write32(c->io.user,c->base+reg,v);}
static int camera_error(r14_camera *c,int code,uint32_t detail){if(!c->error){c->error=(uint32_t)code;c->detail=detail;}return code;}
static int delay_ms(r14_camera *c,uint32_t ms){
    uint64_t end=c->io.now_us(c->io.user)+(uint64_t)ms*1000;uint32_t count=0;
    while(c->io.now_us(c->io.user)<end)if(++count==100000000u)return -1;
    return 0;
}
static int sccb(r14_camera *c,uint16_t reg,int value,uint8_t answer[2]){
    uint32_t v,i,iterations=0;uint64_t end=c->io.now_us(c->io.user)+c->timeout_us;
    if(cr(c,0x2c,&v) || (v&3) || cw(c,0x20,reg) || (value>=0 && cw(c,0x24,(uint32_t)value)) || cw(c,0x28,value<0?0x103u:3u))return camera_error(c,40,reg);
    do{
        if(cr(c,0x2c,&v))return camera_error(c,40,reg);
        if(c->io.now_us(c->io.user)>=end || ++iterations>1000000)return camera_error(c,41,reg);
    }while((v&3) || (v&0x300)!=0x300);
    for(i=0;i<2;i++){
        if(cr(c,0x30+4*i,&v))return camera_error(c,40,reg);
        if(v&0xff00)return camera_error(c,42,((uint32_t)reg<<16)|(i<<8)|((v>>8)&255));
        answer[i]=(uint8_t)v;
    }return 0;
}
int r14_camera_open(r14_camera *c,const r14_camera_step *recipe,uint32_t count,const r14_camera_step *readback,uint32_t checks){
    uint32_t v,i;uint8_t hi[2],lo[2];
    if(!c || !c->io.read32 || !c->io.write32 || !c->io.now_us || !recipe || !readback || !count || count>1024 || checks>128 ||
       c->base!=R14_SENSOR_BASE || !c->build || !c->timeout_us || c->timeout_us>10000000u)return 43;
    c->error=0;c->detail=0;c->opened=0;
    if(cr(c,0,&v) || v!=0x53544742 || cr(c,4,&v) || v!=0x10000 || cr(c,8,&v) || v!=c->build)return camera_error(c,44,0);
    if(cw(c,0x14,0) || delay_ms(c,10) || cw(c,0x14,2) || delay_ms(c,10) || cw(c,0x14,3) || delay_ms(c,30) ||
        cr(c,0x38,&v) || (v&63)!=31)return camera_error(c,45,v);
    if(sccb(c,0x300a,-1,hi) || sccb(c,0x300b,-1,lo))return (int)c->error;
    for(i=0;i<2;i++)if(hi[i]!=0x56 || lo[i]!=0x40)return camera_error(c,46,i);
    for(i=0;i<count;i++){
        if(recipe[i].delay_ms){if(recipe[i].delay_ms>2000 || delay_ms(c,recipe[i].delay_ms))return camera_error(c,41,i);}
        else if(sccb(c,recipe[i].reg,recipe[i].value,lo))return (int)c->error;
    }
    for(i=0;i<checks;i++){
        if(sccb(c,readback[i].reg,-1,lo))return (int)c->error;
        if(lo[0]!=readback[i].value || lo[1]!=readback[i].value)return camera_error(c,47,readback[i].reg);
    }
    c->opened=1;return 0;
}
static int vr(r14_display *d,uint32_t reg,uint32_t *v){return d->io.read32(d->io.user,d->base+reg,v);}
static int vw(r14_display *d,uint32_t reg,uint32_t v){return d->io.write32(d->io.user,d->base+reg,v);}
static int video_error(r14_display *d,int code,uint32_t detail){d->quarantined=1;if(!d->error){d->error=(uint32_t)code;d->detail=detail;}return code;}
int r14_display_open(r14_display *d){
    uint32_t v,i=0;uint64_t end;
    if(!d || !d->io.read32 || !d->io.write32 || !d->io.flush || !d->io.barrier || !d->io.now_us || !d->frames[0] || !d->frames[1] ||
        d->frames[0]==d->frames[1] || d->base!=R14_VDMA_BASE || d->physical[0]!=R14_FRAMEBUFFER0 || d->physical[1]!=R14_FRAMEBUFFER1 || (d->physical[0]&63) || (d->physical[1]&63) ||
        d->physical[0]<0x10000000 || d->physical[1]<0x10000000 || (uint64_t)d->physical[0]+FB_BYTES>0x20000000 ||
        (uint64_t)d->physical[1]+FB_BYTES>0x20000000 ||
        ((uint64_t)d->physical[0]+FB_BYTES>d->physical[1] && (uint64_t)d->physical[1]+FB_BYTES>d->physical[0]) || !d->timeout_us)return 50;
    d->opened=0;d->quarantined=0;d->error=0;d->detail=0;d->front=0;
    if(vw(d,0,4))return video_error(d,51,0);
    end=d->io.now_us(d->io.user)+d->timeout_us;
    do{if(vr(d,0,&v))return video_error(d,51,0);if(d->io.now_us(d->io.user)>=end || ++i>1000000)return video_error(d,52,0);}while(v&4);
    for(i=0;i<2;i++){memset(d->frames[i],0,FB_BYTES);d->io.flush(d->io.user,d->frames[i],FB_BYTES);}
    d->io.barrier(d->io.user);
    /* SDK 2018.3 axivdma_v6_6 register definitions: MM2S only, 2 stores, park mode. */
    if(vw(d,0x5c,d->physical[0]) || vw(d,0x60,d->physical[1]) || vw(d,0x58,1920) || vw(d,0x54,1920) ||
        vw(d,0x28,0) || vw(d,0,1) || vw(d,0x50,480))return video_error(d,51,1);
    d->opened=1;return 0;
}
static int profiled_display_present(r14_display *d,uint32_t next){
    uint32_t v,i=0;uint64_t end;
    if(!d || !d->opened || d->quarantined || next>1 || next==d->front)return 50;
    /* PG020: current frame pointer is not a DMA-drained acknowledgement.
       Wait for HALTED before releasing the old framebuffer to the CPU.
       An early stream termination is handled by scanout's next-SOF resync. */
    R14P_BEGIN(R14P_VDMA_HALT);if(vw(d,0,0))return video_error(d,51,5);
    end=d->io.now_us(d->io.user)+d->timeout_us;
    do{
        if(vr(d,4,&v))return video_error(d,51,6);
        if(v&0xff0)return video_error(d,53,v);
        if(d->io.now_us(d->io.user)>=end || ++i>1000000)return video_error(d,52,v);
    }while(!(v&1));
    R14P_END(R14P_VDMA_HALT,0);R14P_BEGIN(R14P_VDMA_FLUSH);
    d->io.flush(d->io.user,d->frames[next],FB_BYTES);d->io.barrier(d->io.user);
    R14P_ADD(R14P_DISPLAY_FLUSH_BYTES,FB_BYTES);R14P_END(R14P_VDMA_FLUSH,0);R14P_BEGIN(R14P_VDMA_START_WAIT);
    if(vw(d,0x28,next) || vw(d,0,1) || vw(d,0x50,480))return video_error(d,51,2);
    end=d->io.now_us(d->io.user)+d->timeout_us;i=0;
    for(;;){
        if(vr(d,4,&v))return video_error(d,51,3);
        if(v&0xff0)return video_error(d,53,v);
        if(vr(d,0x28,&v))return video_error(d,51,4);
        if(((v>>16)&31)==next)break;
        if(d->io.now_us(d->io.user)>=end || ++i>1000000)return video_error(d,52,v);
    }
    R14P_END(R14P_VDMA_START_WAIT,0);d->front=next;return 0;
}
int r14_display_present(r14_display *d,uint32_t next){
 int rc;R14P_ADD(R14P_DISPLAY_ATTEMPTS,1);R14P_BEGIN(R14P_DISPLAY);
 rc=profiled_display_present(d,next);R14P_ABORT(R14P_VDMA_HALT,R14P_VDMA_START_WAIT,rc);R14P_END(R14P_DISPLAY,rc);
 if(rc)R14P_ADD(R14P_DISPLAY_FAILED,1);else R14P_ADD(R14P_DISPLAY_COMPLETED,1);return rc;
}
