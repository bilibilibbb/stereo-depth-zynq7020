/* Derived by scripts/video_capture_source.py. Video owns both DMA slots.
 * Init cleans all four buffers once; CPUs never write them thereafter.
 * Poll validates the complete descriptor without touching pixel memory.
 * The producer can arm the other slot immediately, then must invalidate
 * each consumed eye before its first read. CPU1 reads a separate copy.
 * All descriptor/identity/freshness/quarantine checks remain intact.
 */
#include "retained_camera_driver.h"
#include <string.h>
static int rd(rc_device *c,uint32_t a,uint32_t *v){return c->io.read32(c->io.user,c->base+a,v)?RN_IO_ERROR:RN_OK;}
static int wr(rc_device *c,uint32_t a,uint32_t v){return c->io.write32(c->io.user,c->base+a,v)?RN_IO_ERROR:RN_OK;}
static int eq(rc_device *c,uint32_t a,uint32_t want){uint32_t v;return rd(c,a,&v) || v!=want;}
static int error(rc_device *c,int e,uint32_t detail){if(!c->first_error){c->first_error=(uint32_t)e;c->detail=detail;}return e;}
static int quarantine(rc_device *c,uint32_t slot,int e,uint32_t detail){c->ownership[slot]=3;return error(c,e,detail);}
static int identity(rc_device *c){
    uint32_t v;
    if(eq(c,0,0x43415032u) || eq(c,4,0x10000u) || eq(c,0x40,640) || eq(c,0x44,480) || eq(c,0x48,1280) || eq(c,0x4c,RC_FRAME_BYTES))return RN_IDENTITY;
    if(c->io.read32(c->io.user,c->sensor_base,&v) || v!=0x53544742u || c->io.read32(c->io.user,c->sensor_base+8,&v) || v!=c->sensor_build)return RN_IDENTITY;
    return RN_OK;
}
static int session(rc_device *c){return identity(c) || eq(c,0x10,c->cookie) || eq(c,0x1c,c->format) || eq(c,0x20,c->mode_tag) || eq(c,0x24,c->max_skew_ticks);}
static int descriptor(rc_device *c,uint32_t slot,uint32_t *p){uint32_t i;for(i=0;i<64;i++)if(rd(c,0x100u+slot*256u+i*4u,p+i))return RN_IO_ERROR;return RN_OK;}
int ss_video_capture_arm(rc_device *c,uint32_t slot){
    uint32_t s;
    if(!c || !c->opened || slot>1)return RN_ARGUMENT;
    if(c->ownership[slot] || c->first_error)return RN_BUSY_ERROR;
    if(session(c))return error(c,RN_IDENTITY,0);
    if(rd(c,8,&s) || rd(c,0x34,&c->epoch[slot]))return error(c,RN_IO_ERROR,8);
    if((s&0x301u) || (s&(2u<<slot)))return RN_BUSY_ERROR;
    if(!c->epoch[slot] || c->epoch[slot]==0xffffffffu)return error(c,RN_IDENTITY,0x34);
    c->io.barrier(c->io.user);
    c->ownership[slot]=1;c->active_slot=slot;c->started_us[slot]=c->io.now_us(c->io.user);
    if(wr(c,0x0c,1u+(slot<<8)))return quarantine(c,slot,RN_IO_ERROR,0x0c);
    return RN_OK;
}
int ss_video_capture_poll(rc_device *c,uint32_t slot){
    uint32_t s,*p,side,base;int64_t skew;int rc;
    if(!c || !c->opened || slot>1 || c->ownership[slot]!=1)return RN_ARGUMENT;
    if(rd(c,0x100u+slot*256u,&s))return quarantine(c,slot,RN_IO_ERROR,0x100);
    if(!(s&2u)){
        if(c->io.now_us(c->io.user)-c->started_us[slot]>=c->timeout_us){(void)wr(c,0xc,2);return quarantine(c,slot,RN_TIMEOUT,s);}
        return 9;
    }
    p=c->descriptor[slot];rc=descriptor(c,slot,p);if(rc)return quarantine(c,slot,rc,0x100);
    if(p[0]!=6 || p[1]!=c->epoch[slot] || p[2]!=c->cookie || p[3] || p[4]!=c->format || p[5]!=c->mode_tag ||
       p[6]!=c->max_skew_ticks || p[7]>c->max_skew_ticks || p[8]!=R14_CAPTURE_START+slot*0x200000u || p[9]!=p[8]+0x100000u || p[10]!=RC_FRAME_BYTES || session(c))return quarantine(c,slot,RN_STALE,p[0]);
    skew=(int32_t)(p[34]-p[18]);if(skew<0)skew=-skew;
    if((uint64_t)skew!=p[7])return quarantine(c,slot,RN_STALE,7);
    for(side=0;side<2;side++){
        base=16u+16u*side;
        if(p[base] || p[base+4]!=RC_FRAME_BYTES || p[base+5]!=480 || p[base+6]!=76800 || p[base+8]<1 || p[base+8]>512 ||
           p[base+9]!=0xffffffffu || p[base+10]!=76800 || p[base+11]!=76800 || (p[base+13]&31u)!=6u)return quarantine(c,slot,RN_HARDWARE,base);
        if(c->source_seen && (int32_t)(p[base+1]-c->last_source[side])<=0)return quarantine(c,slot,RN_STALE,base+1);
    }
    for(side=0;side<2;side++)c->last_source[side]=p[17+16*side];
    c->io.barrier(c->io.user);c->source_seen=1;c->ownership[slot]=2;return RN_OK;
}
