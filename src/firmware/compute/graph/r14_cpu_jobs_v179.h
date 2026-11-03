#ifndef R14_CPU_JOBS_V179_H
#define R14_CPU_JOBS_V179_H
#include "cpu_task_probe.h"

enum {R14_CPU_QUEUE_CAPACITY=8,R14_CPU_LEASES=5};
enum {R14_CPU_READY=1,R14_CPU_RUNNING=2,R14_CPU_COMPLETE=3,R14_CPU_CANCELLED=4};
enum {R14_CPU_COLOR_DONE=1,R14_CPU_COARSE_DONE=2};
/* Stable 128-byte ARM/host ABI. Addresses are checked before uintptr casts.
 * Region IDs describe owner-held immutable input/output leases. A lease is
 * never reused until its request has a consumed completion receipt.
 * Only CPU0 may open/close leases, submit, cancel and consume completions.
 * Only CPU1 advances worker_step. Worker code contains no MMIO callbacks.
 */
typedef struct {
 uint64_t frame_id,input_pair_sequence,address[8];
 uint32_t job_id,task,lease_slot,region_epoch,retry_epoch,input_region,output_region;
 uint32_t dependencies,first,last,state,reserved;
} r14_cpu_request_v179;
typedef struct {
 uint64_t frame_id,input_pair_sequence,begin_tick,end_tick;
 uint32_t job_id,task,lease_slot,region_epoch,retry_epoch,status,chunks,core_id;
} r14_cpu_completion_v179;

size_t r14_cpu_kernel_bytes_v179(void);
size_t r14_cpu_queue_bytes_v179(void);
size_t r14_cpu_worker_bytes_v179(void);
unsigned r14_cpu_task_extent_v179(unsigned);
int r14_cpu_request_validate_v179(const r14_cpu_request_v179 *);
int r14_cpu_kernel_begin_v179(void *,size_t,const r14_cpu_request_v179 *);
/* One bounded chunk; RN_PENDING until the entire requested range is done.
 * Full-frame dependency barriers remain necessary for all consumers. */
int r14_cpu_kernel_step_v179(void *);
int r14_cpu_queue_init_v179(void *,size_t);
int r14_cpu_lease_open_v179(void *,unsigned,uint64_t,uint64_t,unsigned,unsigned,unsigned,unsigned *);
int r14_cpu_lease_close_v179(void *,unsigned);
int r14_cpu_submit_v179(void *,const r14_cpu_request_v179 *);
int r14_cpu_cancel_v179(void *,unsigned,unsigned);
int r14_cpu_completion_take_v179(void *,r14_cpu_completion_v179 *);
int r14_cpu_worker_init_v179(void *,size_t,void *,unsigned,uint64_t (*)(void));
int r14_cpu_worker_step_v179(void *);
unsigned r14_cpu_request_peak_v179(const void *);
unsigned r14_cpu_completion_peak_v179(const void *);
int r14_cpu_probe_request_v179(const cpu_probe_input *,cpu_probe_output *,unsigned,r14_cpu_request_v179 *);
int r14_cpu_probe_seed_v179(const cpu_probe_input *,cpu_probe_output *);
int r14_cpu_probe_chunked_v179(const cpu_probe_input *,cpu_probe_output *,uint64_t (*)(void),cpu_probe_sample *,void (*)(void *),void *);
#endif
