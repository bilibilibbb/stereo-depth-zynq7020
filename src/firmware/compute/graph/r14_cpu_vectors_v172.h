#ifndef R14_CPU_VECTORS_V172_H
#define R14_CPU_VECTORS_V172_H
#if defined(R14_HOST_REFERENCE) || defined(_MSC_VER)
#include <emmintrin.h>
typedef __m128 r14_vf172;
static inline r14_vf172 v172_load(const float *p){return _mm_loadu_ps(p);}
static inline void v172_store(float *p,r14_vf172 v){_mm_storeu_ps(p,v);}
static inline r14_vf172 v172_splat(float v){return _mm_set1_ps(v);}
static inline r14_vf172 v172_add(r14_vf172 a,r14_vf172 b){return _mm_add_ps(a,b);}
static inline r14_vf172 v172_sub(r14_vf172 a,r14_vf172 b){return _mm_sub_ps(a,b);}
static inline r14_vf172 v172_mul(r14_vf172 a,r14_vf172 b){return _mm_mul_ps(a,b);}
static inline r14_vf172 v172_clamp(r14_vf172 v,float low,float high){
 r14_vf172 l=v172_splat(low),h=v172_splat(high),m=_mm_cmplt_ps(v,l);v=_mm_or_ps(_mm_and_ps(m,l),_mm_andnot_ps(m,v));
 m=_mm_cmpgt_ps(v,h);return _mm_or_ps(_mm_and_ps(m,h),_mm_andnot_ps(m,v));
}
static inline r14_vf172 v172_stride4(const float *p,int index){return _mm_set_ps(p[index+12],p[index+8],p[index+4],p[index]);}
static inline void v172_rgb_store(uint8_t *dst,r14_vf172 value){
 __m128i q=_mm_cvttps_epi32(value),one=_mm_set1_epi32(1);int32_t words[4];
 r14_vf172 rest=_mm_sub_ps(value,_mm_cvtepi32_ps(q)),half=v172_splat(.5f);
 __m128i increment=_mm_or_si128(_mm_castps_si128(_mm_cmpgt_ps(rest,half)),_mm_and_si128(_mm_castps_si128(_mm_cmpeq_ps(rest,half)),_mm_cmpeq_epi32(_mm_and_si128(q,one),one)));
 _mm_storeu_si128((__m128i*)words,_mm_add_epi32(q,_mm_and_si128(increment,one)));
 dst[0]=(uint8_t)words[0];dst[1]=(uint8_t)words[1];dst[2]=(uint8_t)words[2];dst[3]=(uint8_t)words[3];
}
#else
#include <arm_neon.h>
typedef float32x4_t r14_vf172;
static inline r14_vf172 v172_load(const float *p){return vld1q_f32(p);}
static inline void v172_store(float *p,r14_vf172 v){vst1q_f32(p,v);}
static inline r14_vf172 v172_splat(float v){return vdupq_n_f32(v);}
static inline r14_vf172 v172_add(r14_vf172 a,r14_vf172 b){return vaddq_f32(a,b);}
static inline r14_vf172 v172_sub(r14_vf172 a,r14_vf172 b){return vsubq_f32(a,b);}
static inline r14_vf172 v172_mul(r14_vf172 a,r14_vf172 b){return vmulq_f32(a,b);}
static inline r14_vf172 v172_clamp(r14_vf172 v,float low,float high){
 r14_vf172 l=v172_splat(low),h=v172_splat(high);v=vbslq_f32(vcltq_f32(v,l),l,v);return vbslq_f32(vcgtq_f32(v,h),h,v);
}
static inline r14_vf172 v172_stride4(const float *p,int index){return vld4q_f32(p).val[index];}
static inline void v172_rgb_store(uint8_t *dst,r14_vf172 value){
 uint32x4_t q=vcvtq_u32_f32(value),one=vdupq_n_u32(1);uint32_t words[4];
 r14_vf172 rest=vsubq_f32(value,vcvtq_f32_u32(q)),half=v172_splat(.5f);
 uint32x4_t increment=vorrq_u32(vcgtq_f32(rest,half),vandq_u32(vceqq_f32(rest,half),vceqq_u32(vandq_u32(q,one),one)));
 vst1q_u32(words,vaddq_u32(q,vandq_u32(increment,one)));
 dst[0]=(uint8_t)words[0];dst[1]=(uint8_t)words[1];dst[2]=(uint8_t)words[2];dst[3]=(uint8_t)words[3];
}
#endif
/* Read exactly one sixteen-float row. Fixed fields avoid the ARM compiler
 * spilling vld4's entire result for a variable field index. */
typedef struct {r14_vf172 c0,c1,c2,c3;} v172_quad;
static inline __attribute__((always_inline)) v172_quad v172_row_once(const float *p){
 v172_quad q;
#if defined(R14_HOST_REFERENCE) || defined(_MSC_VER)
 q.c0=_mm_loadu_ps(p);q.c1=_mm_loadu_ps(p+4);q.c2=_mm_loadu_ps(p+8);q.c3=_mm_loadu_ps(p+12);
 _MM_TRANSPOSE4_PS(q.c0,q.c1,q.c2,q.c3);
#else
 float32x4x4_t v=vld4q_f32(p);q.c0=v.val[0];q.c1=v.val[1];q.c2=v.val[2];q.c3=v.val[3];
#endif
 return q;
}
static inline __attribute__((always_inline)) r14_vf172 v172_colour_acc(r14_vf172 sum,r14_vf172 v,r14_vf172 sd,r14_vf172 mean){
 v=v172_add(v172_mul(v,sd),mean);return v172_add(sum,v172_clamp(v,0,1));
}
/* The original scalar operation sequence runs independently in each lane.
 * Bounded normal image values keep all intermediate values normal or zero.
 * Blocks outside this domain use their original scalar implementation. */
static inline int v172_image_four(const float *p){unsigned k;for(k=0;k<4;k++)if(!(p[k]>=-16 && p[k]<=16))return 0;return 1;}
#endif
