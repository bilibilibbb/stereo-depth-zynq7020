#ifndef R14_CPU_GRAPH_V182_H
#define R14_CPU_GRAPH_V182_H
#include <stddef.h>
#include <stdint.h>
/* Graph submits only after its full-frame colour/coarse dependency is done.
 * CPU0 owns all region epochs and queues. No partial-row halo consumption. */
typedef struct {
 unsigned task;const float *source[4];float *destination;
} r14_cpu_math_v182;
typedef struct {
 int (*begin)(void *,const r14_cpu_math_v182 *);
 int (*poll)(void *);
 int (*cancel)(void *);
 int (*quiescent)(void *);
} r14_cpu_graph_ops_v182;
int r14_graph_coop_bind_cpu_v182(void *,const r14_cpu_graph_ops_v182 *,void *);
int r14_graph_color_checkpoint_bind_cpu_v182(void *,const r14_cpu_graph_ops_v182 *,void *);
/* CPU0 resumes locally at each bounded range. CPU1 inputs stay leased until
 * a consumed completion, including cancel/failure, before graph release. */
size_t r14_cpu_coordinator_bytes_v182(void);
size_t r14_cpu_binding_bytes_v182(void);
int r14_cpu_coordinator_init_v182(void *,size_t,void *);
int r14_cpu_binding_open_v182(void *,size_t,void *,unsigned,uint64_t,uint64_t,unsigned);
int r14_cpu_binding_close_v182(void *);
int r14_cpu_coordinator_service_v182(void *);
r14_cpu_graph_ops_v182 r14_cpu_graph_callbacks_v182(void);
unsigned r14_cpu_coordinator_submitted_v182(const void *);
unsigned r14_cpu_coordinator_completed_v182(const void *);
unsigned r14_cpu_binding_pending_v182(const void *);
int r14_cpu_binding_completion_v182(const void *,void *,size_t);
int r14_cpu_worker_has_work_v182(const void *);
#endif
