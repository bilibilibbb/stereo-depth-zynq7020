#ifndef SUPPLY_QUANT_BIAS_V186_H
#define SUPPLY_QUANT_BIAS_V186_H
/* Finite F32 inputs, dyadic scale, round-to-nearest-even. A binary32 addition
 * within the unit-spaced interval around 1.5*2^23 rounds to the same integer
 * as the original double scalar reference. Tiny/clipped values are masked
 * before arithmetic; no control-register writes or relaxed floating math. */
static int16_t r14_quant_bias_scalar_v186(float x,int fraction){
 float magnitude=fabsf(x),scale=ldexpf(1.0f,fraction),limit=ldexpf(32767.0f,-fraction),zero=ldexpf(.5f,-fraction);uint32_t bits;volatile float rounded;
 if(!isfinite(x))return 0;
 if(magnitude>=limit)return x<0?-32767:32767;
 if(magnitude<=zero)return 0;
 rounded=magnitude*scale+12582912.0f;{float value=rounded;memcpy(&bits,&value,4);}bits-=0x4b400000u;return (int16_t)(x<0?-(int32_t)bits:(int32_t)bits);
}
static void r14_quantize_bias_v186(const float *src,int16_t *dst,size_t count,int fraction){
 size_t i=0;
#if !defined(_MSC_VER) && !defined(R14_HOST_REFERENCE)
 float32x4_t scale=vdupq_n_f32(ldexpf(1.0f,fraction)),limit=vdupq_n_f32(ldexpf(32767.0f,-fraction)),zero=vdupq_n_f32(ldexpf(.5f,-fraction)),magic=vdupq_n_f32(12582912.0f);
 for(;i+8<=count;i+=8){
  int32x4_t out[2];unsigned lane;
  for(lane=0;lane<2;lane++){
   float32x4_t x=vld1q_f32(src+i+4u*lane),m=vabsq_f32(x);uint32x4_t clipped=vcgeq_f32(m,limit),active=vandq_u32(vcgtq_f32(m,zero),vmvnq_u32(clipped)),q;int32x4_t sign=vshrq_n_s32(vreinterpretq_s32_f32(x),31);
   m=vbslq_f32(active,m,vdupq_n_f32(0));q=vsubq_u32(vreinterpretq_u32_f32(vaddq_f32(vmulq_f32(m,scale),magic)),vdupq_n_u32(0x4b400000u));q=vbslq_u32(clipped,vdupq_n_u32(32767u),q);out[lane]=vsubq_s32(veorq_s32(vreinterpretq_s32_u32(q),sign),sign);
  }
  vst1q_s16(dst+i,vcombine_s16(vmovn_s32(out[0]),vmovn_s32(out[1])));
 }
#endif
 for(;i<count;i++)dst[i]=r14_quant_bias_scalar_v186(src[i],fraction);
}
#endif
