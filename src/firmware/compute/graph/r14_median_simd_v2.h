#ifndef R14_MEDIAN_SIMD_V2_H
#define R14_MEDIAN_SIMD_V2_H
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
typedef float32x4_t m9_vec;
typedef uint32x4_t m9_mask;
#define M9_LOAD(p) vld1q_f32(p)
#define M9_STORE(p,v) vst1q_f32(p,v)
#define M9_GT(a,b) vcgtq_f32(a,b)
#define M9_SELECT(m,a,b) vbslq_f32(m,a,b)
#define M9_OR(a,b) vorrq_u32(a,b)
static inline m9_mask m9_bad(m9_vec v){
    uint32x4_t z=vdupq_n_u32(0),all=vdupq_n_u32(0x7f800000u);
    uint32x4_t mag=vandq_u32(vreinterpretq_u32_f32(v),vdupq_n_u32(0x7fffffffu));
    uint32x4_t exp=vandq_u32(mag,all);
    return vorrq_u32(vceqq_u32(exp,all),vandq_u32(vceqq_u32(exp,z),vcgtq_u32(mag,z)));
}
static inline int m9_any(m9_mask m){return (vgetq_lane_u32(m,0)|vgetq_lane_u32(m,1)|vgetq_lane_u32(m,2)|vgetq_lane_u32(m,3))!=0;}
#else
#include <emmintrin.h>
typedef __m128 m9_vec;
typedef __m128 m9_mask;
#define M9_LOAD(p) _mm_loadu_ps(p)
#define M9_STORE(p,v) _mm_storeu_ps(p,v)
#define M9_GT(a,b) _mm_cmpgt_ps(a,b)
#define M9_SELECT(m,a,b) _mm_or_ps(_mm_and_ps(m,a),_mm_andnot_ps(m,b))
#define M9_OR(a,b) _mm_or_ps(a,b)
static inline m9_mask m9_bad(m9_vec v){
    __m128i z=_mm_setzero_si128(),all=_mm_set1_epi32(0x7f800000);
    __m128i mag=_mm_and_si128(_mm_castps_si128(v),_mm_set1_epi32(0x7fffffff));
    __m128i exp=_mm_and_si128(mag,all);
    return _mm_castsi128_ps(_mm_or_si128(_mm_cmpeq_epi32(exp,all),_mm_and_si128(_mm_cmpeq_epi32(exp,z),_mm_cmpgt_epi32(mag,z))));
}
static inline int m9_any(m9_mask m){return _mm_movemask_ps(m)!=0;}
#endif
/* Use comparison masks rather than min/max: equal values retain their original
 * signed-zero/payload order. ARM SIMD denormal/nonfinite behavior can differ
 * from VFP, so every block containing those values uses the unchanged scalar
 * pixel function. All36 original comparisons run in the same order. */
#define M9_SWAP(a,b) do{m9_mask m=M9_GT(a,b);m9_vec t=M9_SELECT(m,b,a);b=M9_SELECT(m,a,b);a=t;}while(0)
static void r14_median_rows_v179(const float *src,float *dst,float (*scalar)(const float *,int,int),int first,int last){
    int y,x,j;for(y=first;y<last;y++){
        int y0=y?y-1:0,y2=y<239?y+1:239;
        const float *r0=src+y0*320,*r1=src+y*320,*r2=src+y2*320;
        dst[y*320]=scalar(src,y,0);
        for(x=1;x+3<319;x+=4){
            m9_vec a0=M9_LOAD(r0+x-1),a1=M9_LOAD(r0+x),a2=M9_LOAD(r0+x+1);
            m9_vec a3=M9_LOAD(r1+x-1),a4=M9_LOAD(r1+x),a5=M9_LOAD(r1+x+1);
            m9_vec a6=M9_LOAD(r2+x-1),a7=M9_LOAD(r2+x),a8=M9_LOAD(r2+x+1);
            m9_mask bad=M9_OR(M9_OR(M9_OR(m9_bad(a0),m9_bad(a1)),M9_OR(m9_bad(a2),m9_bad(a3))),
                M9_OR(M9_OR(m9_bad(a4),m9_bad(a5)),M9_OR(M9_OR(m9_bad(a6),m9_bad(a7)),m9_bad(a8))));
            if(m9_any(bad)){for(j=0;j<4;j++)dst[y*320+x+j]=scalar(src,y,x+j);continue;}
            M9_SWAP(a0,a1);M9_SWAP(a2,a3);M9_SWAP(a4,a5);M9_SWAP(a6,a7);
            M9_SWAP(a1,a2);M9_SWAP(a3,a4);M9_SWAP(a5,a6);M9_SWAP(a7,a8);
            M9_SWAP(a0,a1);M9_SWAP(a2,a3);M9_SWAP(a4,a5);M9_SWAP(a6,a7);
            M9_SWAP(a1,a2);M9_SWAP(a3,a4);M9_SWAP(a5,a6);M9_SWAP(a7,a8);
            M9_SWAP(a0,a1);M9_SWAP(a2,a3);M9_SWAP(a4,a5);M9_SWAP(a6,a7);
            M9_SWAP(a1,a2);M9_SWAP(a3,a4);M9_SWAP(a5,a6);M9_SWAP(a7,a8);
            M9_SWAP(a0,a1);M9_SWAP(a2,a3);M9_SWAP(a4,a5);M9_SWAP(a6,a7);
            M9_SWAP(a1,a2);M9_SWAP(a3,a4);M9_SWAP(a5,a6);M9_SWAP(a7,a8);
            M9_SWAP(a0,a1);M9_SWAP(a2,a3);M9_SWAP(a4,a5);M9_SWAP(a6,a7);
            M9_STORE(dst+y*320+x,a4);
        }
        for(;x<320;x++)dst[y*320+x]=scalar(src,y,x);
    }
}
static void r14_median_simd_v2(const float *s,float *d,float (*f)(const float *,int,int)){r14_median_rows_v179(s,d,f,0,240);}
#undef M9_SWAP
#endif
