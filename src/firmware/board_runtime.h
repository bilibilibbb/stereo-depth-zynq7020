#ifndef SNAPSHOT_BOARD_RUNTIME_H
#define SNAPSHOT_BOARD_RUNTIME_H
#include "camera_capture.h"
#include "sdp_tcp.h"
#include "latest_pair.h"
#include "video_lane.h"

typedef struct {
    ss_model *model;
    ss_camera *camera;
    sdp_tcp *preview,*result;
    uint8_t *result_packet;size_t result_capacity;
    uint32_t epoch,debounce_us;
    /* Actual GEM input + TCP timer service, CPU0 and bounded. This must not
     * reenter the app, capture, lwIP raw API or model from another core/ISR. */
    void (*network_service)(void *);
    void *network_user;
    uint8_t *result_slot[2];
} ss_board_config;
typedef struct {
    ss_board_config config;
    ss_flow flow;
    ss_latest_pair latest;
    ss_identity send_identity;
    uint8_t pending_status[96],sending_status[96];
    unsigned initialized,preview_loan,result_owner,status_pending;
    uint64_t seen_busy,sgm_notified_shot;
    int camera_error,network_error;
    uint64_t requests,delivered,busy_presses,pressed_since_us,transport_done_us;
    /* One accepted request may await the first complete publication. Key
     * debounce changes must not overwrite that request's original time. */
    uint64_t request_pressed_us;
    int key_initialized,raw_pressed,stable_pressed,key_armed,producer_error,waiting_result;
    ss_video video;
    unsigned video_enabled;
} ss_board;

/* Configured real peripherals/workers/listeners only. No bootstrap, test
 * replacement, hard-coded replay or GPIO/PHY probing is performed here. */
int ss_board_init(ss_board *,const ss_board_config *);
/* Enable once before the first board step. Second packet storage must be
 * private; retains the same TCP protocol and already-validated camera IO. */
int ss_board_video_enable(ss_board *,uint8_t *,size_t,uint8_t *,uint8_t *);
/* Actual CPU0 application iteration. Sample PS_KEY1 outside this function;
 * core1 continues the selected FIELD/CPU-task worker role independently. */
int ss_board_step(ss_board *,int pressed);
#endif
