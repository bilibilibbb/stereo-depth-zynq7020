#ifndef FIELD_ASYNC_V282_H
#define FIELD_ASYNC_V282_H
#include "r14_field_coop.h"
/* CPU0 owns submission, cancellation and receipt consumption. CPU1 alone
 * executes numeric chunks. The mailbox must be aligned and mapped uncached
 * in both real A9 MMU tables. Data buffers retain their original leases;
 * caller-local cache callbacks perform the existing clean/invalidate duties.
 * Function pointers and rf_program pointers never cross this wire ABI. */
typedef struct {
 uint64_t pointer;
 uint32_t address,bytes,write,reserved;
} field_desc_v282;
typedef struct {
 uint32_t kind,count,native_mode,native_epoch,ticket,reserved[3];
 field_desc_v282 input[40],output[8],native[4];
} field_request_v282;
typedef struct field_cache_v282 {
 void *user;
 void (*clean)(void *,const void *,uint32_t);
 void (*invalidate)(void *,void *,uint32_t);
 void (*barrier)(void *);
} field_cache_v282;
size_t field_mailbox_bytes_v282(void);
size_t field_worker_bytes_v282(void);
int field_mailbox_init_v282(void *,size_t);
int field_worker_init_v282(void *,size_t,void *,const field_cache_v282 *);
int field_submit_v282(void *,const rf_job *,const field_cache_v282 *,uint32_t *);
int field_worker_step_v282(void *);
int field_cancel_v282(void *,uint32_t);
/* A terminal receipt is consumed once. Cancellation is terminal only after
 * CPU1 publishes its receipt, so a cancelled buffer cannot be reused early. */
int field_take_v282(void *,uint32_t,const field_cache_v282 *);
uint32_t field_chunks_v282(const void *);
uint32_t field_completed_v282(const void *);
uint32_t field_mailbox_state_v282(const void *);
#endif
