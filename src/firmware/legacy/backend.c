#include "compute_namespace.h"
#include "backend.h"
#include "stream_dispatch.h"
#include "r14_graph_device.h"
#include "r14_cpu_graph_v182.h"
#include "r14_cpu_jobs_v179.h"
#include <string.h>
#include <limits.h>

#define LG_MAGIC 0x534c4731u
typedef struct {
    uint32_t magic,ready,owned,failed,worker_turn;
    ss_lg_config config;ss_lg_storage storage;
    rc_device device;r14_shared_parameters parameters;
    r14_nn_coop neural;r14_integer_coop integer;rcl_coop color;rg_coop gray;
    r14_graph_device graph_device;
    field_cache_v282 cache;
    void *graph,*field_worker,*cpu_worker,*coordinator,*binding;
    void *field_queue,*cpu_queue;ss_lg_stream_mailbox *stream;
    float *output;uint64_t statistics[8];
} legacy;
static size_t rounded(size_t n){return (n+63u)&~(size_t)63u;}
static void *carve(uint8_t **p,size_t n){void *r=*p;*p+=rounded(n);return r;}
static int aligned(const void *p){return p&&!((uintptr_t)p&63u);}
static int valid(const legacy *p){return p&&p->magic==LG_MAGIC;}
size_t ss_lg_state_bytes(void){return rounded(sizeof(legacy))+rounded(r14_graph_coop_bytes())+
    rounded(field_worker_bytes_v282())+rounded(r14_cpu_worker_bytes_v179())+
    rounded(r14_cpu_coordinator_bytes_v182())+rounded(r14_cpu_binding_bytes_v182());}
