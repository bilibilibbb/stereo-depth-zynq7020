#ifndef RETAINED_MODEL_PL_H
#define RETAINED_MODEL_PL_H
#include "retained_model.h"
#include "retained_neural_driver.h"
typedef struct {
    uint32_t source_format_enabled,source_wide,source_fraction,source_relu;
    rn_device *device;
    int16_t *input,*output,*packed_weights;
    uint32_t input_address,output_address,weight_address;
    uint32_t input_capacity,output_capacity,weight_capacity,weight_used,cycle_limit;
    const rn_parameter *cached_parameter[64];
    uint32_t parameter_offset[64];
    uint32_t layer_calls,dma_jobs;
} rn_pl_context;
/* The caller reserves nonoverlapping,cache-line-aligned DDR buffers in the
 * hardware's0x19000000..0x1a000000 region and opens the matching rn_device.
 * Capacities are in bytes. Pointer/address mappings are supplied by the BSP.
 */
int rn_pl_init(rn_pl_context *context);
int rn_pl_convolution(void *context,int parameter_id,const rn_parameter *parameter,const int16_t *source,int ih,int iw,int16_t *destination,int oh,int ow);
#endif
