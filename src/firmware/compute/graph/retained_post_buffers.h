#ifndef RETAINED_POST_BUFFERS_H
#define RETAINED_POST_BUFFERS_H
#include "retained_model.h"
typedef int (*rn_median_backend)(void *,const float *,float *);
typedef struct {
    float *source,*destination;
    size_t source_bytes,destination_bytes;
    rn_median_backend median;
    void *user;
    const uint32_t *ownership;
} rn_post_buffers;
/* The existing synchronous model lease covers both buffers. Ownership zero
 * is required before any model work; a failed DMA remains quarantined.
 * Both are full320x240 FP32, disjoint, cache-line aligned. No CPU staging copy. */
RN_API int retained_model_forward_post(const float *,const float *,const float *,const float *,const float *,const float *,
    float *,void *,size_t,rn_conv_backend,void *,const rn_i16_buffers *,const rn_post_buffers *);
#endif
