#ifndef SNAPSHOT_SMALL_MODEL_H
#define SNAPSHOT_SMALL_MODEL_H
#include "pair_export.h"
#include "fw_observations.h"
#include "fw_sgm_driver.h"
#include "legacy/backend.h"

enum {SS_BUSY=-5,SS_CLOCK=-6,SS_BACKEND=-7,SS_EXHAUSTED=-8,SS_IO=-9};
typedef struct {
    void *system;
    ss_lg_storage storage;
    rs_device *sgm;
    fw_cache cache;
    float *input[6],*output;
    rs_frame dma;
    uint64_t (*now_us)(void *);
    void *clock_user;
    void (*notify)(void *);
} ss_model_config;
typedef struct {
    ss_model_config config;
    _Alignas(64) fw_observation_prepare left;
    _Alignas(64) fw_observation_prepare right;
    _Alignas(64) uint32_t right_state,right_error,cancelled;
    fw_observation_decode decode;
    ss_pair_source source;
    uint64_t last_us;
    unsigned phase,left_done,recover_clear;
    int initialized,active,ready,sgm_seen,error,backend_error;
} ss_model;
/* CPU0 owns the graph, PL drivers and the public lifetime. CPU1 independently
 * calls worker_step plus ss_lg_worker_step; buffers remain owned through drain. */
int ss_model_init(ss_model *,const ss_model_config *);
int ss_model_start(ss_model *,ss_identity,uint64_t,uint64_t,const uint8_t *,const uint8_t *);
int ss_model_step(ss_model *);
int ss_model_worker_step(ss_model *);
int ss_model_source(const ss_model *,ss_pair_source *);
int ss_model_release(ss_model *);
void ss_model_cancel(ss_model *,int);
int ss_model_packet_private(const ss_model *,const void *,size_t);
#endif
