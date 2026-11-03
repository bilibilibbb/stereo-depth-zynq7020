#ifndef Q40_PACK_V317_H
#define Q40_PACK_V317_H
#include <stdint.h>
#include <string.h>
/* Exactly RNE((double)signed64)*2^-40. The scale is a normal power-of-two
 * for every signed64 value, so it introduces no second rounding. There is
 * no approximation, table, floating reassociation or changed Q40 domain. */
static uint64_t field_q40_bits_v317(int64_t value){
 uint64_t sign=(uint64_t)(value<0)<<63;
 uint64_t magnitude=value<0?UINT64_C(0)-(uint64_t)value:(uint64_t)value,sig;
 uint32_t high=(uint32_t)(magnitude>>32);unsigned k,exp;
 if(!magnitude)return 0;
 k=high?63u-(unsigned)__builtin_clz(high):31u-(unsigned)__builtin_clz((uint32_t)magnitude);
 exp=983u+k;
 if(k<=52u)sig=magnitude<<(52u-k);
 else{
  unsigned shift=k-52u;uint32_t rem=(uint32_t)magnitude&((1u<<shift)-1u),half=1u<<(shift-1u);
  sig=magnitude>>shift;
  if(rem>half || (rem==half && (sig&1u)))sig++;
  if(sig==(UINT64_C(1)<<53)){sig>>=1;exp++;}
 }
 return sign|((uint64_t)exp<<52)|(sig&UINT64_C(0xfffffffffffff));
}
static double field_q40_to_f64_v317(int64_t value){
 /* All intermediate scalings are exact; only this addition rounds. */
 int32_t high=(int32_t)((uint64_t)value>>32);uint32_t low=(uint32_t)(uint64_t)value;
 double upper=(double)high*0x1p-8,lower=(double)low*0x1p-40;return upper+lower;
}
#endif
