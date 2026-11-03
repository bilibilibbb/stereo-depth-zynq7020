#include "platform_a9.h"
#include "fw_a9_platform.h"
#include "xparameters.h"
#include <string.h>
#include <limits.h>
#if !defined(__arm__) || defined(R14_HOST_REFERENCE)
#error "Production platform requires actual Cortex-A9 instructions and MMIO"
#endif

extern void ss_cpu1_boot(void);
extern uint32_t MMUTable[];
extern int retained_final_lut_init(void);
_Static_assert(offsetof(ss_platform,system)==0,"Shared system is the callback anchor");
enum {SECONDARY_MAGIC=0x53534331u,SECONDARY_RELEASED=1,SECONDARY_JOINED=2,SECONDARY_FAILED=3};
static struct {
    _Alignas(64) uint32_t magic,state;
    ss_platform *platform;
    int error,worker_error;
} secondary;

static void *mem(const ss_platform *p,unsigned id){return (void*)(uintptr_t)p->memory.span[id].address;}
static uint64_t ticks(void){
    volatile const uint32_t *t=(volatile const uint32_t *)(uintptr_t)0xf8f00200u;
    uint32_t hi,lo,again;
    do{hi=t[1];lo=t[0];again=t[1];}while(hi!=again);
    return ((uint64_t)hi<<32)|lo;
}
uint64_t ss_platform_now_us(void *user){
    const ss_platform *p=user;uint64_t t=ticks(),hz=p->config.global_timer_hz;
    /* Quotient/remainder avoids overflow from ticks*1e6. Timer started only
     * by CPU0 SDK startup; CPU1 never resets this shared timebase. */
    return t/hz*1000000u+(t%hz)*1000000u/hz;
}
static int camera_address(const ss_platform *p,uint32_t address){
    return !(address&3u) &&
        ((address>=p->config.camera_base && address-p->config.camera_base<0x300u) ||
         (address>=p->config.sensor_base && address-p->config.sensor_base<0x1000u));
}
static int camera_read(void *user,uint32_t address,uint32_t *out){
    ss_platform *p=user;if(!out || !camera_address(p,address))return RN_ARGUMENT;
    *out=*(volatile const uint32_t *)(uintptr_t)address;fw_a9_barrier();return RN_OK;
}
static int camera_write(void *user,uint32_t address,uint32_t value){
    ss_platform *p=user;if(!camera_address(p,address))return RN_ARGUMENT;
    *(volatile uint32_t *)(uintptr_t)address=value;return RN_OK;
}
unsigned ss_lg_core_id(void){return fw_a9_core_id();}
static void shared_clean(void *user,const void *data,uint32_t bytes){ss_platform *p=user;fw_a9_clean(&p->system.cache_lock,data,bytes);}
static void shared_invalidate(void *user,void *data,uint32_t bytes){ss_platform *p=user;fw_a9_invalidate(&p->system.cache_lock,data,bytes);}
static void shared_barrier(void *user){(void)user;fw_a9_barrier();}
static int compute_address(const ss_platform *p,uint32_t a){return !(a&3u)&&
    ((a>=p->config.nn_base&&a-p->config.nn_base<0x1000u)||(a>=p->config.sgm_base&&a-p->config.sgm_base<0x1000u));}
static int compute_read(void *user,uint32_t a,uint32_t *out){ss_platform *p=user;if(!out||!compute_address(p,a))return RN_ARGUMENT;*out=*(volatile const uint32_t *)(uintptr_t)a;fw_a9_barrier();return RN_OK;}
static int compute_write(void *user,uint32_t a,uint32_t v){ss_platform *p=user;if(!compute_address(p,a))return RN_ARGUMENT;*(volatile uint32_t *)(uintptr_t)a=v;return RN_OK;}
static int requirement(uint32_t *out,size_t bytes){if(!bytes || bytes>UINT32_MAX)return SS_ARGUMENT;*out=(uint32_t)bytes;return SS_OK;}
static int backend_error(ss_platform *p,int rc){p->setup_error=rc;return SS_BACKEND;}
static void camera_notify(void *user){(void)user;fw_a9_notify();}

