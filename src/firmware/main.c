#include "platform_a9.h"
#include "ps_io.h"
#include "boot_config.h"
#include "network/gem_netif.h"
#include "retained_camera_recipe.h"
#include "fw_a9_platform.h"
#include "xparameters.h"
#include <stdio.h>
#include <string.h>

extern uint32_t MMUTable[];
static struct {
    _Alignas(64) ss_platform platform;
    _Alignas(64) ss_gem gem;
    ss_ps_io ps;
    ss_boot_config config;
    r14_camera sensor;
    volatile int error;
} app;
static uint32_t mmio_read(void *user,uint32_t a){(void)user;return *(volatile const uint32_t *)(uintptr_t)a;}
static void mmio_write(void *user,uint32_t a,uint32_t v){(void)user;*(volatile uint32_t *)(uintptr_t)a=v;}
static void mmio_barrier(void *user){(void)user;fw_a9_barrier();}
static uint64_t now(void *user){(void)user;return ss_platform_now_us(&app.platform);}
static int gem_clock(void *user,unsigned speed){(void)user;return ss_ps_gem_clock(&app.ps,speed);}
static int sensor_read(void *user,uint32_t a,uint32_t *v){
    (void)user;if(!v || (a&3u) || a<R14_SENSOR_BASE || a-R14_SENSOR_BASE>=0x1000u)return RN_ARGUMENT;
    *v=mmio_read(NULL,a);fw_a9_barrier();return RN_OK;
}
static int sensor_write(void *user,uint32_t a,uint32_t v){
    (void)user;if((a&3u) || a<R14_SENSOR_BASE || a-R14_SENSOR_BASE>=0x1000u)return RN_ARGUMENT;
    mmio_write(NULL,a,v);fw_a9_barrier();return RN_OK;
}
static void fatal(const char *phase,int error) __attribute__((noreturn));
static void fatal(const char *phase,int error){
    app.error=error;printf("SNAP11459 startup failed: %s rc=%d; buffers retained\r\n",phase,error);fflush(stdout);
    /* No reset, core takeover or freeing unknown DMA after failure. */
    for(;;)__asm__ volatile("wfe":::"memory");
}
int main(void){
    ss_platform_config pc;ss_gem_config gc;ss_boot_config *c=&app.config;
    uint32_t recipe,fp;uint64_t boot,joined;int rc,key,last_error=0;
    if(fw_a9_core_id()!=0u)fatal("core",-1);
    fp=0;__asm__ volatile("vmsr fpscr,%0"::"r"(fp):"memory");
    rc=fw_a9_platform_check(MMUTable,0,&fp);if(rc)fatal("MMU/cache",rc);
    app.platform.config.global_timer_hz=XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ/2u;
    if((mmio_read(NULL,0xf8f00208u)&0xff01u)!=1u)fatal("global timer",-1);
    app.ps=(ss_ps_io){mmio_read,mmio_write,mmio_barrier,NULL,33333333u,0};
    rc=ss_ps_prepare(&app.ps);if(rc)fatal("PS Ethernet/key configuration",rc);
    _Static_assert(sizeof(r14_camera_step)==8,"canonical recipe layout");
    recipe=ss_crc32_update(UINT32_MAX,(const uint8_t*)r14_recipe,sizeof(r14_recipe));
    recipe=ss_crc32_update(recipe,(const uint8_t*)r14_readback,sizeof(r14_readback))^UINT32_MAX;
    rc=ss_boot_files(c,recipe,&boot);if(rc)fatal("SD configuration/calibration/identity",rc);
    /* Original sensor driver performs physical reset/recipe/readback once,
     * on the loader-owned fresh image, before capture or workers start. */
    app.sensor.io=(rn_io){NULL,sensor_read,sensor_write,NULL,NULL,mmio_barrier,now};
    app.sensor.base=R14_SENSOR_BASE;app.sensor.build=c->platform;app.sensor.timeout_us=c->driver_timeout_us;
    rc=r14_camera_open(&app.sensor,r14_recipe,sizeof(r14_recipe)/sizeof(*r14_recipe),r14_readback,sizeof(r14_readback)/sizeof(*r14_readback));
    if(rc)fatal("OV5640 recipe/readback",rc);
    memset(&pc,0,sizeof(pc));pc.boot=boot;pc.epoch=c->epoch;pc.platform_id=c->platform;
    pc.global_timer_hz=XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ/2u;
    pc.nn_base=c->nn_base;pc.box_base=c->box_base;pc.sgm_base=c->sgm_base;pc.sgm_build=c->sgm_build;
    pc.camera_base=R14_CAMERA_BASE;pc.sensor_base=R14_SENSOR_BASE;pc.sensor_build=c->platform;
    pc.capture_cookie=c->capture_cookie;pc.driver_timeout_us=c->driver_timeout_us;pc.sgm_timeout_cycles=c->sgm_timeout_cycles;pc.debounce_us=c->debounce_us;
    rc=ss_platform_prepare(&app.platform,&pc,MMUTable);if(rc)fatal("frozen model/platform",rc);
    memset(&gc,0,sizeof(gc));gc.base=0xe000b000u;gc.phy_address=1;gc.cache=app.platform.system.cache;gc.table=MMUTable;
    memcpy(gc.mac,c->mac,6);memcpy(gc.ip,c->ip,4);memcpy(gc.mask,c->mask,4);memcpy(gc.gateway,c->gateway,4);
    gc.now_us=now;gc.set_clock=gem_clock;rc=ss_gem_init(&app.gem,&gc);if(rc)fatal("GEM/lwIP",rc);
    rc=ss_platform_release_cpu1(&app.platform);if(rc)fatal("CPU1 release",rc);
    joined=now(NULL);
    do{
        ss_gem_service(&app.gem);rc=ss_platform_cpu1_status(&app.platform);
        if(rc==SS_PENDING && now(NULL)-joined>10000000u)fatal("CPU1 startup acknowledgement",-1);
    }while(rc==SS_PENDING);
    if(rc)fatal("CPU1 join",rc);
    rc=ss_platform_finish(&app.platform,ss_gem_service,&app.gem);if(rc)fatal("application",rc);
    printf("SNAP11459 ready; key=MIO10; preview=6000 pair=6001; boot=%08lx%08lx\r\n",(unsigned long)(boot>>32),(unsigned long)boot);fflush(stdout);
    for(;;){
        key=ss_ps_key1(&app.ps);if(key<0)key=0;
        /* A permanent GEM fault prevents new photos; an existing accepted
         * computation still drains normally and keeps its complete result. */
        rc=ss_board_step(&app.platform.board,app.gem.error?0:key);
        if(app.gem.error && app.gem.error!=app.error){app.error=app.gem.error;printf("SNAP11459 GEM fault %d; result retained\r\n",app.error);}
        if(rc<0 && rc!=last_error){last_error=rc;printf("SNAP11459 application rc=%d\r\n",rc);}
    }
}
