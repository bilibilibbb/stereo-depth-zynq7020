#ifndef R14_NEON_TRANSPORT_V3_H
#define R14_NEON_TRANSPORT_V3_H
#include <stdint.h>
#include <stddef.h>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
typedef int16x8_t t3_vec;
typedef uint16x8_t t3_mask;
#define T3_LOAD(p) vld1q_s16(p)
#define T3_STORE(p,v) vst1q_s16(p,v)
#define T3_DUP(v) vdupq_n_s16(v)
#define T3_MAX(a,b) vmaxq_s16(a,b)
#define T3_MIN(a,b) vminq_s16(a,b)
#define T3_GT(a,b) vcgtq_s16(a,b)
#define T3_EQ(a,b) vceqq_s16(a,b)
#define T3_AND(a,b) vandq_s16(a,b)
#define T3_MAND(a,b) vandq_u16(a,b)
#define T3_MOR(a,b) vorrq_u16(a,b)
#define T3_SELECT(m,a,b) vbslq_s16(m,a,b)
#define T3_SUB_MASK(a,b) vsubq_s16(a,vreinterpretq_s16_u16(b))
#define T3_LEFT(v,n) vshlq_s16(v,vdupq_n_s16(n))
#define T3_RIGHT(v,n) vshlq_s16(v,vdupq_n_s16(-(n)))
#else
#include <emmintrin.h>
typedef __m128i t3_vec;
typedef __m128i t3_mask;
#define T3_LOAD(p) _mm_loadu_si128((const __m128i *)(p))
#define T3_STORE(p,v) _mm_storeu_si128((__m128i *)(p),v)
#define T3_DUP(v) _mm_set1_epi16(v)
#define T3_MAX(a,b) _mm_max_epi16(a,b)
#define T3_MIN(a,b) _mm_min_epi16(a,b)
#define T3_GT(a,b) _mm_cmpgt_epi16(a,b)
#define T3_EQ(a,b) _mm_cmpeq_epi16(a,b)
#define T3_AND(a,b) _mm_and_si128(a,b)
#define T3_MAND(a,b) _mm_and_si128(a,b)
#define T3_MOR(a,b) _mm_or_si128(a,b)
#define T3_SELECT(m,a,b) _mm_or_si128(_mm_and_si128(m,a),_mm_andnot_si128(m,b))
#define T3_SUB_MASK(a,b) _mm_sub_epi16(a,b)
#define T3_LEFT(v,n) _mm_sll_epi16(v,_mm_cvtsi32_si128(n))
#define T3_RIGHT(v,n) _mm_sra_epi16(v,_mm_cvtsi32_si128(n))
#endif

static inline t3_vec t3_relu(t3_vec v,int relu,int cap){
    if(relu){v=T3_MAX(v,T3_DUP(0));if(cap<=32767)v=T3_MIN(v,T3_DUP((int16_t)cap));}return v;
}
static int16_t r14_transport_tail_v3(int16_t x,int from,int to,int relu){
    int32_t v=x,cap=6*(1<<from),shift=to-from;uint32_t mag,q;
    if(relu){if(v<0)v=0;if(v>cap)v=cap;}
    mag=(uint32_t)(v<0?-v:v);
    if(shift>=0)q=mag<<shift;
    else{uint32_t rest=mag&((1u<<(-shift))-1u),half=1u<<(-shift-1);q=mag>>(-shift);if(rest>half||(rest==half&&(q&1u)))q++;}
    if(q>32767u)q=32767u;
    return (int16_t)(v<0?-(int32_t)q:(int32_t)q);
}
/* Input/output fractions remain frozen9..13/11..14. Signed arithmetic right
 * shift gives floor(x/2^r); adding one for remainder>half or odd halfway floor
 * is exactly ties-to-even for positive AND negative inputs. Left-shift overflow
 * is selected using the original unshifted input and symmetric32767 bounds.
 * Ordinary16-bit SIMD operations never set the saturating-arithmetic QC bit. */
static void r14_transport_neon_v3(const int16_t *src,int16_t *dst,size_t n,int from,int to,int relu){
    size_t i=0;int diff=to-from,cap=6*(1<<from);t3_vec positive=T3_DUP(32767),negative=T3_DUP(-32767);
    if(diff==0){
        for(;i+8<=n;i+=8){t3_vec v=t3_relu(T3_LOAD(src+i),relu,cap);T3_STORE(dst+i,T3_MAX(v,negative));}
    }else if(diff>0){
        int16_t bound=(int16_t)(32767>>diff);t3_vec high=T3_DUP(bound),low=T3_DUP((int16_t)-bound);
        for(;i+8<=n;i+=8){
            t3_vec v=t3_relu(T3_LOAD(src+i),relu,cap),q=T3_LEFT(v,diff);
            q=T3_SELECT(T3_GT(v,high),positive,q);q=T3_SELECT(T3_GT(low,v),negative,q);T3_STORE(dst+i,q);
        }
    }else{
        int r=-diff;t3_vec mask=T3_DUP((int16_t)((1u<<r)-1u)),half=T3_DUP((int16_t)(1u<<(r-1))),one=T3_DUP(1);
        for(;i+8<=n;i+=8){
            t3_vec v=t3_relu(T3_LOAD(src+i),relu,cap),q=T3_RIGHT(v,r),rest=T3_AND(v,mask);
            t3_mask increment=T3_MOR(T3_GT(rest,half),T3_MAND(T3_EQ(rest,half),T3_EQ(T3_AND(q,one),one)));
            T3_STORE(dst+i,T3_SUB_MASK(q,increment));
        }
    }
    for(;i<n;i++)dst[i]=r14_transport_tail_v3(src[i],from,to,relu);
}
#endif
