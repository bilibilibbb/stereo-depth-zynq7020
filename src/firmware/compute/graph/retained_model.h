#ifndef RETAINED_MODEL_H
#define RETAINED_MODEL_H
#include <stddef.h>
#include <stdint.h>
#ifdef _MSC_VER
#define RN_API __declspec(dllexport)
#else
#define RN_API
#endif
typedef struct {
    int ci,co,k,stride,dilation,groups,input_frac,output_frac,right_shift;
    const int16_t *weights;
    const int64_t *bias;
} rn_parameter;
typedef int (*rn_conv_backend)(void *,int,const rn_parameter *,const int16_t *,int,int,int16_t *,int,int);
#define RN_DIRECT_BUFFERS_API 1
/* Caller-owned, nonoverlapping I16 buffers. The caller must hold the backend
 * lease for the full synchronous forward call; cache maintenance remains the
 * backend's responsibility. Capacities are in bytes. */
typedef struct {
    int16_t *input,*output;
    size_t input_bytes,output_bytes;
} rn_i16_buffers;
/* No heap or ground truth. All inputs are planar float32 with fixed geometry.
 * The optional backend receives actual I16 tensors and returns raw I16 outputs.
 * A null backend executes the independent portable integer reference.
 */
RN_API size_t retained_model_workspace_bytes(void);
RN_API size_t retained_model_workspace_bytes_buffers(void);
RN_API int retained_model_forward(const float *left,const float *right,const float *native,const float *gray,const float *right_gray,const float *curve,
    float *output,void *workspace,size_t bytes,rn_conv_backend backend,void *user);
RN_API int retained_model_forward_buffers(const float *left,const float *right,const float *native,const float *gray,const float *right_gray,const float *curve,
    float *output,void *workspace,size_t bytes,rn_conv_backend backend,void *user,const rn_i16_buffers *buffers);
RN_API int retained_final_lut_init(void);
RN_API void retained_final_lut_release(void);
RN_API void retained_final_lut_stats(uint64_t *,uint64_t *);
#endif
