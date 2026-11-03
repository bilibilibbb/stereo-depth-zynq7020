#ifndef R14_EXACT_CPU_HOTSPOTS_V169_H
#define R14_EXACT_CPU_HOTSPOTS_V169_H
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#if FLT_RADIX!=2 || FLT_MANT_DIG!=24 || FLT_MAX_EXP!=128
#error Requires the existing IEEE binary32 model representation
#endif
/* ldexpf(1,-fraction) is an exactly representable normal power of two
 * for every current tensor format. The original libm remains the fallback. */
static inline float r14_inverse_scale_v169(int fraction){
 if(fraction>=-126 && fraction<=126){uint32_t bits=(uint32_t)(127-fraction)<<23;float value;memcpy(&value,&bits,4);return value;}
 return ldexpf(1.0f,-fraction);
}
/* Current warp coordinates lie within [-63,319]. This wider bounded path
 * has exact integer/float conversions, including the sign of zero. */
static inline float r14_floor_v169(float value){
 if(value>=-4096.0f && value<=4096.0f){int whole;if(value==0)return value;whole=(int)value;if((float)whole>value)whole--;return (float)whole;}
 return floorf(value);
}
/* Only the already-clamped finite [0,255] RGB branch calls this. Integer
 * truncation and the exactly representable remainder retain ties to even. */
static inline uint8_t r14_rgb_round_v169(float value){
 uint32_t whole=(uint32_t)value;float remainder=value-(float)whole;
 if(remainder>.5f || (remainder==.5f && (whole&1u)))whole++;
 return (uint8_t)whole;
}
#endif
