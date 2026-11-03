#ifndef R14_TYPED_NN_H
#define R14_TYPED_NN_H
#include "r14_resident_nn.h"
/* The source remains in its resident16/32-bit dyadic format. Hardware converts
 * on load; output remains exactly the original raw signed16 convolution. */
typedef int (*r14_typed_backend)(void *,int,const rn_parameter *,const void *,int,int,int16_t *,int,int,int,int,int);
int r14_typed_convolution(void *,int,const rn_parameter *,const void *,int,int,int16_t *,int,int,int,int,int);
#endif
