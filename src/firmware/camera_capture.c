#include "camera_capture.h"
#include "retained_image.h"
#include <math.h>
#include <string.h>

#define CAL_MAGIC 0x5343414cu
enum { WORK_OPEN=1u,WORK_CPU0=2u,WORK_CPU1=4u };
static uint32_t close_work(ss_camera *c){
    return __atomic_fetch_and(&c->work_gate,~(uint32_t)WORK_OPEN,__ATOMIC_ACQ_REL)&(WORK_CPU0|WORK_CPU1);
}
static int same(ss_identity a,ss_identity b){return a.boot==b.boot && a.shot==b.shot && a.epoch==b.epoch && a.attempt==b.attempt;}
static int span(const void *p,size_t n){return p && n && n-1u<=UINTPTR_MAX-(uintptr_t)p;}
static int overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;}
static uint32_t crc(const void *p,size_t n){return ss_crc32_update(UINT32_MAX,p,n)^UINT32_MAX;}
static void be16(uint8_t *p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void be32(uint8_t *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(24u-i*8u));}
static void be64(uint8_t *p,uint64_t v){be32(p,(uint32_t)(v>>32));be32(p+4,(uint32_t)v);}
int ss_calibration_bind(ss_calibration *c,const void *blob,size_t bytes,uint32_t platform){
    uint32_t h[32],i,side,identity=0,inside[2]={0,0};const uint8_t *p=blob;
    const float *map[2],*q;float numerator=0,denominator=0;
    if(!c || !platform || bytes!=SS_CALIBRATION_BYTES || !span(p,bytes) || ((uintptr_t)p&3u) ||
       overlap(c,sizeof(*c),p,bytes))return SS_ARGUMENT;
    memset(c,0,sizeof(*c));memcpy(h,p,sizeof(h));
    map[0]=(const float*)(p+128);map[1]=map[0]+SS_PIXELS*2u;q=map[1]+SS_PIXELS*2u;
    for(i=16;i<24;i++)identity|=h[i];
    if(h[0]!=0x52313443u || h[1]!=0x10000u || h[2]!=platform || h[3]!=640u || h[4]!=480u ||
       h[5]!=320u || h[6]!=240u || h[7]!=64u || h[11]>1u || h[12]>1000000u ||
       !h[13] || h[15]!=1u || !identity || h[31]!=crc(p,124u) ||
       h[8]!=crc(map[0],SS_PIXELS*8u) || h[9]!=crc(map[1],SS_PIXELS*8u) || h[10]!=crc(q,64u))return SS_DATA;
    for(i=0;i<16;i++)if(!isfinite(q[i]))return SS_DATA;
    for(i=8;i<12;i++)numerator+=fabsf(q[i]);
    for(i=12;i<16;i++)denominator+=fabsf(q[i]);
    if(numerator==0 || denominator==0)return SS_DATA;
    for(i=0;i<SS_PIXELS;i++)for(side=0;side<2u;side++){
        float x=map[side][2u*i],y=map[side][2u*i+1u];
        if(isfinite(x) && isfinite(y) && x>=0 && x<639 && y>=0 && y<479)inside[side]++;
    }
    if(!inside[0] || !inside[1])return SS_DATA;
    *c=(ss_calibration){p,{map[0],map[1]},q,platform,h[13],h[11],h[12],h[31],CAL_MAGIC};return SS_OK;
}
static int fault(ss_camera *c,int reason,int driver){
    (void)close_work(c);
    if(!c->error)c->error=reason;
    if(driver && !c->driver_error)c->driver_error=driver;
    if(c->request_owned && !c->request_error)c->request_error=reason;
    c->phase=SS_CAM_QUARANTINE;return reason;
}
int ss_camera_init(ss_camera *c,const ss_camera_config *cfg){
    const void *v[15];size_t n[15];unsigned i,j;rc_device *d;const ss_calibration *k;
    if(!c || !cfg || c->initialized || !(d=cfg->device) || !(k=cfg->calibration) || k->magic!=CAL_MAGIC ||
       !cfg->boot || !d->opened || d->first_error || d->ownership[0] || d->ownership[1] ||
       d->format!=1u || d->mode_tag!=k->mode_tag || d->sensor_build!=k->platform || d->max_skew_ticks!=k->max_skew_ticks ||
       cfg->raw_rgb_bytes!=SS_RAW_RGB_BYTES || cfg->rectified_bytes!=SS_PIXELS*3u || cfg->roi_bytes!=SS_PIXELS ||
       cfg->preview_capacity<96u+(k->full_range?SS_RAW_RGB_BYTES:RC_FRAME_BYTES))return SS_ARGUMENT;
    v[0]=c;n[0]=sizeof(*c);v[1]=d;n[1]=sizeof(*d);v[2]=k->blob;n[2]=SS_CALIBRATION_BYTES;
    v[3]=cfg->raw_rgb;n[3]=SS_RAW_RGB_BYTES;v[4]=cfg->rectified[0];v[5]=cfg->rectified[1];n[4]=n[5]=SS_PIXELS*3u;
    v[6]=cfg->roi[0];v[7]=cfg->roi[1];n[6]=n[7]=SS_PIXELS;
    v[8]=cfg->preview_packet;n[8]=cfg->preview_capacity;v[9]=k;n[9]=sizeof(*k);
    for(i=0;i<4u;i++){
        v[10u+i]=d->buffers[i/2u][i%2u];n[10u+i]=RC_FRAME_BYTES;
#if defined(__arm__)
        if((uintptr_t)v[10u+i]!=R14_CAPTURE_START+(i/2u)*0x200000u+(i%2u)*0x100000u)return SS_ARGUMENT;
#endif
    }
    v[14]=cfg->right_raw_rgb;n[14]=SS_RAW_RGB_BYTES;
    for(i=0;i<15u;i++){
        if(!span(v[i],n[i]))return SS_ARGUMENT;
        for(j=0;j<i;j++)if(overlap(v[i],n[i],v[j],n[j]))return SS_ARGUMENT;
    }
    if(overlap(c,sizeof(*c),cfg,sizeof(*cfg)))return SS_ARGUMENT;
    for(i=3u;i<8u;i++)if((uintptr_t)v[i]&63u)return SS_ARGUMENT;
    if((uintptr_t)v[14]&63u)return SS_ARGUMENT;
    memset(c,0,sizeof(*c));c->config=*cfg;c->initialized=1;return SS_OK;
}
int ss_camera_begin(void *user,ss_identity id,uint64_t trigger){
    ss_camera *c=user;uint64_t now;
    if(!c || !c->initialized || id.boot!=c->config.boot || !id.shot)return SS_ARGUMENT;
    if(c->request_owned)return SS_BUSY;
    if(c->error)return c->error;
    if(c->request.shot && id.shot<=c->request.shot)return SS_STALE;
    now=c->config.device->io.now_us(c->config.device->io.user);if(trigger>now)return SS_CLOCK;
    c->request=id;c->trigger_us=trigger;c->request_owned=c->request_pending=1;
    c->request_ready=c->request_cancelled=0;c->request_error=0;memset(&c->receipt,0,sizeof(c->receipt));
    c->promote_pending=0;
    return SS_OK;
}
int ss_camera_begin_latest(void *user,ss_identity id,uint64_t started){
    int rc=ss_camera_begin(user,id,started);
    if(rc==SS_OK)((ss_camera*)user)->promote_pending=1;
    return rc;
}
int ss_camera_poll(void *user,ss_identity id,ss_capture_pair *pair){
    ss_camera *c=user;
    if(!c || !pair || !c->request_owned || !same(id,c->request))return SS_STALE;
    if(c->request_error)return c->request_error;
    if(!c->request_ready)return SS_PENDING;
    *pair=(ss_capture_pair){id,c->receipt.locked_us,{c->config.rectified[0],c->config.rectified[1]},SS_PIXELS*3u,
        {c->config.roi[0],c->config.roi[1]},SS_PIXELS};return SS_OK;
}
void ss_camera_cancel(void *user,ss_identity id){
    ss_camera *c=user;if(!c || !c->request_owned || !same(id,c->request))return;
    c->request_cancelled=1;if(!c->request_error)c->request_error=SS_CANCELLED;
    (void)close_work(c);
    /* Ordinary cancellation lets an accepted DMA finish and release by its
     * actual descriptor. rc_cancel alone would quarantine, not prove drain. */
}
int ss_camera_release(void *user,ss_identity id){
    ss_camera *c=user;
    if(!c || !c->request_owned || !same(id,c->request))return SS_STALE;
    if(c->raw_shot && c->imported_raw && c->phase==SS_CAM_QUARANTINE && !close_work(c)){
        c->raw_shot=c->imported_raw=0;
    }
    if(c->raw_shot)return SS_PENDING;
    if(!c->request_ready && !c->request_cancelled && !c->request_error)return SS_PENDING;
    c->request_owned=c->request_pending=c->request_ready=0;return SS_OK;
}
int ss_camera_enable_video(ss_camera *c,uint8_t *left,uint8_t *right,size_t bytes){
    const void *v[17];size_t n[17];unsigned i,j;
    if(!c || !c->initialized || c->external_preview || c->request_owned || c->phase!=SS_CAM_IDLE ||
       c->preview_ready || c->preview_loan || bytes!=RC_FRAME_BYTES)return SS_ARGUMENT;
    v[0]=c;n[0]=sizeof(*c);v[1]=c->config.device;n[1]=sizeof(rc_device);
    v[2]=c->config.calibration;n[2]=sizeof(ss_calibration);
    v[3]=c->config.calibration->blob;n[3]=SS_CALIBRATION_BYTES;
    v[4]=c->config.raw_rgb;v[5]=c->config.right_raw_rgb;n[4]=n[5]=SS_RAW_RGB_BYTES;
    v[6]=c->config.rectified[0];v[7]=c->config.rectified[1];n[6]=n[7]=SS_PIXELS*3u;
    v[8]=c->config.roi[0];v[9]=c->config.roi[1];n[8]=n[9]=SS_PIXELS;
    v[10]=c->config.preview_packet;n[10]=c->config.preview_capacity;
    for(i=0;i<4;i++){v[11+i]=c->config.device->buffers[i/2][i%2];n[11+i]=RC_FRAME_BYTES;}
    v[15]=left;v[16]=right;n[15]=n[16]=bytes;
    for(i=15;i<17;i++){
        if(!span(v[i],n[i]) || ((uintptr_t)v[i]&63u))return SS_ARGUMENT;
        for(j=0;j<i;j++)if(overlap(v[i],n[i],v[j],n[j]))return SS_ARGUMENT;
    }
    c->external_raw[0]=left;c->external_raw[1]=right;c->external_preview=1;return SS_OK;
}
static void preview_header(ss_camera *c){
    uint8_t *p=c->config.preview_packet;const ss_calibration *k=c->config.calibration;
    memset(p,0,96);memcpy(p,"SDP1",4);p[4]=1;p[5]=1;be16(p+6,96);
    be32(p+8,k->full_range?SS_RAW_RGB_BYTES:RC_FRAME_BYTES);be32(p+12,c->preview_crc^UINT32_MAX);
    be64(p+16,c->config.boot);be64(p+24,c->preview_sequence);be32(p+32,c->config.device->epoch[0]);
    be64(p+48,c->raw_ready_us);be16(p+76,640);be16(p+78,480);be16(p+80,k->full_range?1:2);
}
static void open_preprocess(ss_camera *c){
    if(c->request_cancelled)c->phase=SS_CAM_RELEASE;
    else{
        memset(c->work,0,sizeof(c->work));c->phase=SS_CAM_PREPROCESS;
        __atomic_store_n(&c->work_gate,WORK_OPEN,__ATOMIC_RELEASE);
        if(c->config.notify)c->config.notify(c->config.notify_user);
    }
}
static void preprocess(ss_camera *c){
    rc_device *d=c->config.device;ss_camera_receipt *r=&c->receipt;
    c->raw_shot=1;c->request_pending=c->promote_pending=0;
    r->identity=c->request;r->raw_arm_us=d->started_us[0];r->raw_ready_us=c->raw_ready_us;r->capture_epoch=d->epoch[0];
    r->source_id[0]=d->descriptor[0][17];r->source_id[1]=d->descriptor[0][33];
    r->sof_ticks[0]=d->descriptor[0][18];r->sof_ticks[1]=d->descriptor[0][34];
    r->skew_ticks=d->descriptor[0][7];r->calibration_crc=c->config.calibration->header_crc;
    c->imported_raw=0;open_preprocess(c);
}
int ss_camera_copy_begin(ss_camera *c,unsigned slot,uint64_t ready){
    rc_device *d;ss_camera_receipt *r;
    if(!c || !c->external_preview || slot>1u)return SS_ARGUMENT;
    if(!c->request_owned || !c->request_pending || c->request_cancelled || c->error ||
       c->phase!=SS_CAM_IDLE || c->raw_shot)return SS_PENDING;
    d=c->config.device;r=&c->receipt;
    if(d->ownership[slot]!=2u || ready<d->started_us[slot] || ready>d->io.now_us(d->io.user))return SS_STALE;
    r->identity=c->request;r->raw_arm_us=d->started_us[slot];r->raw_ready_us=ready;r->capture_epoch=d->epoch[slot];
    r->source_id[0]=d->descriptor[slot][17];r->source_id[1]=d->descriptor[slot][33];
    r->sof_ticks[0]=d->descriptor[slot][18];r->sof_ticks[1]=d->descriptor[slot][34];
    r->skew_ticks=d->descriptor[slot][7];r->calibration_crc=c->config.calibration->header_crc;
    c->raw_ready_us=ready;c->raw_shot=c->imported_raw=1;c->request_pending=c->promote_pending=0;
    c->phase=SS_CAM_EXTERNAL_COPY;return SS_OK;
}
int ss_camera_copy_finish(ss_camera *c){
    rc_device *d;
    if(!c || c->phase!=SS_CAM_EXTERNAL_COPY || !c->raw_shot || !c->imported_raw)return SS_STALE;
    d=c->config.device;
    /* CPU0 wrote the copy. Publish RIGHT to DDR before CPU1 acquires it;
     * LEFT stays CPU0-owned. Work admission is opened only after full copy. */
    d->io.flush(d->io.user,c->external_raw[1],RC_FRAME_BYTES);d->io.barrier(d->io.user);
    open_preprocess(c);return SS_OK;
}
void ss_camera_copy_abort(ss_camera *c,int reason){
    if(c && c->phase==SS_CAM_EXTERNAL_COPY){
        c->request_error=reason;c->request_cancelled=1;c->phase=SS_CAM_RELEASE;
    }
}
int ss_camera_worker_step(ss_camera *c,unsigned side){
    uint32_t gate,bit,state;ss_camera_work *w;const ss_camera_config *cfg;
    rc_device *d;uint8_t *rgb,*raw;unsigned take;int rc=0,result=SS_OK;
    if(!c || !c->initialized || side>1u)return SS_ARGUMENT;
    bit=WORK_CPU0<<side;gate=__atomic_load_n(&c->work_gate,__ATOMIC_ACQUIRE);
    for(;;){
        if(!(gate&WORK_OPEN) || (gate&bit))return SS_PENDING;
        if(__atomic_compare_exchange_n(&c->work_gate,&gate,gate|bit,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))break;
    }
    /* Read this shot's state only AFTER entering. A late wake cannot retain
     * a pointer/row from the previous shot across close, drain and reopen. */
    cfg=&c->config;d=cfg->device;w=c->work+side;
    rgb=side?cfg->right_raw_rgb:cfg->raw_rgb;
    raw=c->imported_raw?c->external_raw[side]:d->buffers[0][side];
    state=__atomic_load_n(&w->phase,__ATOMIC_ACQUIRE);
    if(state==SS_CAM_WORK_ACQUIRE){
        /* rc_poll acquired DMA on CPU0. CPU1 must also invalidate its own
         * stale L1 lines from an earlier shot, using the shared PL310 lock. */
        if(side)d->io.invalidate(d->io.user,raw,RC_FRAME_BYTES);
        d->io.barrier(d->io.user);
        __atomic_store_n(&w->phase,SS_CAM_WORK_CONVERT,__ATOMIC_RELEASE);
    }else if(state==SS_CAM_WORK_CONVERT){
        take=SS_RAW_HEIGHT-w->row;if(take>4u)take=4u;
        rc=r14_yuyv_rgb(raw+w->row*1280u,take*1280u,
                640,take,1280,(int)cfg->calibration->full_range,rgb+w->row*1920u,w->gray_rows);
        if(!rc){
            w->row+=take;
            if(w->row==SS_RAW_HEIGHT){w->row=0;__atomic_store_n(&w->phase,SS_CAM_WORK_RECTIFY,__ATOMIC_RELEASE);}
        }
    }else if(state==SS_CAM_WORK_RECTIFY){
        take=SS_HEIGHT-w->row;if(take>4u)take=4u;
        rc=r14_rectify_rgb(rgb,640,480,cfg->calibration->map[side]+w->row*SS_WIDTH*2u,SS_WIDTH,take,
                cfg->rectified[side]+w->row*SS_WIDTH*3u,cfg->roi[side]+w->row*SS_WIDTH);
        if(!rc){
            d->io.flush(d->io.user,cfg->rectified[side]+w->row*SS_WIDTH*3u,take*SS_WIDTH*3u);
            d->io.flush(d->io.user,cfg->roi[side]+w->row*SS_WIDTH,take*SS_WIDTH);
            d->io.barrier(d->io.user);w->row+=take;
            __atomic_store_n(&w->ready_rows,w->row,__ATOMIC_RELEASE);
            if(w->row==SS_HEIGHT)__atomic_store_n(&w->phase,SS_CAM_WORK_DONE,__ATOMIC_RELEASE);
        }
    }else if(state==SS_CAM_WORK_DONE)result=SS_PENDING;
    else rc=SS_ARGUMENT;
    if(rc){__atomic_store_n(&w->error,SS_DATA,__ATOMIC_RELEASE);result=SS_DATA;}
    __atomic_fetch_and(&c->work_gate,~bit,__ATOMIC_RELEASE);
    return result;
}
int ss_camera_step(ss_camera *c){
    rc_device *d;const ss_calibration *k;unsigned take;uint64_t now;int rc;
    if(!c || !c->initialized)return SS_ARGUMENT;
    d=c->config.device;k=c->config.calibration;now=d->io.now_us(d->io.user);
    if(now<c->last_us)return fault(c,SS_CLOCK,0);
    c->last_us=now;
    if(c->error)return c->error;
    if(c->request_pending && c->request_cancelled)c->request_pending=0;
    if(!c->external_preview && c->request_pending && c->promote_pending && !c->raw_shot && d->ownership[0]==2u &&
       (c->phase==SS_CAM_CONVERT || c->phase==SS_CAM_PREVIEW_COPY || c->phase==SS_CAM_RELEASE)){
        /* Raw data is already descriptor-validated and acquired. Its
         * unpublished preview copy can be dropped; any network loan is
         * a different, already complete packet and cannot be in this path. */
        preprocess(c);return SS_PENDING;
    }
    if(c->raw_shot && c->request_cancelled && c->phase!=SS_CAM_WAIT && c->phase!=SS_CAM_RELEASE && c->phase!=SS_CAM_EXTERNAL_COPY){
        if(close_work(c))return SS_PENDING;
        c->phase=SS_CAM_RELEASE;
    }
    switch(c->phase){
    case SS_CAM_IDLE:
        if(c->external_preview){
            if(d->first_error && c->request_pending)return fault(c,SS_IO,d->first_error);
            return SS_PENDING;
        }
        if(!c->request_pending && (c->preview_ready || c->preview_loan))return SS_PENDING;
        c->raw_shot=c->request_pending;
        if(!c->raw_shot && c->preview_sequence==UINT64_MAX)return fault(c,SS_EXHAUSTED,0);
        rc=rc_arm(d,0);
        if(rc){
            if(!d->ownership[0] && rc==RN_BUSY_ERROR && !d->first_error){c->raw_shot=0;return SS_PENDING;}
            if(!d->ownership[0])c->raw_shot=0;
            return fault(c,SS_IO,rc);
        }
        if(c->raw_shot){c->request_pending=0;c->receipt.identity=c->request;c->receipt.raw_arm_us=d->started_us[0];}
        else {c->preview_sequence++;memset(c->config.preview_packet,0,96);}
        c->phase=SS_CAM_WAIT;return SS_PENDING;
    case SS_CAM_WAIT:
        rc=rc_poll(d,0);if(rc==RN_PENDING)return SS_PENDING;
        if(rc)return fault(c,SS_IO,rc);
        c->raw_ready_us=d->io.now_us(d->io.user);c->side=c->row=0;
        if(c->raw_shot || (c->request_pending && c->promote_pending))preprocess(c);
        else{c->preview_crc=UINT32_MAX;c->phase=k->full_range?SS_CAM_CONVERT:SS_CAM_PREVIEW_COPY;}
        return SS_PENDING;
    case SS_CAM_CONVERT:
        take=SS_RAW_HEIGHT-c->row;if(take>4u)take=4u;
        rc=r14_yuyv_rgb((const uint8_t*)d->buffers[0][c->side]+c->row*1280u,take*1280u,640,take,1280,(int)k->full_range,
                        c->config.raw_rgb+c->row*1920u,c->work[0].gray_rows);
        if(rc)return fault(c,SS_DATA,0);
        c->row+=take;if(c->row==SS_RAW_HEIGHT){c->row=0;c->phase=SS_CAM_PREVIEW_COPY;}
        return SS_PENDING;
    case SS_CAM_PREPROCESS:
        rc=ss_camera_worker_step(c,0);
        if(rc<0 || __atomic_load_n(&c->work[1].error,__ATOMIC_ACQUIRE))return fault(c,SS_DATA,0);
        if(__atomic_load_n(&c->work[0].phase,__ATOMIC_ACQUIRE)==SS_CAM_WORK_DONE &&
           __atomic_load_n(&c->work[1].phase,__ATOMIC_ACQUIRE)==SS_CAM_WORK_DONE && !close_work(c)){
            d->io.barrier(d->io.user);c->receipt.locked_us=d->io.now_us(d->io.user);c->phase=SS_CAM_RELEASE;
        }
        return SS_PENDING;
    case SS_CAM_PREVIEW_COPY:{
        unsigned stride=k->full_range?1920u:1280u;const uint8_t *src=k->full_range?c->config.raw_rgb:d->buffers[0][0];
        take=SS_RAW_HEIGHT-c->row;if(take>4u)take=4u;
        memcpy(c->config.preview_packet+96u+c->row*stride,src+c->row*stride,take*stride);
        c->preview_crc=ss_crc32_update(c->preview_crc,c->config.preview_packet+96u+c->row*stride,take*stride);
        c->row+=take;if(c->row==SS_RAW_HEIGHT)c->phase=SS_CAM_RELEASE;return SS_PENDING;}
    case SS_CAM_RELEASE:
        if(close_work(c))return SS_PENDING;
        if(!c->imported_raw){rc=rc_release(d,0);if(rc)return fault(c,SS_IO,rc);}
        if(c->raw_shot){if(!c->request_cancelled)c->request_ready=1;c->raw_shot=0;}
        else {preview_header(c);c->preview_ready=1;}
        c->imported_raw=0;c->phase=SS_CAM_IDLE;return SS_OK;
    case SS_CAM_EXTERNAL_COPY:return SS_PENDING;
    default:return fault(c,SS_ARGUMENT,0);
    }
}
int ss_camera_preview_acquire(ss_camera *c,const uint8_t **packet,size_t *bytes){
    if(!c || !c->initialized || !packet || !bytes)return SS_ARGUMENT;
    *packet=NULL;*bytes=0;if(c->preview_loan)return SS_BUSY;
    if(!c->preview_ready)return SS_PENDING;
    c->preview_loan=1;c->preview_ready=0;*packet=c->config.preview_packet;
    *bytes=96u+(c->config.calibration->full_range?SS_RAW_RGB_BYTES:RC_FRAME_BYTES);return SS_OK;
}
void ss_camera_preview_release(ss_camera *c){if(c)c->preview_loan=0;}
