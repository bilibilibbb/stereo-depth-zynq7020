#include "video_lane.h"
#include "video_capture.h"
#include "retained_image.h"
#include <string.h>

static int overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;}
static void be16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void be32(uint8_t *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(24-8*i));}
static void be64(uint8_t *p,uint64_t v){be32(p,(uint32_t)(v>>32));be32(p+4,(uint32_t)v);}
static int fail(ss_video *v,int rc){
    if(!v->error)v->error=rc;
    if(v->camera){
        ss_camera_copy_abort(v->camera,rc);
        if(v->camera->request_pending)v->camera->request_error=rc;
    }
    return rc;
}
static int writer_slot(ss_video *v){
    unsigned i;for(i=0;i<2;i++)if(v->state[i]==SS_VIDEO_FREE)return (int)i;
    for(i=0;i<2;i++)if(v->state[i]==SS_VIDEO_READY)return (int)i;
    return -1;
}
static void begin_copy(ss_video *v,unsigned raw_slot,uint64_t ready){
    v->raw_slot=raw_slot;v->raw_epoch=v->device->epoch[raw_slot];
    v->source_frame=v->device->descriptor[raw_slot][17];v->ready_us=ready;v->row=0;
    v->crc=v->full_range?UINT32_MAX:v->device->descriptor[raw_slot][23];v->phase=SS_VIDEO_ACQUIRE;
}
static int arm_next(ss_video *v){
    int rc=ss_video_capture_arm(v->device,v->next_slot);
    if(rc==RN_BUSY_ERROR && !v->device->first_error)return SS_PENDING;
    if(rc)return fail(v,SS_IO);
    v->active=(int)v->next_slot;v->next_slot^=1u;return SS_OK;
}
int ss_video_init(ss_video *v,rc_device *d,uint64_t boot,unsigned full,uint8_t *a,uint8_t *b,size_t capacity){
    unsigned i,j;const void *span[7];size_t bytes[7];
    if(!v || !d || !boot || full>1 || !d->opened || d->first_error || d->ownership[0] || d->ownership[1] ||
       !a || !b || capacity<SS_PREVIEW_MAX_BYTES)return SS_ARGUMENT;
    span[0]=v;bytes[0]=sizeof(*v);span[1]=a;span[2]=b;bytes[1]=bytes[2]=capacity;
    for(i=0;i<4;i++){span[3+i]=d->buffers[i/2][i%2];bytes[3+i]=RC_FRAME_BYTES;}
    for(i=0;i<7;i++){
        if(!span[i] || bytes[i]-1>UINTPTR_MAX-(uintptr_t)span[i] || overlap(span[i],bytes[i],d,sizeof(*d)))return SS_ARGUMENT;
        for(j=0;j<i;j++)if(overlap(span[i],bytes[i],span[j],bytes[j]))return SS_ARGUMENT;
    }
    /* Both raw slots are PL-write/CPU0-read only for this lane's lifetime. */
    for(i=0;i<4;i++)d->io.flush(d->io.user,d->buffers[i/2][i%2],RC_FRAME_BYTES);
    d->io.barrier(d->io.user);
    memset(v,0,sizeof(*v));v->device=d;v->boot=boot;v->full_range=full;
    v->packet[0]=a;v->packet[1]=b;v->capacity=capacity;v->writer=v->loan=v->active=-1;return SS_OK;
}
int ss_video_step(ss_video *v,int admit_capture){
    rc_device *d;uint64_t now;unsigned i,take,stride;int rc,slot=-1;uint8_t *p;
    if(!v || !(d=v->device))return SS_ARGUMENT;
    if(v->error)return v->error;
    now=d->io.now_us(d->io.user);if(now<v->last_us)return fail(v,SS_CLOCK);v->last_us=now;
    switch(v->phase){
    case SS_VIDEO_IDLE:
        if(!admit_capture)return SS_PENDING;
        rc=arm_next(v);if(rc!=SS_OK)return rc;
        v->phase=SS_VIDEO_WAIT;return SS_PENDING;
    case SS_VIDEO_WAIT:
        if(v->active<0)return fail(v,SS_STALE);
        rc=ss_video_capture_poll(d,(unsigned)v->active);if(rc==RN_PENDING)return SS_PENDING;if(rc)return fail(v,SS_IO);
        slot=writer_slot(v);if(slot<0)return fail(v,SS_BUSY);
        if(v->sequence==UINT64_MAX)return fail(v,SS_EXHAUSTED);
        if(v->state[slot]==SS_VIDEO_READY)v->dropped++;
        v->writer=slot;v->state[slot]=SS_VIDEO_BUILD;v->sequence++;
        begin_copy(v,(unsigned)v->active,now);v->active=-1;
        /* Arm BEFORE cache maintenance and copying. Completed pixels remain
         * leased in the other slot, so capture and copy cannot overwrite. */
        if(admit_capture){rc=arm_next(v);if(rc<0)return rc;}
        return SS_PENDING;
    case SS_VIDEO_ACQUIRE:
        v->model_copy=0;
        if(v->camera){
            rc=ss_camera_copy_begin(v->camera,v->raw_slot,v->ready_us);
            if(rc<0)return fail(v,rc);
            v->model_copy=rc==SS_OK;
        }
        d->io.invalidate(d->io.user,d->buffers[v->raw_slot][0],RC_FRAME_BYTES);
        if(v->model_copy)d->io.invalidate(d->io.user,d->buffers[v->raw_slot][1],RC_FRAME_BYTES);
        d->io.barrier(d->io.user);v->phase=SS_VIDEO_COPY;return SS_PENDING;
    case SS_VIDEO_COPY:
        if(v->writer<0 || v->state[v->writer]!=SS_VIDEO_BUILD || d->ownership[v->raw_slot]!=2u || d->epoch[v->raw_slot]!=v->raw_epoch)return fail(v,SS_STALE);
        stride=v->full_range?1920u:1280u;p=v->packet[v->writer]+96u+v->row*stride;
        take=SS_RAW_HEIGHT-v->row;if(take>(v->full_range?4u:32u))take=v->full_range?4u:32u;
        if(v->full_range){
            rc=r14_yuyv_rgb((const uint8_t*)d->buffers[v->raw_slot][0]+v->row*1280u,take*1280u,640,take,1280,1,p,v->gray);
            if(rc)return fail(v,SS_DATA);
            v->crc=ss_crc32_update(v->crc,p,take*stride);
        }else memcpy(p,(const uint8_t*)d->buffers[v->raw_slot][0]+v->row*1280u,take*1280u);
        if(v->model_copy)for(i=0;i<2;i++)memcpy(v->camera->external_raw[i]+v->row*1280u,
            (const uint8_t*)d->buffers[v->raw_slot][i]+v->row*1280u,take*1280u);
        v->row+=take;if(v->row==SS_RAW_HEIGHT)v->phase=SS_VIDEO_RELEASE;return SS_PENDING;
    case SS_VIDEO_RELEASE:
        if(d->ownership[v->raw_slot]!=2u || d->epoch[v->raw_slot]!=v->raw_epoch)return fail(v,SS_STALE);
        rc=rc_release(d,v->raw_slot);if(rc)return fail(v,SS_IO);
        if(v->model_copy){rc=ss_camera_copy_finish(v->camera);if(rc)return fail(v,rc);v->model_frames++;v->model_copy=0;}
        p=v->packet[v->writer];memset(p,0,96);memcpy(p,"SDP1",4);p[4]=1;p[5]=1;be16(p+6,96);
        be32(p+8,v->full_range?SS_RAW_RGB_BYTES:RC_FRAME_BYTES);be32(p+12,v->full_range?(v->crc^UINT32_MAX):v->crc);
        be64(p+16,v->boot);be64(p+24,v->sequence);be32(p+32,v->raw_epoch);be64(p+48,v->ready_us);
        be16(p+76,640);be16(p+78,480);be16(p+80,v->full_range?1:2);
        for(i=0;i<2;i++)if(v->state[i]==SS_VIDEO_READY){v->state[i]=SS_VIDEO_FREE;v->dropped++;}
        v->state[v->writer]=SS_VIDEO_READY;v->writer=-1;v->published++;
        v->last_source_frame=v->source_frame;v->source_seen=1;v->phase=v->active>=0?SS_VIDEO_WAIT:SS_VIDEO_IDLE;return SS_OK;
    default:return fail(v,SS_ARGUMENT);
    }
}
int ss_video_acquire(ss_video *v,const uint8_t **packet,size_t *bytes){
    unsigned i;if(!v || !packet || !bytes)return SS_ARGUMENT;*packet=NULL;*bytes=0;
    if(v->loan>=0)return SS_BUSY;
    for(i=0;i<2;i++)if(v->state[i]==SS_VIDEO_READY){
        v->loan=(int)i;v->state[i]=SS_VIDEO_LOAN;*packet=v->packet[i];
        *bytes=96u+(v->full_range?SS_RAW_RGB_BYTES:RC_FRAME_BYTES);return SS_OK;
    }
    return SS_PENDING;
}
void ss_video_release(ss_video *v){if(v && v->loan>=0){v->state[v->loan]=SS_VIDEO_FREE;v->loan=-1;}}
