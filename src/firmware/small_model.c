#include "small_model.h"
#include <string.h>

static const uint32_t input_bytes[6]={983040u,983040u,1310720u,327680u,327680u,696320u};
enum {RIGHT_IDLE,RIGHT_READY,RIGHT_RUNNING,RIGHT_DONE,RIGHT_CANCELLED};
enum {PREPARE=1,SGM_BEGIN,SGM_POLL,DECODE,GRAPH_BEGIN,GRAPH_RUN};
static uint32_t load(const uint32_t *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void publish(uint32_t *p,uint32_t v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static int overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an&&bn&&(x<=y?y-x<an:x-y<bn);}
static void notify(ss_model *m){m->config.notify(m->config.clock_user);}
static void error(ss_model *m,int code,int backend){
    if(!m->error)m->error=code;
    if(backend&&!m->backend_error)m->backend_error=backend;
    m->ready=0;publish(&m->cancelled,1);notify(m);
}
static int completed_at(ss_model *m,uint64_t *stamp){
    uint64_t now=m->config.now_us(m->config.clock_user);
    if(now<m->last_us){error(m,SS_CLOCK,0);return 0;}
    m->last_us=*stamp=now;return 1;
}
int ss_model_init(ss_model *m,const ss_model_config *c){
    unsigned i;
    if(!m||!c||m->initialized||!c->system||c->storage.state!=c->system||!ss_lg_ready(c->system)||
       ss_lg_owned(c->system)||!c->sgm||!c->sgm->opened||c->sgm->ownership||!c->output||
       !c->now_us||!c->notify||!c->cache.clean||!c->cache.invalidate||!c->cache.barrier||
       !c->dma.left||!c->dma.right||!c->dma.curve||!c->dma.native)return SS_ARGUMENT;
    for(i=0;i<6;i++)if(!c->input[i])return SS_ARGUMENT;
    memset(m,0,sizeof(*m));m->config=*c;m->initialized=1;return SS_OK;
}
int ss_model_packet_private(const ss_model *m,const void *data,size_t bytes){
    const ss_lg_storage *p;unsigned i;
    if(!m||!m->initialized||!data||!bytes||bytes-1u>UINTPTR_MAX-(uintptr_t)data)return 0;
    p=&m->config.storage;
#define HIT(a,n) overlap(data,bytes,(a),(n))
    if(HIT(m,sizeof(*m))||HIT(p->state,p->state_bytes)||HIT(p->mailbox,p->mailbox_bytes)||
       HIT(p->tensor[0],2457600u)||HIT(p->tensor[1],2457600u)||HIT(p->weights,65536u)||
       HIT(p->filter,1552384u)||HIT(p->floats,p->float_bytes)||HIT(m->config.sgm,sizeof(*m->config.sgm))||
       HIT(m->config.output,SS_MODEL_PIXELS*4u)||HIT(m->config.dma.left,RS_CENSUS_BYTES)||
       HIT(m->config.dma.right,RS_CENSUS_BYTES)||HIT(m->config.dma.curve,RS_CURVE_BYTES)||
       HIT(m->config.dma.native,RS_NATIVE_BYTES))return 0;
    for(i=0;i<6;i++)if(HIT(m->config.input[i],input_bytes[i]))return 0;
#undef HIT
    return 1;
}
int ss_model_start(ss_model *m,ss_identity id,uint64_t trigger,uint64_t locked,const uint8_t *left,const uint8_t *right){
    uint64_t now;int rc;
    if(!m||!m->initialized||!id.boot||!id.shot||!id.epoch||id.epoch==UINT32_MAX||!id.attempt||
       !left||!right||trigger>locked)return SS_ARGUMENT;
    if(m->active||ss_lg_owned(m->config.system)||m->config.sgm->ownership||load(&m->right_state)==RIGHT_RUNNING)return SS_BUSY;
    now=m->config.now_us(m->config.clock_user);if(now<locked)return SS_CLOCK;
    rc=fw_observations_prepare_rgb_begin(&m->left,left,m->config.input[0],m->config.input[3],(uint32_t *)m->config.dma.left);
    if(rc){m->backend_error=rc;return SS_BACKEND;}
    rc=fw_observations_prepare_rgb_begin(&m->right,right,m->config.input[1],m->config.input[4],(uint32_t *)m->config.dma.right);
    if(rc){m->backend_error=rc;return SS_BACKEND;}
    memset(&m->source,0,sizeof(m->source));
    m->source.identity=m->source.rgb_identity=m->source.sgm_identity=m->source.model_identity=id;
    m->source.trigger_us=trigger;m->source.locked_us=locked;m->source.rgb=left;m->source.rgb_bytes=SS_PIXELS*3u;
    m->source.sgm=m->config.dma.native;m->source.sgm_pixels=SS_PIXELS;
    m->source.model=m->config.output;m->source.model_pixels=SS_MODEL_PIXELS;
    m->active=1;m->ready=m->sgm_seen=m->error=m->backend_error=0;m->last_us=now;
    m->left_done=m->recover_clear=0;m->phase=PREPARE;
    m->right_error=0;publish(&m->cancelled,0);publish(&m->right_state,RIGHT_READY);notify(m);return SS_OK;
}
int ss_model_worker_step(ss_model *m){
    uint32_t expect=RIGHT_READY;int rc;
    if(!m||!m->initialized)return SS_ARGUMENT;
    if(!__atomic_compare_exchange_n(&m->right_state,&expect,RIGHT_RUNNING,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return SS_PENDING;
    if(load(&m->cancelled)){publish(&m->right_state,RIGHT_CANCELLED);return SS_PENDING;}
    rc=fw_observations_prepare_step(&m->right);
    if(rc!=FW_OK&&rc!=FW_PENDING){m->right_error=(uint32_t)rc;publish(&m->right_state,RIGHT_DONE);return SS_BACKEND;}
    if(rc==FW_OK){
        m->config.cache.clean(m->config.cache.user,m->config.input[1],input_bytes[1]);
        m->config.cache.clean(m->config.cache.user,m->config.input[4],input_bytes[4]);
        m->config.cache.clean(m->config.cache.user,m->config.dma.right,RS_CENSUS_BYTES);
        m->config.cache.barrier(m->config.cache.user);
    }
    publish(&m->right_state,load(&m->cancelled)?RIGHT_CANCELLED:rc==FW_OK?RIGHT_DONE:RIGHT_READY);
    return rc==FW_OK?SS_OK:SS_PENDING;
}
void ss_model_cancel(ss_model *m,int reason){if(m&&m->active)error(m,reason<0?reason:SS_CANCELLED,0);}
/* Existing SGM recovery expressed as one status/clear step; never block the
 * network loop in rs_recover's timeout polling. NEEDS_RESET stays quarantined. */
static int sgm_drain(ss_model *m){
    rs_device *d=m->config.sgm;uint32_t s,mask=RN_BUSY|RN_QUIET|RN_READY|RN_NEEDS_RESET;
    if(!d->ownership)return SS_OK;
    if(d->ownership==1)(void)rs_cancel(d);
    if(d->ownership!=2||d->io.read32(d->io.user,d->base+RN_STATUS,&s))return SS_PENDING;
    d->last_status=s;if(s&RN_NEEDS_RESET)return SS_PENDING;
    if(!m->recover_clear){
        if((s&RN_BUSY)||!(s&RN_QUIET))return SS_PENDING;
        if(d->io.write32(d->io.user,d->base+RN_CONTROL,4))return SS_PENDING;
        d->io.barrier(d->io.user);m->recover_clear=1;return SS_PENDING;
    }
    if((s&mask)!=(RN_QUIET|RN_READY)||(s>>16))return SS_PENDING;
    if(d->owned_curve)d->io.invalidate(d->io.user,d->owned_curve,RS_CURVE_BYTES);
    if(d->owned_native)d->io.invalidate(d->io.user,d->owned_native,RS_NATIVE_BYTES);
    d->io.barrier(d->io.user);d->ownership=0;d->owned_curve=d->owned_native=0;
    memset(d->cached_valid,0,sizeof(d->cached_valid));m->recover_clear=0;return SS_OK;
}
static int drain(ss_model *m){
    uint32_t expected=RIGHT_READY;
    (void)__atomic_compare_exchange_n(&m->right_state,&expected,RIGHT_CANCELLED,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    if(load(&m->right_state)==RIGHT_RUNNING)return SS_PENDING;
    if(sgm_drain(m)!=SS_OK)return SS_PENDING;
    if(ss_lg_owned(m->config.system)){
        (void)ss_lg_cancel(m->config.system);
        if(ss_lg_release(m->config.system)!=RN_OK)return SS_PENDING;
    }
    m->active=0;return m->error;
}
int ss_model_step(ss_model *m){
    uint64_t now;int rc=RN_PENDING;
    if(!m||!m->initialized)return SS_ARGUMENT;
    if(!m->active)return m->error?m->error:SS_OK;
    if(m->ready)return SS_OK;
    now=m->config.now_us(m->config.clock_user);
    if(now<m->last_us)error(m,SS_CLOCK,0);else m->last_us=now;
    if(m->error)return drain(m);
    switch(m->phase){
    case PREPARE:
        if(!m->left_done){
            rc=fw_observations_prepare_step(&m->left);
            if(rc!=FW_OK&&rc!=FW_PENDING){error(m,SS_BACKEND,rc);break;}
            m->left_done=rc==FW_OK;
        }
        if(load(&m->right_state)==RIGHT_DONE){
            if(m->right_error){error(m,SS_BACKEND,(int)m->right_error);break;}
            if(m->left_done)m->phase=SGM_BEGIN;
        }
        break;
    case SGM_BEGIN:
        rc=rs_start(m->config.sgm,&m->config.dma);
        if(rc==RN_OK)m->phase=SGM_POLL;
        else if(rc!=RN_BUSY_ERROR&&rc!=RN_PENDING)error(m,SS_BACKEND,rc);
        break;
    case SGM_POLL:
        rc=rs_poll(m->config.sgm);
        if(rc==RN_OK){
            if(!completed_at(m,&m->source.sgm_done_us))break;
            m->sgm_seen=1;
            rc=fw_observations_decode_begin(&m->decode,m->config.dma.native,m->config.dma.curve,m->config.input[2],m->config.input[5]);
            if(rc==FW_OK)m->phase=DECODE;else error(m,SS_BACKEND,rc);
        }else if(rc!=RN_PENDING)error(m,SS_BACKEND,rc);
        break;
    case DECODE:
        rc=fw_observations_decode_step(&m->decode);
        if(rc==FW_OK)m->phase=GRAPH_BEGIN;else if(rc!=FW_PENDING)error(m,SS_BACKEND,rc);
        break;
    case GRAPH_BEGIN:
        rc=ss_lg_begin(m->config.system,m->source.identity.shot,m->source.identity.shot,m->source.identity.attempt,
                       (const float *const *)m->config.input,m->config.output);
        if(rc==RN_OK)m->phase=GRAPH_RUN;else if(rc!=RN_PENDING&&rc!=RN_BUSY_ERROR)error(m,SS_BACKEND,rc);
        break;
    case GRAPH_RUN:
        rc=ss_lg_step(m->config.system);
        if(rc==RN_OK&&completed_at(m,&m->source.model_done_us))m->ready=1;
        else if(rc!=RN_PENDING)error(m,SS_BACKEND,rc);
        break;
    default:error(m,SS_BACKEND,RN_ARGUMENT);break;
    }
    notify(m);return m->ready?SS_OK:SS_PENDING;
}
int ss_model_source(const ss_model *m,ss_pair_source *source){
    if(!m||!source)return SS_ARGUMENT;
    if(!m->active||!m->ready||m->error)return SS_PENDING;
    *source=m->source;return SS_OK;
}
int ss_model_release(ss_model *m){
    int rc;if(!m||!m->initialized||!m->active||!m->ready)return SS_ARGUMENT;
    rc=ss_lg_release(m->config.system);
    if(rc!=RN_OK){if(rc!=RN_PENDING)error(m,SS_BACKEND,rc);return SS_PENDING;}
    m->active=m->ready=0;return SS_OK;
}
