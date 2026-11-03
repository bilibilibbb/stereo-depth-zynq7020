#include "snapshot_flow.h"
#include <string.h>

static int same(ss_identity a,ss_identity b){return a.boot==b.boot && a.shot==b.shot && a.epoch==b.epoch && a.attempt==b.attempt;}
static int overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;}
static void state(ss_flow *s,enum ss_flow_state next){
    if(s->state==next)return;
    s->state=next;
    if(s->config.io.state_changed)s->config.io.state_changed(s->config.io.user,next,s->identity,s->error);
}
int ss_flow_init(ss_flow *s,const ss_flow_config *c){
    const ss_flow_io *io;
    if(!s || !c || s->initialized || !c->model || !c->model->initialized || c->model->active ||
       !c->boot || !c->epoch || c->epoch==UINT32_MAX || !c->debounce_us || !c->packet || c->packet_capacity<SS_PACKET_BYTES ||
       !ss_model_packet_private(c->model,c->packet,SS_PACKET_BYTES) ||
       overlap(c->packet,SS_PACKET_BYTES,s,sizeof(*s)) ||
       overlap(c->model,sizeof(*c->model),s,sizeof(*s)))return SS_ARGUMENT;
    io=&c->io;
    if(!io->service || !io->capture_begin || !io->capture_poll || !io->capture_cancel ||
       !io->capture_release || !io->send_begin || !io->send_poll || !io->send_cancel)return SS_ARGUMENT;
    /* Copy before clearing permits a separately supplied config only. */
    if(overlap(s,sizeof(*s),c,sizeof(*c)))return SS_ARGUMENT;
    memset(s,0,sizeof(*s));s->config=*c;s->identity.boot=c->boot;s->identity.epoch=c->epoch;
    s->initialized=1;return SS_OK;
}
void ss_flow_abort(ss_flow *s,int reason){
    if(!s || !s->initialized || s->state==SS_FLOW_IDLE || s->state==SS_FLOW_FAULT)return;
    if(!s->error)s->error=reason<0?reason:SS_CANCELLED;
    state(s,SS_FLOW_DRAIN);
}
int ss_flow_start(ss_flow *s,uint64_t started){
    if(!s || !s->initialized || started>s->config.model->config.now_us(s->config.model->config.clock_user))return SS_ARGUMENT;
    if(s->state!=SS_FLOW_IDLE)return SS_BUSY;
    if(s->identity.shot==UINT64_MAX || (s->identity.shot && s->identity.epoch>=UINT32_MAX-1u)){
        s->error=SS_EXHAUSTED;state(s,SS_FLOW_FAULT);return SS_EXHAUSTED;
    }
    /* The original job rejects epoch reuse even for a different frame ID.
     * Consume a generation for every accepted photo, including later failed
     * captures. Busy/debounced presses never change it; no automatic retry. */
    if(s->identity.shot)s->identity.epoch++;
    s->identity.attempt=1u;
    s->identity.shot++;s->trigger_us=started;s->error=0;
    /* A later aborted producer must not invalidate a previously published
     * packet before the new frame has even acquired its private output. */
    memset(&s->exporter,0,sizeof(s->exporter));
    s->capture_cancelled=s->send_cancelled=0;s->transport_done_us=0;s->accepted++;
    state(s,SS_FLOW_CAPTURE_START);
    return SS_OK;
}
static void key(ss_flow *s,int pressed,uint64_t now){
    pressed=!!pressed;
    if(!s->key_initialized){s->key_initialized=1;s->raw_pressed=s->stable_pressed=pressed;s->raw_since_us=now;return;}
    if(pressed!=s->raw_pressed){s->raw_pressed=pressed;s->raw_since_us=now;}
    if(now<s->raw_since_us || now-s->raw_since_us<s->config.debounce_us)return;
    if(!pressed){s->stable_pressed=0;s->key_armed=1;return;}
    if(s->stable_pressed)return;
    s->stable_pressed=1;
    if(!s->key_armed)return;
    s->key_armed=0;
    if(s->state!=SS_FLOW_IDLE){s->busy_presses++;return;}
    (void)ss_flow_start(s,s->raw_since_us);
}
static int drain(ss_flow *s){
    const ss_flow_io *io=&s->config.io;ss_model *m=s->config.model;int rc;
    if(s->send_owned){
        if(!s->send_cancelled){io->send_cancel(io->user,s->identity);s->send_cancelled=1;}
        rc=io->send_poll(io->user,s->identity);
        if(rc==SS_PENDING)return SS_PENDING;
        s->send_owned=0;
    }
    if(m->active){ss_model_cancel(m,s->error);(void)ss_model_step(m);if(m->active)return SS_PENDING;}
    if(s->capture_owned){
        if(!s->capture_cancelled){io->capture_cancel(io->user,s->identity);s->capture_cancelled=1;}
        rc=io->capture_release(io->user,s->identity);
        if(rc!=SS_OK)return SS_PENDING;
        s->capture_owned=0;
    }
    /* A sender can retain a zero-copy loan through cancel. Only now may its
     * packet be invalidated; an error or timeout alone never permits reuse. */
    ss_pair_cancel(&s->exporter);s->failed++;state(s,SS_FLOW_IDLE);return s->error;
}
int ss_flow_step(ss_flow *s,int pressed){
    const ss_flow_io *io;ss_model *m;const uint8_t *packet;size_t bytes;uint64_t now;int rc;
    if(!s || !s->initialized)return SS_ARGUMENT;
    io=&s->config.io;m=s->config.model;io->service(io->user);
    now=m->config.now_us(m->config.clock_user);
    if(now<s->last_us){
        if(s->state==SS_FLOW_IDLE){s->error=SS_CLOCK;state(s,SS_FLOW_FAULT);}
        else ss_flow_abort(s,SS_CLOCK);
    }else s->last_us=now;
    key(s,pressed,s->last_us);
    switch(s->state){
    case SS_FLOW_IDLE:return SS_OK;
    case SS_FLOW_FAULT:return s->error;
    case SS_FLOW_CAPTURE_START:
        rc=io->capture_begin(io->user,s->identity,s->trigger_us);
        if(rc==SS_OK){s->capture_owned=1;state(s,SS_FLOW_CAPTURE);}
        else if(rc!=SS_PENDING)ss_flow_abort(s,rc);
        break;
    case SS_FLOW_CAPTURE:
        rc=io->capture_poll(io->user,s->identity,&s->capture);
        if(rc==SS_OK){
            if(!same(s->capture.identity,s->identity) || !s->capture.rgb[0] || !s->capture.rgb[1] ||
               s->capture.rgb_bytes!=SS_PIXELS*3u || !s->capture.roi[0] || !s->capture.roi[1] ||
               s->capture.roi_pixels!=SS_PIXELS || s->capture.locked_us<s->trigger_us ||
               s->capture.locked_us>m->config.now_us(m->config.clock_user))ss_flow_abort(s,SS_STALE);
            else state(s,SS_FLOW_MODEL_START);
        }else if(rc!=SS_PENDING)ss_flow_abort(s,rc);
        break;
    case SS_FLOW_MODEL_START:
        rc=ss_model_start(m,s->identity,s->trigger_us,s->capture.locked_us,s->capture.rgb[0],s->capture.rgb[1]);
        if(rc==SS_OK)state(s,SS_FLOW_COMPUTE);
        else if(rc!=SS_PENDING)ss_flow_abort(s,rc);
        break;
    case SS_FLOW_COMPUTE:
        rc=ss_model_step(m);
        if(rc==SS_OK){
            rc=ss_model_source(m,&s->source);
            s->source.roi[0]=s->capture.roi[0];s->source.roi[1]=s->capture.roi[1];s->source.roi_pixels=s->capture.roi_pixels;
            if(rc==SS_OK)rc=ss_pair_begin(&s->exporter,&s->source,s->config.packet,s->config.packet_capacity);
            if(rc==SS_OK)state(s,SS_FLOW_EXPORT);else ss_flow_abort(s,rc);
        }else if(rc!=SS_PENDING)ss_flow_abort(s,rc);
        else if(m->error)ss_flow_abort(s,m->error);
        break;
    case SS_FLOW_EXPORT:
        rc=ss_pair_step(&s->exporter,4u);
        if(rc==SS_OK)state(s,SS_FLOW_MODEL_RELEASE);
        else if(rc!=SS_PENDING)ss_flow_abort(s,rc);
        break;
    case SS_FLOW_MODEL_RELEASE:
        rc=ss_model_release(m);
        if(rc==SS_OK)state(s,SS_FLOW_CAPTURE_RELEASE);
        else if(rc!=SS_PENDING || m->error)ss_flow_abort(s,m->error?m->error:rc);
        break;
    case SS_FLOW_CAPTURE_RELEASE:
        rc=io->capture_release(io->user,s->identity);
        if(rc==SS_OK){s->capture_owned=0;state(s,SS_FLOW_SEND_START);}
        else if(rc!=SS_PENDING)ss_flow_abort(s,rc);
        break;
    case SS_FLOW_SEND_START:
        rc=ss_pair_packet(&s->exporter,&packet,&bytes);
        if(rc!=SS_OK){ss_flow_abort(s,SS_DATA);break;}
        rc=io->send_begin(io->user,s->identity,packet,bytes);
        if(rc==SS_OK){s->send_owned=1;state(s,SS_FLOW_SENDING);}
        else if(rc!=SS_PENDING)ss_flow_abort(s,rc);
        break;
    case SS_FLOW_SENDING:
        rc=io->send_poll(io->user,s->identity);
        if(rc!=SS_PENDING){
            s->send_owned=0;
            if(rc==SS_OK){s->delivered++;s->transport_done_us=m->config.now_us(m->config.clock_user);state(s,SS_FLOW_IDLE);}
            else ss_flow_abort(s,rc);
        }
        break;
    case SS_FLOW_DRAIN:return drain(s);
    default:ss_flow_abort(s,SS_ARGUMENT);break;
    }
    return SS_PENDING;
}
