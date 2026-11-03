#include "board_runtime.h"
#include <string.h>

enum { OWNER_NONE,OWNER_STATUS,OWNER_PAIR };
static int same(ss_identity a,ss_identity b){return a.boot==b.boot && a.shot==b.shot && a.epoch==b.epoch && a.attempt==b.attempt;}
static void send_ready(ss_board *);
static int overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;}
static void be32(uint8_t *p,uint32_t n){unsigned i;for(i=0;i<4u;i++)p[i]=(uint8_t)(n>>(24u-8u*i));}
static void be64(uint8_t *p,uint64_t n){be32(p,(uint32_t)(n>>32));be32(p+4,(uint32_t)n);}
static void status(ss_board *b,ss_identity id,unsigned code,int error){
    uint8_t *p=b->pending_status;memset(p,0,96);memcpy(p,"SDP1",4);p[4]=1;p[5]=3;p[7]=96;
    be64(p+16,id.boot);be64(p+24,id.shot);be32(p+32,id.epoch);be32(p+36,id.attempt);
    be32(p+72,11459);be32(p+84,code);be32(p+88,error<0?0u-(uint32_t)error:(uint32_t)error);b->status_pending=1;
}
static void changed(void *user,enum ss_flow_state state,ss_identity id,int error){
    ss_board *b=user;(void)state;(void)id;
    /* Producer progress is independent of a user's freeze request. Do not
     * mislabel every background frame as a new button press or transfer. */
    if(error){b->waiting_result=0;status(b,b->send_identity,6,error);}
}
static void service_once(void *user){
    ss_board *b=user;const uint8_t *packet;size_t bytes;int rc;
    b->config.network_service(b->config.network_user);
    if(b->video_enabled){
        rc=ss_video_step(&b->video,b->config.preview->peer || b->config.camera->request_pending);
        if(rc<0)b->camera_error=rc;
    }
    rc=ss_camera_step(b->config.camera);if(rc<0)b->camera_error=rc;
    (void)sdp_tcp_step(b->config.preview);
    if(b->preview_loan && sdp_tcp_result(b->config.preview)!=SDP_PENDING){
        if(b->video_enabled)ss_video_release(&b->video);else ss_camera_preview_release(b->config.camera);
        b->preview_loan=0;
    }
    if(!b->preview_loan && b->config.preview->peer){
        rc=b->video_enabled?ss_video_acquire(&b->video,&packet,&bytes):ss_camera_preview_acquire(b->config.camera,&packet,&bytes);
        if(rc==SS_OK){
            rc=sdp_tcp_begin(b->config.preview,packet,bytes);
            if(rc==SDP_OK)b->preview_loan=1;
            else {if(b->video_enabled)ss_video_release(&b->video);else ss_camera_preview_release(b->config.camera);if(rc<0)b->network_error=rc;}
        }
    }
    (void)sdp_tcp_step(b->config.result);
    if(b->result_owner==OWNER_STATUS && sdp_tcp_result(b->config.result)!=SDP_PENDING)b->result_owner=OWNER_NONE;
    if(b->status_pending && b->result_owner==OWNER_NONE && b->latest.freeze!=SS_FREEZE_READY){
        memcpy(b->sending_status,b->pending_status,96);
        rc=sdp_tcp_begin(b->config.result,b->sending_status,96);
        if(rc==SDP_OK){b->result_owner=OWNER_STATUS;b->status_pending=0;}
        else if(rc<0)b->network_error=rc;
    }
    send_ready(b); /* Retired status must not defer a ready result past CPU work. */
}
static void service(void *user){
    ss_board *b=user;uint64_t start=b->config.model->config.now_us(b->config.model->config.clock_user);unsigned i;
    /* Bounded foreground video/network budget, still followed by a real
     * model step. No lwIP API runs concurrently on CPU1 or in an ISR. */
    for(i=0;i<(b->video_enabled?8u:1u);i++){
        service_once(user);
        if(b->config.model->config.now_us(b->config.model->config.clock_user)-start>=500u)break;
    }
}
static int capture_begin(void *user,ss_identity id,uint64_t trigger){return ss_camera_begin_latest(((ss_board*)user)->config.camera,id,trigger);}
static int capture_poll(void *user,ss_identity id,ss_capture_pair *pair){return ss_camera_poll(((ss_board*)user)->config.camera,id,pair);}
static void capture_cancel(void *user,ss_identity id){ss_camera_cancel(((ss_board*)user)->config.camera,id);}
static int capture_release(void *user,ss_identity id){return ss_camera_release(((ss_board*)user)->config.camera,id);}
static int send_begin(void *user,ss_identity id,const uint8_t *packet,size_t bytes){
    ss_board *b=user;int rc;(void)id;
    rc=ss_latest_publish(&b->latest,packet,bytes,b->config.camera->receipt.raw_arm_us,b->config.camera->receipt.raw_ready_us);
    /* Publication ended the producer loan. Later error cleanup must not
     * clear the committed slot, which may already be pinned for sending. */
    if(rc==SS_OK){
        b->flow.exporter.packet=NULL;
        if(b->waiting_result){
            uint64_t now=b->config.model->config.now_us(b->config.model->config.clock_user);
            int selected=ss_latest_freeze(&b->latest,b->send_identity,b->request_pressed_us,now);
            b->waiting_result=0;
            if(selected==SS_OK){status(b,b->send_identity,4,0);send_ready(b);}
            else {b->network_error=selected;status(b,b->send_identity,6,selected);}
        }
    }
    return rc;
}
static int send_poll(void *user,ss_identity id){
    ss_board *b=user;
    return same(id,b->flow.identity)?SS_OK:SS_STALE;
}
static void send_cancel(void *user,ss_identity id){
    (void)user;(void)id; /* Already committed locally; no live producer loan. */
}
int ss_board_init(ss_board *b,const ss_board_config *cfg){
    ss_flow_config flow;const ss_camera_config *c;const fw_cache *p;unsigned i,j;int rc;
    const void *v[20];size_t n[20];
    if(!b || !cfg || b->initialized || !cfg->model || !cfg->model->initialized || cfg->model->active ||
       !cfg->camera || !cfg->camera->initialized || cfg->camera->request_owned || cfg->camera->preview_loan ||
       !cfg->preview || !cfg->preview->listener || cfg->preview->kind!=1u || cfg->preview->active ||
       !cfg->result || !cfg->result->listener || cfg->result->kind!=2u || cfg->result->active ||
       !cfg->network_service || !cfg->result_packet || cfg->result_capacity<SS_FROZEN_PACKET_BYTES ||
       overlap(b,sizeof(*b),cfg,sizeof(*cfg)))return SS_ARGUMENT;
    c=&cfg->camera->config;p=&cfg->model->config.cache;
    if(c->device->io.flush!=p->clean || c->device->io.invalidate!=p->invalidate ||
       c->device->io.barrier!=p->barrier || c->device->io.user!=p->user ||
       c->device->io.now_us!=cfg->model->config.now_us || c->device->io.user!=cfg->model->config.clock_user)return SS_ARGUMENT;
    v[0]=b;n[0]=sizeof(*b);v[1]=cfg->camera;n[1]=sizeof(*cfg->camera);v[2]=c->device;n[2]=sizeof(*c->device);
    v[3]=c->raw_rgb;n[3]=c->raw_rgb_bytes;v[4]=c->rectified[0];v[5]=c->rectified[1];n[4]=n[5]=c->rectified_bytes;
    v[6]=c->roi[0];v[7]=c->roi[1];n[6]=n[7]=c->roi_bytes;v[8]=c->preview_packet;n[8]=c->preview_capacity;
    v[9]=cfg->preview;n[9]=sizeof(*cfg->preview);v[10]=cfg->result;n[10]=sizeof(*cfg->result);
    for(i=0;i<4u;i++){v[11u+i]=c->device->buffers[i/2u][i%2u];n[11u+i]=RC_FRAME_BYTES;}
    v[15]=c->calibration;n[15]=sizeof(*c->calibration);v[16]=c->calibration->blob;n[16]=SS_CALIBRATION_BYTES;
    v[17]=c->right_raw_rgb;n[17]=c->raw_rgb_bytes;
    v[18]=cfg->result_slot[0];v[19]=cfg->result_slot[1];n[18]=n[19]=SS_FROZEN_PACKET_BYTES;
    for(i=0;i<20u;i++){
        if(!ss_model_packet_private(cfg->model,v[i],n[i]) || overlap(cfg->result_packet,SS_FROZEN_PACKET_BYTES,v[i],n[i]))return SS_ARGUMENT;
        for(j=0;j<i;j++)if(overlap(v[i],n[i],v[j],n[j]))return SS_ARGUMENT;
    }
    memset(b,0,sizeof(*b));b->config=*cfg;memset(&flow,0,sizeof(flow));
    rc=ss_latest_init(&b->latest,cfg->result_slot[0],cfg->result_slot[1],cfg->result_packet,cfg->result_capacity);if(rc)return rc;
    flow.model=cfg->model;flow.packet=cfg->result_slot[0];flow.packet_capacity=cfg->result_capacity;
    flow.boot=c->boot;flow.epoch=cfg->epoch;flow.debounce_us=cfg->debounce_us;
    flow.io=(ss_flow_io){b,service,capture_begin,capture_poll,capture_cancel,capture_release,send_begin,send_poll,send_cancel,changed};
    rc=ss_flow_init(&b->flow,&flow);if(rc)return rc;
    b->send_identity.boot=c->boot;b->send_identity.epoch=b->send_identity.attempt=1;
    b->initialized=1;status(b,b->send_identity,0,0);return SS_OK;
}
int ss_board_video_enable(ss_board *b,uint8_t *second,size_t bytes,uint8_t *left,uint8_t *right){
    ss_camera *c;const void *v[15];size_t n[15];unsigned i;int rc;
    if(!b || !b->initialized || b->video_enabled || b->flow.state!=SS_FLOW_IDLE || !second || bytes<SS_PREVIEW_MAX_BYTES)return SS_ARGUMENT;
    c=b->config.camera;
    if(c->request_owned || c->phase!=SS_CAM_IDLE || c->preview_ready || c->preview_loan ||
       !ss_model_packet_private(b->config.model,second,bytes) ||
       !ss_model_packet_private(b->config.model,left,RC_FRAME_BYTES) ||
       !ss_model_packet_private(b->config.model,right,RC_FRAME_BYTES))return SS_ARGUMENT;
    v[0]=b;n[0]=sizeof(*b);v[1]=c;n[1]=sizeof(*c);
    v[2]=c->config.raw_rgb;v[3]=c->config.right_raw_rgb;n[2]=n[3]=SS_RAW_RGB_BYTES;
    v[4]=c->config.rectified[0];v[5]=c->config.rectified[1];n[4]=n[5]=SS_PIXELS*3u;
    v[6]=c->config.roi[0];v[7]=c->config.roi[1];n[6]=n[7]=SS_PIXELS;
    v[8]=c->config.calibration; n[8]=sizeof(*c->config.calibration);
    v[9]=c->config.calibration->blob;n[9]=SS_CALIBRATION_BYTES;
    v[10]=b->config.preview;v[11]=b->config.result;n[10]=n[11]=sizeof(sdp_tcp);
    v[12]=b->config.result_packet;v[13]=b->config.result_slot[0];v[14]=b->config.result_slot[1];n[12]=n[13]=n[14]=SS_FROZEN_PACKET_BYTES;
    for(i=0;i<15;i++)if(overlap(second,bytes,v[i],n[i]) || overlap(left,RC_FRAME_BYTES,v[i],n[i]) ||
        overlap(right,RC_FRAME_BYTES,v[i],n[i]))return SS_ARGUMENT;
    if(overlap(second,bytes,left,RC_FRAME_BYTES) || overlap(second,bytes,right,RC_FRAME_BYTES))return SS_ARGUMENT;
    rc=ss_camera_enable_video(c,left,right,RC_FRAME_BYTES);if(rc)return rc;
    rc=ss_video_init(&b->video,c->config.device,c->config.boot,c->config.calibration->full_range,c->config.preview_packet,second,bytes);
    if(rc){c->external_preview=0;c->external_raw[0]=c->external_raw[1]=NULL;return rc;}
    b->video.camera=c;b->video_enabled=1;return SS_OK;
}
static void freeze_key(ss_board *b,int pressed,uint64_t now){
    int rc;pressed=!!pressed;
    if(!b->key_initialized){b->key_initialized=1;b->raw_pressed=b->stable_pressed=pressed;b->pressed_since_us=now;return;}
    if(pressed!=b->raw_pressed){b->raw_pressed=pressed;b->pressed_since_us=now;}
    if(now<b->pressed_since_us || now-b->pressed_since_us<b->config.debounce_us)return;
    if(!pressed){b->stable_pressed=0;b->key_armed=1;return;}
    if(b->stable_pressed)return;
    b->stable_pressed=1;if(!b->key_armed)return;b->key_armed=0;
    if(b->waiting_result || b->latest.freeze!=SS_FREEZE_IDLE){b->busy_presses++;status(b,b->send_identity,5,0);return;}
    if(b->latest.latest<0 && b->producer_error){status(b,b->send_identity,6,b->producer_error);return;}
    if(b->requests==UINT64_MAX){b->network_error=SS_EXHAUSTED;return;}
    b->send_identity.shot=++b->requests;
    b->request_pressed_us=b->pressed_since_us;
    /* No busy wait, new capture or inference restart. Publication itself
     * will pin the first complete result for this one accepted request. */
    if(b->latest.latest<0){b->waiting_result=1;status(b,b->send_identity,2,0);return;}
    rc=ss_latest_freeze(&b->latest,b->send_identity,b->request_pressed_us,now);
    if(rc<0){b->network_error=rc;status(b,b->send_identity,6,rc);}
    else status(b,b->send_identity,4,0);
}
static void send_ready(ss_board *b){
    const uint8_t *packet;size_t bytes;
    if(b->result_owner==OWNER_NONE && ss_latest_packet(&b->latest,&packet,&bytes)==SS_OK){
        int started=sdp_tcp_begin(b->config.result,packet,bytes);
        if(started==SDP_OK){b->result_owner=OWNER_PAIR;(void)sdp_tcp_step(b->config.result);}
        else if(started<0)b->network_error=started;
    }
}
int ss_board_step(ss_board *b,int pressed){
    int rc;uint64_t now;uint8_t *writer;
    if(!b || !b->initialized)return SS_ARGUMENT;
    now=b->config.model->config.now_us(b->config.model->config.clock_user);
    freeze_key(b,pressed,now);
    send_ready(b);
    /* Starting a producer is unrelated to the key and never waits for TCP.
     * Three slots always leave a writer apart from latest and pinned. */
    if(!b->producer_error && b->flow.state==SS_FLOW_IDLE){
        rc=ss_latest_write(&b->latest,&writer);
        if(rc==SS_OK){
            b->flow.config.packet=writer;
            rc=ss_flow_start(&b->flow,now);
            if(rc<0){ss_latest_discard(&b->latest);b->producer_error=rc;}
        }
    }
    rc=ss_flow_step(&b->flow,0);
    if(rc<0){b->producer_error=rc;if(b->flow.state==SS_FLOW_IDLE)ss_latest_discard(&b->latest);}
    if(b->producer_error && b->waiting_result){b->waiting_result=0;status(b,b->send_identity,6,b->producer_error);}
    now=b->config.model->config.now_us(b->config.model->config.clock_user);
    if(b->result_owner==OWNER_PAIR){
        int sent=sdp_tcp_result(b->config.result);
        if(sent!=SDP_PENDING){
            b->result_owner=OWNER_NONE;ss_latest_sent(&b->latest);
            if(sent==SDP_OK){b->delivered++;b->transport_done_us=now;status(b,b->send_identity,0,0);}
            else {b->network_error=sent;status(b,b->send_identity,6,sent);}
        }
    }
    send_ready(b);
    return rc;
}
