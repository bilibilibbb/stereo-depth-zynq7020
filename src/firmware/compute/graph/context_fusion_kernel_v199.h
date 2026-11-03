#ifndef CONTEXT_FUSION_KERNEL_V199_H
#define CONTEXT_FUSION_KERNEL_V199_H
#include <stdint.h>
#include <stddef.h>
/* Exact accepted FP32 correlation expressed as a rounded dyadic numerator.
 * This is the software oracle for a future integer PL stream, not an ARM
 * performance optimization. Every multiply/add keeps its original rounding. */
RN_API int64_t r14_round_fp24_v199(int64_t value);
RN_API int r14_context_fuse_v199(const int16_t *left,const int16_t *right,
    const float *prior,int16_t *out,size_t values);
RN_API int r14_context_directed_v199(uint32_t count);
#endif
