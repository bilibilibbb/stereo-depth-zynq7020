#ifndef Q40_PAIR_V345_H
#define Q40_PAIR_V345_H
#include <stdint.h>
#include <string.h>

/* This integer-only lowering is used after the original whole-input domain
 * preflight. It neither changes FPSCR nor performs floating arithmetic.
 * The host primitives give the same two-lane operations as A9 NEON. */
#ifdef __ARM_NEON
#include <arm_neon.h>
typedef uint64x2_t qp_v345;
static inline qp_v345 qp_load_v345(const void *p){return vreinterpretq_u64_u8(vld1q_u8(p));}
static inline void qp_store_v345(void *p,qp_v345 v){vst1q_u8(p,vreinterpretq_u8_u64(v));}
static inline qp_v345 qp_const_v345(uint64_t n){return vdupq_n_u64(n);}
static inline qp_v345 qp_add_v345(qp_v345 a,qp_v345 b){return vaddq_u64(a,b);}
static inline qp_v345 qp_sub_v345(qp_v345 a,qp_v345 b){return vsubq_u64(a,b);}
static inline qp_v345 qp_and_v345(qp_v345 a,qp_v345 b){return vandq_u64(a,b);}
static inline qp_v345 qp_or_v345(qp_v345 a,qp_v345 b){return vorrq_u64(a,b);}
static inline qp_v345 qp_xor_v345(qp_v345 a,qp_v345 b){return veorq_u64(a,b);}
static inline qp_v345 qp_shift_v345(qp_v345 a,qp_v345 b){return vshlq_u64(a,vreinterpretq_s64_u64(b));}
static inline qp_v345 qp_sign_v345(qp_v345 a){return vreinterpretq_u64_s64(vshrq_n_s64(vreinterpretq_s64_u64(a),63));}
static inline qp_v345 qp_select_v345(qp_v345 mask,qp_v345 a,qp_v345 b){return vbslq_u64(mask,a,b);}
static inline qp_v345 qp_exponent_shift_v345(qp_v345 bits){
 uint32x2_t high=vshrn_n_u64(bits,32);
 int32x2_t e=vreinterpret_s32_u32(vand_u32(vshr_n_u32(high,20),vdup_n_u32(2047)));
 /* NEON register shifts use a signed low byte. Clamp tiny exponents so a
  * shift such as -1035 cannot wrap into a small, nonzero shift. */
 return vreinterpretq_u64_s64(vmovl_s32(vmax_s32(vsub_s32(e,vdup_n_s32(1035)),vdup_n_s32(-64))));
}
#else
typedef struct {uint64_t lane[2];} qp_v345;
static inline qp_v345 qp_load_v345(const void *p){qp_v345 v;memcpy(&v,p,16);return v;}
static inline void qp_store_v345(void *p,qp_v345 v){memcpy(p,&v,16);}
static inline qp_v345 qp_const_v345(uint64_t n){qp_v345 v={{n,n}};return v;}
#define QP_BINARY_V345(name,op) static inline qp_v345 name(qp_v345 a,qp_v345 b){qp_v345 v={{a.lane[0] op b.lane[0],a.lane[1] op b.lane[1]}};return v;}
QP_BINARY_V345(qp_add_v345,+)
QP_BINARY_V345(qp_sub_v345,-)
QP_BINARY_V345(qp_and_v345,&)
QP_BINARY_V345(qp_or_v345,|)
QP_BINARY_V345(qp_xor_v345,^)
#undef QP_BINARY_V345
static inline qp_v345 qp_shift_v345(qp_v345 a,qp_v345 b){
 qp_v345 v;unsigned i;for(i=0;i<2;i++){
  int64_t n;memcpy(&n,b.lane+i,8);
  v.lane[i]=n>=64 || n<=-64?0:(n>=0?a.lane[i]<<n:a.lane[i]>>-n);
 }return v;
}
static inline qp_v345 qp_sign_v345(qp_v345 a){qp_v345 v={{0u-(a.lane[0]>>63),0u-(a.lane[1]>>63)}};return v;}
static inline qp_v345 qp_select_v345(qp_v345 m,qp_v345 a,qp_v345 b){qp_v345 v={{(m.lane[0]&a.lane[0])|(~m.lane[0]&b.lane[0]),(m.lane[1]&a.lane[1])|(~m.lane[1]&b.lane[1])}};return v;}
static inline qp_v345 qp_exponent_shift_v345(qp_v345 bits){
 qp_v345 v;unsigned i;for(i=0;i<2;i++){
  int64_t s=(int64_t)((bits.lane[i]>>52)&2047u)-1035;
  v.lane[i]=(uint64_t)(s<-64?-64:s);
 }return v;
}
#endif

static inline qp_v345 field_q40_pair_value_v345(qp_v345 bits){
 qp_v345 one=qp_const_v345(1),zero=qp_const_v345(0);
 qp_v345 m=qp_or_v345(qp_and_v345(bits,qp_const_v345(UINT64_C(0xfffffffffffff))),qp_const_v345(UINT64_C(1)<<52));
 qp_v345 shift=qp_exponent_shift_v345(bits),raw=qp_shift_v345(m,shift);
 qp_v345 half=qp_shift_v345(one,qp_sub_v345(qp_sub_v345(zero,shift),one));
 /* (m + half - 1 + parity) >> n implements nearest, ties to even.
  * For n>=64 the result is zero. At n=0 or a left shift select raw. */
 qp_v345 rounded=qp_shift_v345(qp_add_v345(qp_sub_v345(qp_add_v345(m,half),one),qp_and_v345(raw,one)),shift);
 qp_v345 q=qp_select_v345(qp_sign_v345(shift),rounded,raw),sign=qp_sign_v345(bits);
 return qp_sub_v345(qp_xor_v345(q,sign),sign);
}
static inline void field_q40_pair_valid_v345(const void *source,void *out){
 qp_store_v345(out,field_q40_pair_value_v345(qp_load_v345(source)));
}
static inline void field_columns_incoming_pair_v345(const void *source,const void *previous,void *row,void *columns){
 /* Ring slots may alias. Load the old pair before writing the new row. */
 qp_v345 prior=qp_load_v345(previous),incoming=field_q40_pair_value_v345(qp_load_v345(source));
 qp_store_v345(columns,qp_sub_v345(qp_add_v345(qp_load_v345(columns),incoming),prior));
 qp_store_v345(row,incoming);
}
static inline void field_columns_cached_pair_v345(const void *incoming,const void *previous,void *columns){
 qp_store_v345(columns,qp_sub_v345(qp_add_v345(qp_load_v345(columns),qp_load_v345(incoming)),qp_load_v345(previous)));
}
#endif
