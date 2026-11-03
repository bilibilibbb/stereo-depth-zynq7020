#ifndef R14_CPU_TASK_PROBE_V165_H
#define R14_CPU_TASK_PROBE_V165_H
#include "r14_graph_coop.h"
enum {CPU_PROBE_N=81920,CPU_PROBE_CN=5120,CPU_PROBE_PN=76800};
enum cpu_probe_task {CPU_PRIORS,CPU_DECODE_PREPARE,CPU_DECODE_FINISH,CPU_ATTRIBUTES_POOL,CPU_FINAL_PREDICTION,CPU_MEDIAN,CPU_RGB,CPU_EXPORT_COPY,CPU_COARSE_COPY,CPU_GRAY_SNAPSHOT_COPY,CPU_TASK_COUNT};
typedef struct {
 const float *left,*right,*native,*gray,*right_gray,*curve;
 const float *raw_logits,*filtered,*coarse;
 const int32_t *final_i32;
} cpu_probe_input;
typedef struct {
 float *priors,*logits,*guide,*decoded,*attributes,*prediction,*median,*snapshot_median,*copy;
 uint8_t *rgb,*snapshot_rgb;void *tensor_memory;size_t tensor_bytes;
} cpu_probe_output;
typedef struct {uint64_t begin_tick,end_tick;unsigned task,clock_error;} cpu_probe_sample;
/* Actual production functions. Caller supplies the actual monotonic ARM
 * timer and its frequency; a host timer never establishes an ARM cost.
 * Fixture setup is outside these task samples and must be accounted for
 * separately in the full-chain model. No PL or camera is accessed here. */
int cpu_probe_run(const cpu_probe_input *,cpu_probe_output *,uint64_t (*)(void),cpu_probe_sample [CPU_TASK_COUNT]);
/* Host-only extraction executes the original scalar graph and captures the
 * actual inputs to these CPU functions. It never generates a timing claim. */
typedef struct {
 float *raw_logits,*filtered,*coarse,*attributes,*prediction,*median,*priors,*prepared_logits,*guide;
 int32_t *final_i32;uint8_t *rgb;
} cpu_probe_fixture;
int cpu_probe_extract(void *,void *,size_t,void *,size_t,const r14_graph_frame *,cpu_probe_fixture *);
#endif
