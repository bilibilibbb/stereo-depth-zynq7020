#ifndef R14_RESIDENT_NN_H
#define R14_RESIDENT_NN_H
#include "retained_model_pl.h"
typedef struct {
    rn_pl_context *neural;
    uint8_t *memory;
    uint32_t address,bytes;
    uint64_t calls,source_bytes,destination_bytes;
} r14_resident_nn;
/* The persistent integer arena and the input bridge occupy one reserved DDR
 * span. Weights are separately reserved. Original driver ownership, cache,
 * tag, timeout and fault handling remain in effect for every actual job. */
int r14_resident_nn_init(r14_resident_nn *,rn_pl_context *,void *,uint32_t,uint32_t);
int r14_resident_convolution(void *,int,const rn_parameter *,const int16_t *,int,int,int16_t *,int,int);
#endif
