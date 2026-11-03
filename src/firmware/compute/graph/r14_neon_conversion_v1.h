#ifndef R14_NEON_CONVERSION_V1_H
#define R14_NEON_CONVERSION_V1_H
#include <arm_neon.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* Finite binary32, frozen fractions 11..14, symmetric I16 saturation.
 * Guard tiny/large magnitudes before SIMD multiplication: every remaining
 * arithmetic value is normal and exactly representable in binary32.
 * Explicit halfway parity preserves the original ties-to-even conversion.
 * No FPSCR control writes, fast-math, cross-tensor transport or allocation. */
static inline int32x4_t r14_quant4(float32x4_t x, float32x4_t scale,
    float32x4_t limit, float32x4_t zero) {
    const uint32x4_t one=vdupq_n_u32(1u);
    const float32x4_t half=vdupq_n_f32(0.5f);
    float32x4_t magnitude=vabsq_f32(x),scaled,rest;
    uint32x4_t clipped=vcgeq_f32(magnitude,limit),active;
    uint32x4_t whole,increment,q;
    int32x4_t sign=vshrq_n_s32(vreinterpretq_s32_f32(x),31);
    active=vandq_u32(vcgtq_f32(magnitude,zero),vmvnq_u32(clipped));
    magnitude=vbslq_f32(active,magnitude,vdupq_n_f32(0.0f));
    scaled=vmulq_f32(magnitude,scale);
    whole=vcvtq_u32_f32(scaled);
    rest=vsubq_f32(scaled,vcvtq_f32_u32(whole));
    increment=vorrq_u32(vcgtq_f32(rest,half),
        vandq_u32(vceqq_f32(rest,half),vceqq_u32(vandq_u32(whole,one),one)));
    q=vaddq_u32(whole,vandq_u32(increment,one));
    q=vbslq_u32(clipped,vdupq_n_u32(32767u),q);
    return vsubq_s32(veorq_s32(vreinterpretq_s32_u32(q),sign),sign);
}
static inline int16_t r14_quant_tail(float x,double scale) {
    double a=fabs((double)x)*scale,rest;uint32_t whole;
    if(a>=32767.0)return x<0?-32767:32767;
    whole=(uint32_t)a;rest=a-whole;
    if(rest>.5 || (rest==.5 && (whole&1u)))whole++;
    return (int16_t)(x<0?-(int32_t)whole:(int32_t)whole);
}
static inline void r14_quantize_neon_v1(const float *src,int16_t *dst,size_t n,int fraction) {
    size_t i=0;float s=ldexpf(1.0f,fraction);
    float32x4_t scale=vdupq_n_f32(s),limit=vdupq_n_f32(ldexpf(32767.0f,-fraction));
    float32x4_t zero=vdupq_n_f32(ldexpf(0.5f,-fraction));
    for(;i+8<=n;i+=8) {
        int32x4_t a=r14_quant4(vld1q_f32(src+i),scale,limit,zero);
        int32x4_t b=r14_quant4(vld1q_f32(src+i+4),scale,limit,zero);
        vst1q_s16(dst+i,vcombine_s16(vmovn_s32(a),vmovn_s32(b)));
    }
    for(;i<n;i++)dst[i]=r14_quant_tail(src[i],(double)s);
}
static inline void r14_dequantize_neon_v1(const int16_t *src,float *dst,size_t n,int fraction,int relu) {
    size_t i=0;float s=ldexpf(1.0f,-fraction);
    float32x4_t scale=vdupq_n_f32(s),zero=vdupq_n_f32(0.0f),six=vdupq_n_f32(6.0f);
    for(;i+8<=n;i+=8) {
        int16x8_t q=vld1q_s16(src+i);
        float32x4_t a=vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(q))),scale);
        float32x4_t b=vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(q))),scale);
        if(relu) {a=vminq_f32(vmaxq_f32(a,zero),six);b=vminq_f32(vmaxq_f32(b,zero),six);}
        vst1q_f32(dst+i,a);vst1q_f32(dst+i+4,b);
    }
    for(;i<n;i++) {float v=(float)src[i]*s;dst[i]=relu?(v<0?0:(v>6?6:v)):v;}
}
#endif
