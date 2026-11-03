/* Diagnostic host adaptation only: IEEE binary32 word access and the exact
 * normal/subnormal constants from preserved fdlibm_upstream.h. Algorithm
 * source files and their copyright notices remain unchanged. */
#ifndef FW_DIAG_FDLIBM_H
#define FW_DIAG_FDLIBM_H
#include <stdint.h>
#include <string.h>
#include <math.h>
typedef int32_t __int32_t;
typedef uint32_t __uint32_t;
#define GET_FLOAT_WORD(i,d) do{uint32_t fw_bits;float fw_value=(d);memcpy(&fw_bits,&fw_value,4);(i)=fw_bits;}while(0)
#define SET_FLOAT_WORD(d,i) do{uint32_t fw_bits=(uint32_t)(i);memcpy(&(d),&fw_bits,4);}while(0)
#define FLT_UWORD_IS_FINITE(x) ((x)<0x7f800000L)
#define FLT_UWORD_IS_NAN(x) ((x)>0x7f800000L)
#define FLT_UWORD_IS_INFINITE(x) ((x)==0x7f800000L)
#define FLT_UWORD_LOG_MAX 0x42b17217
#define FLT_UWORD_IS_ZERO(x) ((x)==0)
#define FLT_UWORD_IS_SUBNORMAL(x) ((x)<0x00800000L)
#define FLT_UWORD_LOG_MIN 0x42cff1b5
float fw_diag_newlib_expf(float);
float fw_diag_newlib_logf(float);
float fw_diag_newlib_tanhf(float);
float fw_diag_newlib_expm1f(float);
#define __ieee754_expf fw_diag_newlib_expf
#define __ieee754_logf fw_diag_newlib_logf
#define tanhf fw_diag_newlib_tanhf
#define expm1f fw_diag_newlib_expm1f
#endif