int ss_platform_prepare(ss_platform *p,const ss_platform_config *c,const uint32_t *table){
    ss_memory_requirements needs={0};ss_lg_config legacy;ss_platform_config config;rn_io compute_io;
    uint32_t fp,timer_control;unsigned slot,side;int rc;
    if(!p || !c || p->attempted || ((uintptr_t)p&63u) || table!=MMUTable || fw_a9_core_id()!=0u ||
       (uintptr_t)p<SS_IMAGE_BEGIN || (uintptr_t)p>SS_IMAGE_END-sizeof(*p) ||
       !c->boot || !c->epoch || !c->platform_id || !c->global_timer_hz || !c->sgm_timeout_cycles ||
       c->global_timer_hz!=XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ/2u ||
       !c->driver_timeout_us || c->driver_timeout_us>10000000u || !c->debounce_us ||
       c->camera_base!=R14_CAMERA_BASE || c->sensor_base!=R14_SENSOR_BASE ||
       !c->sensor_build || !c->capture_cookie || secondary.magic)return SS_ARGUMENT;
    /* Both workers use the accepted nearest-even/gradual-underflow mode. */
    fp=0;__asm__ volatile("vmsr fpscr,%0"::"r"(fp):"memory");
    rc=fw_a9_platform_check(table,0u,&fp);if(rc)return backend_error(p,rc);
    timer_control=*(volatile const uint32_t *)(uintptr_t)0xf8f00208u;
    if(!(timer_control&1u) || (timer_control&0xff00u) ||
       !fw_a9_section_range(table,SS_ARENA_BEGIN,SS_DMA_END-SS_ARENA_BEGIN,0,1) ||
       table[SS_MAILBOX_BEGIN>>20]!= (SS_MAILBOX_BEGIN|0x11de2u) ||
       !fw_a9_section_range(table,c->camera_base,0x300u,1,0) ||
       !fw_a9_section_range(table,c->sensor_base,0x1000u,1,0))return SS_ARGUMENT;
    config=*c;memset(p,0,sizeof(*p));p->config=config;p->table=table;p->attempted=1;c=&p->config;
    rc=ss_calibration_bind(&p->calibration,(const void*)(uintptr_t)SS_CALIBRATION_BEGIN,SS_CALIBRATION_BYTES,c->platform_id);
    if(rc)return rc;
    if(requirement(&needs.system,ss_lg_state_bytes()) || requirement(&needs.workspace,ss_lg_float_bytes()) ||
       ss_lg_mailbox_bytes()>SS_MAILBOX_END-SS_MAILBOX_BEGIN)return SS_ARGUMENT;
    rc=ss_memory_plan(&p->memory,&needs);if(rc)return rc;
    /* Startup before either core or DMA owns these reservations. */
    memset((void*)(uintptr_t)SS_ARENA_BEGIN,0,p->memory.arena_used);
    memset((void*)(uintptr_t)SS_MAILBOX_BEGIN,0,ss_lg_mailbox_bytes());
    p->system.cache=(fw_cache){shared_clean,shared_invalidate,shared_barrier,p};
    p->system.backend=mem(p,SS_MEM_SYSTEM);
    p->system.storage=(ss_lg_storage){p->system.backend,needs.system,
        (void*)(uintptr_t)SS_MAILBOX_BEGIN,ss_lg_mailbox_bytes(),
        {mem(p,SS_MEM_TENSOR0),mem(p,SS_MEM_TENSOR1)},mem(p,SS_MEM_WEIGHTS),mem(p,SS_MEM_FILTER),
        mem(p,SS_MEM_WORKSPACE),needs.workspace};
    compute_io=(rn_io){p,compute_read,compute_write,shared_clean,shared_invalidate,shared_barrier,ss_platform_now_us};
    legacy=(ss_lg_config){compute_io,c->nn_base,c->platform_id,c->driver_timeout_us,c->sgm_timeout_cycles,ticks};
    rc=ss_lg_init(&p->system.storage,&legacy);if(rc)return backend_error(p,rc);
    p->sgm.io=compute_io;p->sgm.base=c->sgm_base;p->sgm.expected_build=c->sgm_build;p->sgm.timeout_us=c->driver_timeout_us;
    rc=rs_open(&p->sgm);if(rc)return backend_error(p,rc);
    rc=retained_final_lut_init();if(rc)return backend_error(p,rc);
    p->capture.io=(rn_io){p,camera_read,camera_write,p->system.cache.clean,
        p->system.cache.invalidate,p->system.cache.barrier,ss_platform_now_us};
    p->capture.base=c->camera_base;p->capture.sensor_base=c->sensor_base;p->capture.sensor_build=c->sensor_build;
    p->capture.cookie=c->capture_cookie;p->capture.mode_tag=p->calibration.mode_tag;
    p->capture.format=1;p->capture.max_skew_ticks=p->calibration.max_skew_ticks;p->capture.timeout_us=c->driver_timeout_us;
    for(slot=0;slot<2;slot++)for(side=0;side<2;side++)
        p->capture.buffers[slot][side]=(void*)(uintptr_t)(SS_CAPTURE_BEGIN+slot*0x200000u+side*0x100000u);
    rc=rc_open(&p->capture);if(rc)return backend_error(p,rc);
    p->prepared=1;return SS_OK;
}
int ss_platform_release_cpu1(ss_platform *p){
    if(!p || !p->prepared || p->finished || fw_a9_core_id()!=0u || secondary.magic ||
       !p->system.backend || ss_lg_ready(p->system.backend))return SS_ARGUMENT;
    secondary.platform=p;secondary.error=secondary.worker_error=0;secondary.magic=SECONDARY_MAGIC;
    __atomic_store_n(&secondary.state,SECONDARY_RELEASED,__ATOMIC_RELEASE);
    p->system.cache.clean(p->system.cache.user,p,sizeof(*p));
    p->system.cache.clean(p->system.cache.user,&secondary,sizeof(secondary));
    p->system.cache.clean(p->system.cache.user,p->table,16384u);
    /* Fresh-loader precondition above is essential: this is the Zynq ROM
     * wake address, not permission to replace an unknown CPU1 application. */
    *(volatile uint32_t *)(uintptr_t)0xfffffff0u=(uint32_t)(uintptr_t)ss_cpu1_boot;
    p->system.cache.clean(p->system.cache.user,(const void*)(uintptr_t)0xffffffe0u,32u);
    fw_a9_notify();return SS_OK;
}
int ss_platform_cpu1_status(const ss_platform *p){
    uint32_t state;
    if(!p || !p->prepared || secondary.platform!=p || secondary.magic!=SECONDARY_MAGIC)return SS_ARGUMENT;
    state=__atomic_load_n(&secondary.state,__ATOMIC_ACQUIRE);
    if(state==SECONDARY_FAILED)return SS_BACKEND;
    return state==SECONDARY_JOINED && ss_lg_ready(p->system.backend)?SS_OK:SS_PENDING;
}
void ss_secondary_main(void){
    ss_platform *p=NULL;uint32_t fp=0;int rc=SS_ARGUMENT;
    __asm__ volatile("vmsr fpscr,%0"::"r"(fp):"memory");
    if(fw_a9_core_id()!=1u || secondary.magic!=SECONDARY_MAGIC ||
       __atomic_load_n(&secondary.state,__ATOMIC_ACQUIRE)!=SECONDARY_RELEASED)goto failed;
    p=secondary.platform;
    if(!p || !p->prepared || p->table!=MMUTable)goto failed;
    rc=fw_a9_platform_check(MMUTable,1u,&fp);if(rc)goto failed;
    if(MMUTable[SS_MAILBOX_BEGIN>>20]!=(SS_MAILBOX_BEGIN|0x11de2u))goto failed;
    rc=ss_lg_join_cpu1(p->system.backend);if(rc)goto failed;
    __atomic_store_n(&secondary.state,SECONDARY_JOINED,__ATOMIC_RELEASE);fw_a9_notify();
    for(;;){
        /* No secondary reinitialization. Both workers service bounded chunks;
         * a pending numeric chunk must not WFE waiting for its own progress. */
        if(__atomic_load_n(&p->finished,__ATOMIC_ACQUIRE)){
            (void)ss_camera_worker_step(&p->camera,1u);
            (void)ss_model_worker_step(&p->model);
            rc=ss_lg_worker_step(p->system.backend);
            if(rc!=RN_OK&&rc!=RN_PENDING&&rc!=RN_CANCELLED_ERROR)
                __atomic_store_n(&secondary.worker_error,rc,__ATOMIC_RELEASE);
            __asm__ volatile("yield":::"memory");
        }else fw_a9_wait();
    }
failed:
    secondary.error=rc;__atomic_store_n(&secondary.state,SECONDARY_FAILED,__ATOMIC_RELEASE);fw_a9_notify();
    for(;;)fw_a9_wait();
}
static _Alignas(64) uint8_t camera_model_raw[2][RC_FRAME_BYTES];
int ss_platform_finish(ss_platform *p,void (*network_service)(void *),void *network_user){
    ss_model_config model;ss_camera_config camera;ss_board_config board;unsigned i;int rc;
    if(!p || !network_service || !p->prepared || p->finish_attempted || fw_a9_core_id()!=0u)return SS_ARGUMENT;
    rc=ss_platform_cpu1_status(p);if(rc)return rc;
    p->finish_attempted=1;
    memset(&model,0,sizeof(model));model.system=p->system.backend;model.storage=p->system.storage;
    model.sgm=&p->sgm;model.cache=p->system.cache;model.notify=camera_notify;
    for(i=0;i<6;i++)model.input[i]=mem(p,SS_MEM_INPUT0+i);
    model.output=mem(p,SS_MEM_OUTPUT);model.now_us=ss_platform_now_us;model.clock_user=p;
    model.dma=(rs_frame){p->memory.span[SS_MEM_CENSUS_LEFT].address,p->memory.span[SS_MEM_CENSUS_RIGHT].address,
        p->memory.span[SS_MEM_CURVE].address,p->memory.span[SS_MEM_NATIVE].address,p->config.sgm_timeout_cycles,
        mem(p,SS_MEM_CENSUS_LEFT),mem(p,SS_MEM_CENSUS_RIGHT),mem(p,SS_MEM_CURVE),mem(p,SS_MEM_NATIVE)};
    rc=ss_model_init(&p->model,&model);if(rc)return rc;
    camera=(ss_camera_config){&p->capture,&p->calibration,p->config.boot,mem(p,SS_MEM_RAW_RGB),
        {mem(p,SS_MEM_RECT_LEFT),mem(p,SS_MEM_RECT_RIGHT)},{mem(p,SS_MEM_ROI_LEFT),mem(p,SS_MEM_ROI_RIGHT)},
        mem(p,SS_MEM_PREVIEW),SS_RAW_RGB_BYTES,SS_PIXELS*3u,SS_PIXELS,SS_PREVIEW_MAX_BYTES,
        mem(p,SS_MEM_RAW_RGB_RIGHT),camera_notify,p};
    rc=ss_camera_init(&p->camera,&camera);if(rc)return rc;
    /* Listeners bind only after actual lwIP/netif init performed by caller. */
    rc=sdp_tcp_listen(&p->preview,1u,6000u);if(rc)return rc;
    rc=sdp_tcp_listen(&p->result,2u,6001u);if(rc)return rc;
    board=(ss_board_config){&p->model,&p->camera,&p->preview,&p->result,mem(p,SS_MEM_PAIR),SS_FROZEN_PACKET_BYTES,
        p->config.epoch,p->config.debounce_us,network_service,network_user,{mem(p,SS_MEM_RESULT0),mem(p,SS_MEM_RESULT1)}};
    rc=ss_board_init(&p->board,&board);if(rc)return rc;
    rc=ss_board_video_enable(&p->board,mem(p,SS_MEM_PREVIEW_NEXT),SS_PREVIEW_MAX_BYTES,camera_model_raw[0],camera_model_raw[1]);if(rc)return rc;
    __atomic_store_n(&p->finished,1u,__ATOMIC_RELEASE);fw_a9_notify();return SS_OK;
}
