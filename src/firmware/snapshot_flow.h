#ifndef SNAPSHOT_FLOW_H
#define SNAPSHOT_FLOW_H
#include "small_model.h"

typedef struct {
    ss_identity identity;
    uint64_t locked_us;
    const uint8_t *rgb[2];
    size_t rgb_bytes;
    const uint8_t *roi[2];
    size_t roi_pixels;
} ss_capture_pair;
enum ss_flow_state {
    SS_FLOW_IDLE,SS_FLOW_CAPTURE_START,SS_FLOW_CAPTURE,SS_FLOW_MODEL_START,
    SS_FLOW_COMPUTE,SS_FLOW_EXPORT,SS_FLOW_MODEL_RELEASE,SS_FLOW_CAPTURE_RELEASE,
    SS_FLOW_SEND_START,SS_FLOW_SENDING,SS_FLOW_DRAIN,SS_FLOW_FAULT
};
typedef struct {
    void *user;
    /* All callbacks are CPU0, bounded and non-reentrant. Service must poll
     * Ethernet/timers and advance preview even during compute or drain. */
    void (*service)(void *);
    /* begin: OK=one admitted lease; PENDING=no admission, retryable;
     * negative=no admission. poll: OK=complete time-matched, cache-acquired,
     * rectified pair. Error/cancel NEVER releases capture ownership. */
    int (*capture_begin)(void *,ss_identity,uint64_t);
    int (*capture_poll)(void *,ss_identity,ss_capture_pair *);
    void (*capture_cancel)(void *,ss_identity);
    /* OK is proof that DMA drained and the read lease was returned.
     * Any other result retains the lease, including errors. */
    int (*capture_release)(void *,ss_identity);
    /* Send a whole packet, no copy required here. begin uses the same
     * admission convention. poll OK or negative means the source lease
     * has ended. PENDING retains it. Reconnect may restart the SAME packet.
     * OK is transport completion, not proof of browser display. */
    int (*send_begin)(void *,ss_identity,const uint8_t *,size_t);
    int (*send_poll)(void *,ss_identity);
    void (*send_cancel)(void *,ss_identity);
    void (*state_changed)(void *,enum ss_flow_state,ss_identity,int);
} ss_flow_io;
typedef struct {
    ss_model *model;
    ss_flow_io io;
    uint8_t *packet;size_t packet_capacity;
    uint64_t boot;
    uint32_t epoch,debounce_us;
} ss_flow_config;
typedef struct {
    ss_flow_config config;
    ss_identity identity;
    ss_capture_pair capture;
    ss_pair_job exporter;
    ss_pair_source source;
    enum ss_flow_state state;
    uint64_t trigger_us,last_us,raw_since_us,transport_done_us;
    uint64_t accepted,busy_presses,delivered,failed;
    int initialized,key_initialized,raw_pressed,stable_pressed,key_armed;
    int capture_owned,send_owned,capture_cancelled,send_cancelled,error;
} ss_flow;

/* Zero-initialized, private CPU0 control; config/packet/runtime disjoint.
 * Debounce is an electrical filter, not a shot-processing time limit.
 * A key held at startup must be released before it can trigger. */
int ss_flow_init(ss_flow *,const ss_flow_config *);
/* Admit one producer frame independently of any physical button. The
 * continuous app calls only when idle and a private result slot is free. */
int ss_flow_start(ss_flow *,uint64_t);
/* Call in the main loop with the sampled active-low PS_KEY1 level converted
 * to pressed=1. Never run inference or network operations in the key ISR.
 * Busy presses are discarded, never queued. One work step per iteration. */
int ss_flow_step(ss_flow *,int pressed);
/* Optional local abort; does not release buffers or reset hardware. */
void ss_flow_abort(ss_flow *,int);
#endif
