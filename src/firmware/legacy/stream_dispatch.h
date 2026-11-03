#ifndef SS_LEGACY_STREAM_DISPATCH_H
#define SS_LEGACY_STREAM_DISPATCH_H
#include "field_async_v282.h"

/* Both cores map this mailbox as normal, shareable, non-cacheable memory.
 * CPU0 alone publishes/retires; CPU1 alone executes and writes receipts.
 * No function pointer or ABI-sized pointer is a shared field. */
typedef struct __attribute__((aligned(64))) {
    uint32_t magic,state,epoch,kind;
    uint64_t address;
    uint8_t reserved[40];
    uint32_t completed_epoch,status,core,steps;
    uint8_t receipt_reserved[48];
} ss_lg_stream_mailbox;
enum {SS_LG_STREAM_COLOR=1,SS_LG_STREAM_GRAY=2};
int ss_lg_stream_mailbox_init(void *,size_t);
int ss_lg_stream_bind(unsigned,ss_lg_stream_mailbox *,const field_cache_v282 *);
/* Supplied by the actual A9 port; a host fixture must explicitly identify
 * which emulated role is running. It is never read from the mailbox. */
unsigned ss_lg_core_id(void);
const field_cache_v282 *ss_lg_stream_cache(void);
int ss_lg_stream_available(void);
int ss_lg_stream_publish(void *,unsigned);
int ss_lg_stream_retire(void *,unsigned);
int ss_lg_stream_worker_step(void);
#endif
