#ifndef SNAPSHOT_PLATFORM_A9_H
#define SNAPSHOT_PLATFORM_A9_H
#include "board_runtime.h"
#include "memory_layout.h"

typedef struct {
    uint64_t boot;
    uint32_t epoch,platform_id,global_timer_hz;
    uint32_t nn_base,box_base,sgm_base,sgm_build;
    uint32_t camera_base,sensor_base,sensor_build,capture_cookie;
    uint32_t driver_timeout_us,sgm_timeout_cycles,debounce_us;
} ss_platform_config;
typedef struct {
    _Alignas(64) uint32_t cache_lock;
    fw_cache cache;
    void *backend;
    ss_lg_storage storage;
} ss_platform_compute;
typedef struct {
    _Alignas(64) ss_platform_compute system;
    _Alignas(64) rs_device sgm;
    _Alignas(64) ss_model model;
    _Alignas(64) ss_camera camera;
    _Alignas(64) rc_device capture;
    _Alignas(64) ss_calibration calibration;
    _Alignas(64) sdp_tcp preview,result;
    _Alignas(64) ss_board board;
    ss_memory_layout memory;
    ss_platform_config config;
    const uint32_t *table;
    uint32_t attempted,prepared,finish_attempted,finished;
    int setup_error;
} ss_platform;

uint64_t ss_platform_now_us(void *);
/* CPU0, after PS/DDR/MMU boot and actual matching sensor configuration.
 * Real calibration is preloaded into the dedicated immutable DDR region.
 * This binds only slot0, real PL endpoints, original model LUT and worker0.
 * It cannot run on arbitrary already-owned hardware; original drivers refuse
 * unknown BUSY/poisoned state. Does not reset hardware or start capture. */
int ss_platform_prepare(ss_platform *,const ss_platform_config *,const uint32_t *);
/* CPU0, once the loader has established CPU1 is in its fresh BootROM wait.
 * No reset, remote-core takeover or automatic retry is performed. */
int ss_platform_release_cpu1(ss_platform *);
/* Poll once. PENDING means CPU1 has not joined; an error never frees/reuses
 * memory. CPU0 must not enter the application until this returns OK. */
int ss_platform_cpu1_status(const ss_platform *);
/* After actual GEM/lwIP/netif initialization, CPU1 join and sensor setup.
 * Creates separate TCP listeners and enables the existing application. */
int ss_platform_finish(ss_platform *,void (*)(void *),void *);
void ss_secondary_main(void) __attribute__((noreturn));
#endif
