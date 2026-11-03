#ifndef FW_OBSERVATIONS_H
#define FW_OBSERVATIONS_H
#include "fw_sparse.h"
enum {FW_OBS_WIDTH=320,FW_OBS_HEIGHT=240,FW_OBS_N=81920,FW_OBS_C=5120};
typedef struct {
 const uint8_t *rgb;
 const float *normalized;
 float *gray;uint32_t *census;
 float *model_image;
 uint32_t row,error;
 uint8_t luma[5][FW_OBS_WIDTH];
} fw_observation_prepare;
typedef struct {
 const uint64_t *packets;const uint16_t *records;
 float *native,*curve;uint32_t quarter_row,error;
} fw_observation_decode;
/* Exactly one RGB source: native interleaved U8, or original padded CHW
 * normalized F32. The latter follows frozen rgb_from_normalized, without
 * materializing a separate full RGB image. Raw RGB avoids that inverse work.
 * Caller owns disjoint source/gray/census spans through completion. No cache
 * publication happens here; the frame/driver boundary owns that operation. */
int fw_observations_prepare_begin(fw_observation_prepare *,const uint8_t *,const float *,float *,uint32_t *);
/* Original rectified RGB also produces the existing normalized CHW input.
 * Exact768-entry FP32 map; top16 rows replicate the first image row. Every
 * destination is private and disjoint until all240 row steps complete. */
int fw_observations_prepare_rgb_begin(fw_observation_prepare *,const uint8_t *,float *,float *,uint32_t *);
/* One native image row; five luma rows retain the complete Census5x5 halo.
 * FW_PENDING means another row is needed, FW_OK means all outputs are ready. */
int fw_observations_prepare_step(fw_observation_prepare *);
/* Frozen retained SGM packets and36-U16 cell records, with exact original
 * F32 divisions. One four-row native band and one curve row per step.
 * Invalid records never authorize input publication, even after partial
 * writes. No full output memset and no additional native/curve payload. */
int fw_observations_decode_begin(fw_observation_decode *,const uint64_t *,const uint16_t *,float *,float *);
int fw_observations_decode_step(fw_observation_decode *);
#endif
