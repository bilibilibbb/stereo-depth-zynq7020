#include "retained_model.h"
#ifndef R14_FINAL_LUT_V1_H
#define R14_FINAL_LUT_V1_H
/* The frozen last convolution emits INT16 /4096. Its exact 2x bilinear
 * interpolation has weights in quarters on each axis, so the final values
 * lie on a 1/65536 grid. This table is built with the CURRENT platform libm:
 * no approximation, host-generated ARM values, or changed rounding order.
 * Values outside the exact grid (including -0 and nonfinite values) keep
 * the original libm path. Initialize once before inference, single caller.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define R14_FINAL_GRID_MIN (-524288)
#define R14_FINAL_GRID_MAX 524272
#define R14_FINAL_GRID_COUNT 1048561u
typedef struct {float gate,tanh_value;} r14_final_entry;
/* Same permanent DDR storage as the former allocation; outside libc heap. */
_Alignas(64) static r14_final_entry ss_final_lut_storage[R14_FINAL_GRID_COUNT];
static r14_final_entry *r14_final_values_v1;
static uint64_t r14_final_hits_v1,r14_final_misses_v1;
RN_API size_t retained_final_lut_bytes(void){return (size_t)R14_FINAL_GRID_COUNT*sizeof(r14_final_entry);}
RN_API int retained_final_lut_init(void){
    int k;r14_final_entry *p;
    if(r14_final_values_v1)return 0;
    p=ss_final_lut_storage;
    for(k=R14_FINAL_GRID_MIN;k<=R14_FINAL_GRID_MAX;k++){
        float v=(float)k*(1.0f/65536.0f);size_t index=(size_t)(k-R14_FINAL_GRID_MIN);
        p[index].gate=1.0f/(1.0f+expf(-v));p[index].tanh_value=tanhf(v);
    }
    r14_final_values_v1=p;return 0;
}
RN_API void retained_final_lut_release(void){
    r14_final_values_v1=0;r14_final_hits_v1=0;r14_final_misses_v1=0;
}
RN_API void retained_final_lut_stats(uint64_t *hits,uint64_t *misses){
    if(hits)*hits=r14_final_hits_v1;
    if(misses)*misses=r14_final_misses_v1;
}
static int r14_final_index_v1(float v){
    int k;float scaled;uint32_t bits;
    if(!r14_final_values_v1 || !(v>=-8.0f && v<=7.999755859375f))return -1;
    memcpy(&bits,&v,sizeof(bits));if(bits==0x80000000u)return -1;
    scaled=v*65536.0f;k=(int)scaled;
    if((float)k!=scaled)return -1;
    return k-R14_FINAL_GRID_MIN;
}
static float r14_final_gate_v1(float v){
    int k=r14_final_index_v1(v);
    if(k>=0){r14_final_hits_v1++;return r14_final_values_v1[k].gate;}
    r14_final_misses_v1++;return 1.0f/(1.0f+expf(-v));
}
static float r14_final_tanh_v1(float v){
    int k=r14_final_index_v1(v);
    if(k>=0){r14_final_hits_v1++;return r14_final_values_v1[k].tanh_value;}
    r14_final_misses_v1++;return tanhf(v);
}
#endif
