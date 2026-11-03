#ifndef SS_LEGACY_BACKEND_H
#define SS_LEGACY_BACKEND_H
#include <stdint.h>
#include <stddef.h>
#include "retained_neural_driver.h"

/* Opaque compute ABI: camera's rc_device never sees the legacy rc_* types.
 * Results use RN_* status codes. All storage is static and caller-owned. */
typedef struct {
    rn_io io;
    uint32_t base,build,timeout_us,cycle_limit;
    uint64_t (*ticks)(void);
} ss_lg_config;
typedef struct {
    void *state;size_t state_bytes;
    void *mailbox;size_t mailbox_bytes; /* normal shareable non-cacheable */
    void *tensor[2],*weights,*filter,*floats;size_t float_bytes;
} ss_lg_storage;
size_t ss_lg_state_bytes(void);
size_t ss_lg_mailbox_bytes(void);
size_t ss_lg_float_bytes(void);
int ss_lg_init(const ss_lg_storage *,const ss_lg_config *);
int ss_lg_join_cpu1(void *);
int ss_lg_ready(const void *);
int ss_lg_begin(void *,uint64_t,uint64_t,unsigned,const float *const[6],float *);
int ss_lg_step(void *);
int ss_lg_worker_step(void *);
int ss_lg_cancel(void *);
int ss_lg_release(void *);
int ss_lg_owned(const void *);
#endif