size_t ss_lg_mailbox_bytes(void){return sizeof(ss_lg_stream_mailbox)+rounded(field_mailbox_bytes_v282())+rounded(r14_cpu_queue_bytes_v179());}
size_t ss_lg_float_bytes(void){return r14_graph_coop_float_bytes_with_filters();}
static int from_rc(int rc){
    if(rc==RC_OK)return RN_OK;
    if(rc==RC_PENDING||rc==RC_AGAIN)return RN_PENDING;
    if(rc==RC_CANCELLED)return RN_CANCELLED_ERROR;
    return RN_HARDWARE;
}
int ss_lg_init(const ss_lg_storage *s,const ss_lg_config *c){
    legacy *p;uint8_t *at;int rc;
    if(ss_lg_core_id()!=0||!s||!c||!aligned(s->state)||s->state_bytes<ss_lg_state_bytes()||
       !aligned(s->mailbox)||s->mailbox_bytes<ss_lg_mailbox_bytes()||
       !aligned(s->tensor[0])||!aligned(s->tensor[1])||!aligned(s->weights)||!aligned(s->filter)||
       !aligned(s->floats)||s->float_bytes<ss_lg_float_bytes()||!c->ticks||!c->base||!c->build||
       !c->timeout_us||!c->cycle_limit||!c->io.read32||!c->io.write32||!c->io.flush||
       !c->io.invalidate||!c->io.barrier||!c->io.now_us)return RN_ARGUMENT;
#ifndef R14_HOST_REFERENCE
    /* Physical address encoding belongs to the selected immutable driver. */
    if((uintptr_t)s->tensor[0]!=R14_DUAL_TENSOR0||(uintptr_t)s->tensor[1]!=R14_DUAL_TENSOR1||
       (uintptr_t)s->weights!=R14_DUAL_WEIGHTS||(uintptr_t)s->filter!=RG_ARENA0||
       (uintptr_t)s->mailbox!=0x07100000u||s->mailbox_bytes>0x100000u)return RN_ARGUMENT;
#endif
    p=s->state;if(valid(p))return RN_BUSY_ERROR; /* no repeated hardware ownership */
    memset(p,0,ss_lg_state_bytes());p->config=*c;p->storage=*s;
    at=(uint8_t *)p+rounded(sizeof(*p));
    p->graph=carve(&at,r14_graph_coop_bytes());p->field_worker=carve(&at,field_worker_bytes_v282());
    p->cpu_worker=carve(&at,r14_cpu_worker_bytes_v179());p->coordinator=carve(&at,r14_cpu_coordinator_bytes_v182());
    p->binding=carve(&at,r14_cpu_binding_bytes_v182());
    at=s->mailbox;p->stream=carve(&at,sizeof(ss_lg_stream_mailbox));
    p->field_queue=carve(&at,field_mailbox_bytes_v282());p->cpu_queue=carve(&at,r14_cpu_queue_bytes_v179());
    p->cache=(field_cache_v282){c->io.user,c->io.flush,c->io.invalidate,c->io.barrier};
    p->device.io=(rc_io){c->io.user,c->io.read32,c->io.write32,c->io.flush,c->io.invalidate,c->io.barrier,c->io.now_us};
    p->device.base=c->base;p->device.expected_build=c->build;p->device.timeout_us=c->timeout_us;
    rc=rc_open(&p->device);if(rc)return from_rc(rc);
    p->parameters.device=&p->device;p->parameters.packed_weights=s->weights;
    p->parameters.weight_address=R14_DUAL_WEIGHTS;p->parameters.weight_capacity=R14_DUAL_WEIGHT_BYTES;p->parameters.cycle_limit=c->cycle_limit;
    if((rc=r14_nn_coop_init(&p->neural,&p->parameters,s->tensor[0],s->tensor[1],R14_DUAL_TENSOR_BYTES))||
       (rc=r14_integer_coop_init(&p->integer,&p->device,s->tensor[0],s->tensor[1],R14_DUAL_TENSOR_BYTES,c->cycle_limit)))return rc;
    if((rc=rcl_init(&p->color,&p->device,c->cycle_limit))||(rc=rg_init(&p->gray,&p->device,c->cycle_limit)))return from_rc(rc);
    if((rc=field_mailbox_init_v282(p->field_queue,field_mailbox_bytes_v282()))||
       (rc=ss_lg_stream_mailbox_init(p->stream,sizeof(*p->stream)))||
       (rc=ss_lg_stream_bind(0,p->stream,&p->cache))||
       (rc=rf_bind_cpu_queue_v284(&p->color.fields,p->field_queue,&p->cache))||
       (rc=rf_bind_cpu_queue_v284(&p->gray.fields,p->field_queue,&p->cache)))return from_rc(rc);
    p->color.stream_cache=p->cache;p->gray.stream_cache=p->cache;
    if((rc=r14_cpu_queue_init_v179(p->cpu_queue,r14_cpu_queue_bytes_v179()))||
       (rc=r14_cpu_coordinator_init_v182(p->coordinator,r14_cpu_coordinator_bytes_v182(),p->cpu_queue))||
       (rc=r14_graph_coop_init(p->graph,r14_graph_coop_bytes()))||
       (rc=r14_graph_device_init(&p->graph_device,0,&p->neural,&p->integer))||
       (rc=r14_graph_device_filter_init(&p->graph_device,&p->color,&p->gray,s->filter,RG_ARENA_BYTES)))return rc;
    p->magic=LG_MAGIC;__atomic_store_n(&p->ready,1u,__ATOMIC_RELEASE);return RN_OK;
}
int ss_lg_join_cpu1(void *memory){
    legacy *p=memory;int rc;
    if(ss_lg_core_id()!=1||!valid(p)||__atomic_load_n(&p->ready,__ATOMIC_ACQUIRE)!=1u)return RN_ARGUMENT;
    if((rc=ss_lg_stream_bind(1,p->stream,&p->cache))||
       (rc=field_worker_init_v282(p->field_worker,field_worker_bytes_v282(),p->field_queue,&p->cache)))return from_rc(rc);
    rc=r14_cpu_worker_init_v179(p->cpu_worker,r14_cpu_worker_bytes_v179(),p->cpu_queue,1,p->config.ticks);if(rc)return rc;
    __atomic_store_n(&p->ready,3u,__ATOMIC_RELEASE);return RN_OK;
}
int ss_lg_ready(const void *memory){const legacy *p=memory;return valid(p)&&__atomic_load_n(&p->ready,__ATOMIC_ACQUIRE)==3u;}
int ss_lg_begin(void *memory,uint64_t frame,uint64_t source,unsigned retry,const float *const input[6],float *output){
    legacy *p=memory;r14_graph_frame f;r14_cpu_graph_ops_v182 cpu;int rc;unsigned i;
    if(ss_lg_core_id()!=0||!ss_lg_ready(p)||!frame||!source||!retry||!input||!output)return RN_ARGUMENT;
    if(p->owned||p->failed)return RN_BUSY_ERROR;
    for(i=0;i<6;i++)if(!input[i])return RN_ARGUMENT;
    rc=r14_cpu_binding_open_v182(p->binding,r14_cpu_binding_bytes_v182(),p->coordinator,0,frame,source,retry);if(rc)return rc;
    cpu=r14_cpu_graph_callbacks_v182();rc=r14_graph_coop_bind_cpu_v182(p->graph,&cpu,p->binding);
    if(rc){(void)r14_cpu_binding_close_v182(p->binding);return rc;}
    memset(&f,0,sizeof(f));f.left=input[0];f.right=input[1];f.native=input[2];f.gray=input[3];f.right_gray=input[4];f.curve=input[5];
    f.output=output;f.tensor_memory=p->storage.tensor[0];f.tensor_bytes=R14_DUAL_TENSOR_BYTES;
    f.float_memory=p->storage.floats;f.float_bytes=p->storage.float_bytes;
    f.filter_memory=p->storage.filter;f.filter_bytes=RG_ARENA_BYTES;f.statistics=p->statistics;
    rc=r14_graph_coop_begin(p->graph,&f,r14_graph_device_callbacks(),&p->graph_device,0);
    if(rc){(void)r14_cpu_binding_close_v182(p->binding);return rc;}
    p->output=output;p->owned=1;return RN_OK;
}
int ss_lg_step(void *memory){
    legacy *p=memory;int rc;
    if(ss_lg_core_id()!=0||!valid(p)||!p->owned)return RN_ARGUMENT;
    if(p->failed)return (int)p->failed;
    rc=r14_graph_coop_step(p->graph);
    if(rc==RN_OK){p->cache.clean(p->cache.user,p->output,81920u*4u);p->cache.barrier(p->cache.user);}
    if(rc!=RN_OK&&rc!=RN_PENDING)p->failed=(uint32_t)rc;
    return rc;
}
int ss_lg_worker_step(void *memory){
    legacy *p=memory;unsigned turn;int rc;
    if(ss_lg_core_id()!=1||!ss_lg_ready(p))return RN_ARGUMENT;
    turn=p->worker_turn;p->worker_turn=(turn+1)%3;
    if(turn==0)return from_rc(field_worker_step_v282(p->field_worker));
    if(turn==1)return from_rc(ss_lg_stream_worker_step());
    rc=r14_cpu_worker_step_v179(p->cpu_worker);return rc;
}
int ss_lg_cancel(void *memory){
    legacy *p=memory;int rc;
    if(ss_lg_core_id()!=0||!valid(p)||!p->owned)return RN_ARGUMENT;
    if(p->failed)return (int)p->failed;
    rc=r14_graph_coop_cancel(p->graph);if(rc!=RN_ARGUMENT)p->failed=(uint32_t)rc;
    return rc;
}
int ss_lg_release(void *memory){
    legacy *p=memory;int rc;
    if(ss_lg_core_id()!=0||!valid(p)||!p->owned)return RN_ARGUMENT;
    if(p->failed){
        /* Recovery never frees a graph while hardware/CPU1 owns its buffers. */
        if(r14_nn_coop_recover(&p->neural)!=RN_OK||r14_integer_coop_recover(&p->integer)!=RN_OK||
           r14_graph_device_filter_recover(&p->graph_device)!=RN_OK)return RN_PENDING;
    }
    rc=r14_graph_coop_release(p->graph);if(rc==RN_BUSY_ERROR)return RN_PENDING;if(rc)return rc;
    rc=r14_cpu_binding_close_v182(p->binding);if(rc)return rc;
    p->owned=p->failed=0;return RN_OK;
}
int ss_lg_owned(const void *memory){const legacy *p=memory;return valid(p)&&p->owned;}
