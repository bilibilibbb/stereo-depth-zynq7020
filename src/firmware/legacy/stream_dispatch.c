#include "compute_namespace.h"
#include "stream_dispatch.h"
#include "field_stream_v346.h"
#include <string.h>
#include <limits.h>

#define STREAM_MAILBOX_MAGIC 0x53534c47u
typedef struct __attribute__((aligned(64))) {
    field_cache_v282 cache;
    ss_lg_stream_mailbox *mailbox;
    uint32_t epoch,steps,valid;
} stream_local;
/* Each core writes only its own instance. Registration precedes publication. */
static stream_local local[2];
_Static_assert(sizeof(ss_lg_stream_mailbox)==128,"fixed stream mailbox ABI");
_Static_assert(offsetof(ss_lg_stream_mailbox,address)==16,"64-bit address field");
_Static_assert(offsetof(ss_lg_stream_mailbox,completed_epoch)==64,"receipt line");
_Static_assert(sizeof(stream_local)%64==0,"core-local cache line ownership");
_Static_assert(__atomic_always_lock_free(4,0),"A9 mailbox atomics");
static uint32_t get(const uint32_t *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void put(uint32_t *p,uint32_t v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static stream_local *self(void){unsigned n=ss_lg_core_id();return n<2&&local[n].valid?local+n:0;}
int ss_lg_stream_mailbox_init(void *memory,size_t bytes){
    ss_lg_stream_mailbox *q=memory;
    if(ss_lg_core_id()!=0||!q||((uintptr_t)q&63u)||bytes<sizeof(*q))return RC_ARGUMENT;
    if(q->magic==STREAM_MAILBOX_MAGIC&&get(&q->state))return RC_AGAIN;
    memset(q,0,sizeof(*q));q->magic=STREAM_MAILBOX_MAGIC;return RC_OK;
}
int ss_lg_stream_bind(unsigned core,ss_lg_stream_mailbox *q,const field_cache_v282 *cache){
    if(core>1||ss_lg_core_id()!=core||!q||((uintptr_t)q&63u)||q->magic!=STREAM_MAILBOX_MAGIC||
       !cache||!cache->clean||!cache->invalidate||!cache->barrier||get(&q->state))return RC_ARGUMENT;
    local[core].cache=*cache;local[core].mailbox=q;local[core].epoch=0;
    local[core].steps=0;local[core].valid=1;return RC_OK;
}
const field_cache_v282 *ss_lg_stream_cache(void){stream_local *s=self();return s?&s->cache:0;}
int ss_lg_stream_available(void){
    stream_local *s=self();return ss_lg_core_id()==0&&s&&s->mailbox->magic==STREAM_MAILBOX_MAGIC&&!get(&s->mailbox->state);
}
int ss_lg_stream_publish(void *address,unsigned kind){
    stream_local *s=self();ss_lg_stream_mailbox *q;
    if(!ss_lg_stream_available())return RC_AGAIN;
    if(!address||((uintptr_t)address&63u)||(kind!=SS_LG_STREAM_COLOR&&kind!=SS_LG_STREAM_GRAY))return RC_ARGUMENT;
    q=s->mailbox;if(q->epoch==UINT32_MAX)return RC_STALE; /* never recycle an epoch */
    q->address=(uint64_t)(uintptr_t)address;q->kind=kind;q->epoch++;
    s->cache.barrier(s->cache.user);put(&q->state,1);return RC_OK;
}
int ss_lg_stream_retire(void *address,unsigned kind){
    stream_local *s=self();ss_lg_stream_mailbox *q;
    if(ss_lg_core_id()!=0||!s)return RC_ARGUMENT;
    q=s->mailbox;
    if(!get(&q->state)||q->address!=(uint64_t)(uintptr_t)address||q->kind!=kind)return RC_STALE;
    if(get(&q->completed_epoch)!=q->epoch)return RC_PENDING;
    if(q->core!=1)return RC_IDENTITY;
    /* The receipt is written after the consumer function returns. CPU0 may
     * now reuse even the descriptor/scratch lines without a late CPU1 access. */
    s->cache.barrier(s->cache.user);put(&q->state,0);return RC_OK;
}
int ss_lg_stream_worker_step(void){
    stream_local *s=self();ss_lg_stream_mailbox *q;void *p;int rc;
    if(ss_lg_core_id()!=1||!s)return RC_ARGUMENT;
    q=s->mailbox;if(!get(&q->state)||get(&q->completed_epoch)==q->epoch)return RC_PENDING;
    if(s->epoch!=q->epoch){s->epoch=q->epoch;s->steps=0;}
    if(!q->address||q->address>UINTPTR_MAX||(q->address&63u))rc=RC_ARGUMENT;
    else{
        p=(void *)(uintptr_t)q->address;
        rc=q->kind==SS_LG_STREAM_COLOR?fs_color_consumer_v346(p):
           q->kind==SS_LG_STREAM_GRAY?fs_gray_consumer_v346(p):RC_ARGUMENT;
    }
    s->steps++;
    if(rc==RC_PENDING||rc==RC_AGAIN)return rc;
    q->status=(uint32_t)rc;q->core=1;q->steps=s->steps;
    s->cache.barrier(s->cache.user);put(&q->completed_epoch,s->epoch);return rc;
}
