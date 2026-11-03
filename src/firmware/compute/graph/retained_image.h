#ifndef RETAINED_IMAGE_H
#define RETAINED_IMAGE_H
#include "retained_model.h"
#include <stddef.h>
#include <stdint.h>
RN_API int r14_yuyv_rgb(const uint8_t *raw,size_t bytes,uint32_t width,uint32_t height,uint32_t stride,int full_range,uint8_t *rgb,uint8_t *gray);
RN_API int r14_rectify_rgb(const uint8_t *src,uint32_t sw,uint32_t sh,const float *map_xy,uint32_t dw,uint32_t dh,uint8_t *dst,uint8_t *valid);
RN_API int r14_normalize_pad(const uint8_t *rgb,uint32_t w,uint32_t h,uint32_t pw,uint32_t ph,float *out);
RN_API int r14_pack_disparity(const float *padded,uint32_t w,uint32_t h,uint32_t pw,uint32_t ph,const uint8_t *roi,uint16_t *q,uint8_t *valid);
RN_API int r14_depth_mm(const uint16_t *q,const uint8_t *valid,uint32_t w,uint32_t h,const float matrix_q[16],float *z);
/* BGR888 contiguous framebuffer, common fixed 0..64 disparity scale. */
RN_API int r14_render_depth(const uint8_t *rgb,const uint16_t *q,const uint8_t *valid,uint32_t w,uint32_t h,
    uint32_t frame_id,uint32_t elapsed_ms,int stale,uint8_t *bgr,uint32_t dw,uint32_t dh);
RN_API int r14_render_annotation(uint8_t *bgr,int sgm,int quant16,const float *depth_mm,uint32_t w,uint32_t h,int calibrated);
#endif
