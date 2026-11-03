/* Included in the production driver to share its actual ticket/ACK rules.
 * This is a CPU dispatcher, not an emulated neural compute or data copy. */
enum {NB_PREPARE,NB_PREPARE_WAIT,NB_CORE,NB_CORE_WAIT,NB_EXPORT,NB_EXPORT_WAIT,NB_TERMINAL};
#ifdef R14_HOST_REFERENCE
static uint64_t block_snapshot_stats[3];
void fw_nn_block_plan_stats(uint64_t *out){if(out)memcpy(out,block_snapshot_stats,sizeof(block_snapshot_stats));}
#define NB_SNAPSHOT(bytes,overlap) (block_snapshot_stats[0]++,block_snapshot_stats[1]+=(bytes),block_snapshot_stats[2]+=(overlap))
#else
#define NB_SNAPSHOT(bytes,overlap) ((void)0)
#endif
static void block_plan_snapshot(fw_nn_block_plan *to,const fw_nn_block_plan *from){
 uintptr_t a=(uintptr_t)to,b=(uintptr_t)from;
 unsigned overlap=a<=b?b-a<sizeof(*to):a-b<sizeof(*to);
 if(!FW_NN_COMPACT_PLAN || (to!=from && overlap)){
  /* Preserve memmove semantics even for partially overlapping public plans. */
  memmove(to,from,sizeof(*to));NB_SNAPSHOT(sizeof(*to),to!=from && overlap);return;
 }
 if(to==from){NB_SNAPSHOT(0u,0u);return;}
 memcpy(&to->core,&from->core,sizeof(to->core));
 if(from->prepare_count)memcpy(to->prepare,from->prepare,from->prepare_count*sizeof(*to->prepare));
 if(from->export_count)memcpy(to->export,from->export,from->export_count*sizeof(*to->export));
 to->prepare_count=from->prepare_count;to->export_count=from->export_count;to->dma_only=from->dma_only;
 NB_SNAPSHOT(sizeof(to->core)+3u*sizeof(uint32_t)+(from->prepare_count+from->export_count)*sizeof(*to->prepare),0u);
}
#ifdef R14_HOST_REFERENCE
#include "fw_nn_plan_snapshot_checks.inc"
#endif
static uint32_t block_dma_words(const fw_nn_dma_command *c){return c->words[7]*((c->words[11]&127u)+1u);}
static int block_dma_valid(const fw_nn_dma_command *c,const uint32_t *owner,int output){
 const uint32_t *w=c->words;uint32_t rows=(w[11]&127u)+1u,bytes=w[7]*8u,pixels=w[9]&511u;
 uint64_t end=(uint64_t)w[6]+bytes;
 if(!dma_valid(w) || memcmp(w,owner,20u) || !w[7] || w[7]>4096u || block_dma_words(c)>4096u || (w[6]&7u) || (output?w[5]!=FW_NN_DMA_RAW_EXPORT:w[5]==FW_NN_DMA_RAW_EXPORT))return 0;
 if(w[11]&FW_NN_DMA_PACK_I16){
  if(!pixels || w[8]+pixels>4096u || ((w[9]>>9)&3u)+rows>4u || w[7]!=(pixels+((w[9]>>15)&3u)+3u)/4u)return 0;
 }else if(w[5]<2u){
  if((uint64_t)w[8]+w[7]+(rows>1u?(uint64_t)(rows-1u)*w[9]:0u)>4096u || (rows>1u && w[9]<w[7]))return 0;
 }else if(w[5]==2u){if((w[9]>>2)+w[7]>64u)return 0;}
 else if(w[10]+w[7]>17u)return 0;
 if(rows>1u){if((w[10]&7u) || w[10]<bytes)return 0;end+=(uint64_t)(rows-1u)*w[10];}
 return end<=UINT32_MAX;
}
static int block_plan_valid(const fw_nn_block_plan *p){
 unsigned i;if(!p || p->prepare_count>FW_NN_BLOCK_TRANSFERS || p->export_count>FW_NN_BLOCK_TRANSFERS || p->dma_only>1u)return 0;
 if(p->dma_only){
  if(!owner_valid(p->core.words) || !(p->prepare_count+p->export_count))return 0;
  for(i=5u;i<24u;i++)if(p->core.words[i])return 0;
 }else if(!core_valid(p->core.words))return 0;
 for(i=0;i<p->prepare_count;i++)if(!block_dma_valid(p->prepare+i,p->core.words,0))return 0;
 for(i=0;i<p->export_count;i++)if(!block_dma_valid(p->export+i,p->core.words,1))return 0;
 return 1;
}
static int block_enter(fw_nn_block *b,uint32_t identity){
 uint32_t expected=0;if(!b || !identity)return FW_ARGUMENT;
 if(!__atomic_compare_exchange_n(&b->gate,&expected,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return FW_BUSY;
 if(!b->active || b->identity!=identity || !valid(b->device) || !public_blocked(b->device)){__atomic_store_n(&b->gate,0,__ATOMIC_RELEASE);return FW_STALE;}return FW_OK;
}
static int block_leave(fw_nn_block *b,int rc){__atomic_store_n(&b->gate,0,__ATOMIC_RELEASE);return rc;}
static void block_error(fw_nn_block *b,int rc){if(!b->error && rc!=FW_OK && rc!=FW_PENDING && rc!=FW_BUSY)b->error=(uint32_t)rc;}
static int block_stopped(const fw_nn_block *b){return b->error || b->cancel_requested;}
static void block_receipt(const fw_nn_block *b,fw_nn_block_progress *p){
 *p=(fw_nn_block_progress){b->done,b->error?b->error:b->cancel_requested?FW_CANCELLED:FW_OK,b->cancel_requested,b->prepared_words,b->exported_words,b->phase,b->core_progress};
}
int fw_nn_block_begin(fw_nn_block *b,fw_nn_mmio *m,const fw_nn_block_plan *p,uint32_t *identity){
 uint32_t expected=0;unsigned acquired=0;int rc=FW_BUSY;
 if(!b || !valid(m) || !identity || !block_plan_valid(p))return FW_ARGUMENT;
 if(!__atomic_compare_exchange_n(&b->gate,&expected,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return FW_BUSY;
 if(b->active)return block_leave(b,FW_BUSY);
 expected=0;if(!__atomic_compare_exchange_n(&m->reservation,&expected,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return block_leave(b,FW_BUSY);
 for(acquired=0;acquired<2u;acquired++){expected=FREE;if(!__atomic_compare_exchange_n(m->state+acquired,&expected,RESERVED,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))break;}
 if(acquired!=2u || rd(m,FW_NN_REG_CORE_STATUS) || rd(m,FW_NN_REG_DMA_STATUS) || rd(m,FW_NN_REG_POISON))goto fail;
 if(m->block_ticket==UINT32_MAX){rc=FW_STALE;goto fail;}
 b->device=m;block_plan_snapshot(&b->plan,p);b->identity=++m->block_ticket;b->phase=NB_PREPARE;b->index=0;b->ticket[0]=b->ticket[1]=0;
 b->dma_owned=b->core_owned=b->cancel_requested=b->error=b->done=b->prepared_words=b->exported_words=0;memset(&b->core_progress,0,sizeof(b->core_progress));b->active=1;*identity=b->identity;
 return block_leave(b,FW_OK);
fail:
 while(acquired)__atomic_store_n(m->state+(--acquired),FREE,__ATOMIC_RELEASE);
 __atomic_store_n(&m->reservation,0,__ATOMIC_RELEASE);return block_leave(b,rc);
}
int fw_nn_block_cancel(fw_nn_block *b,uint32_t identity){
 int rc=block_enter(b,identity);if(rc)return rc;
 // A terminal receipt is immutable; cancellation is too late to change it.
 if(!b->done)b->cancel_requested=1u;
 return block_leave(b,FW_OK);
}
static void block_dma_wait(fw_nn_block *b){
 fw_nn_mmio *m=b->device;fw_nn_progress p;const fw_nn_dma_command *c;uint32_t expected_words;unsigned locked=0;int rc;
 if(block_stopped(b)){rc=cancel_any(m,FW_NN_DMA,b->ticket[FW_NN_DMA]);block_error(b,rc);}
 if(FW_NN_DMA_RECEIPT_TRANSACTION){
  rc=lock(m,FW_NN_DMA,b->ticket[FW_NN_DMA]);if(rc){block_error(b,rc);return;}
  locked=1;rc=snapshot(m,FW_NN_DMA,&p);snapshot_history(m,FW_NN_DMA,rc,&p);
 }else rc=poll_any(m,FW_NN_DMA,b->ticket[FW_NN_DMA],&p);
 if(rc){block_error(b,rc);goto done;}
 if(p.poisoned){block_error(b,FW_PACKET);goto done;}
 c=b->phase==NB_PREPARE_WAIT?b->plan.prepare+b->index:b->plan.export+b->index;expected_words=block_dma_words(c);
 if(p.completed_words>expected_words)block_error(b,FW_PACKET);
 if(!p.drained)goto done;
 if(p.cancelled)b->cancel_requested=1u;
 if(p.fault || (!p.cancelled && p.completed_words!=expected_words))block_error(b,FW_PACKET);
 if(FW_NN_DMA_RECEIPT_TRANSACTION){
  /* The reserved block and unit lock still own this fully checked drained
   * receipt. Hardware retains it until ACK; do not reread the same fields.
   * Poison/invalid/transitioning receipts above never reach this write. */
  wr(m,FW_NN_REG_DMA_CONTROL,FW_NN_HW_ACK);order(m);
  __atomic_store_n(m->state+FW_NN_DMA,RESERVED,__ATOMIC_RELEASE);locked=0;
 }else{rc=ack_to(m,FW_NN_DMA,b->ticket[FW_NN_DMA],RESERVED);if(rc){block_error(b,rc);goto done;}}
 b->dma_owned=0;
 if(p.completed_words<=expected_words){if(b->phase==NB_PREPARE_WAIT)b->prepared_words+=p.completed_words;else b->exported_words+=p.completed_words;}
 b->phase--;b->index++;
done:
 if(locked)unlock(m,FW_NN_DMA);
}
int fw_nn_block_step(fw_nn_block *b,uint32_t identity,fw_nn_block_progress *out){
 fw_nn_mmio *m;fw_nn_progress p;const fw_nn_dma_command *c;int rc;
 if(!out)return FW_ARGUMENT;
 rc=block_enter(b,identity);if(rc)return rc;m=b->device;
 switch(b->phase){
 case NB_PREPARE:case NB_EXPORT:
  if(block_stopped(b)){b->phase=NB_TERMINAL;break;}
  if(b->index==(b->phase==NB_PREPARE?b->plan.prepare_count:b->plan.export_count)){
   b->phase=b->phase==NB_PREPARE?NB_CORE:NB_TERMINAL;b->index=0;break;
  }
  c=b->phase==NB_PREPARE?b->plan.prepare+b->index:b->plan.export+b->index;
  rc=start_expected(m,FW_NN_DMA,c->words,b->ticket+FW_NN_DMA,RESERVED);
  if(!rc){b->dma_owned=1;b->phase++;}else block_error(b,rc);
  break;
 case NB_PREPARE_WAIT:case NB_EXPORT_WAIT:
  block_dma_wait(b);break;
 case NB_CORE:
  if(block_stopped(b)){b->phase=NB_TERMINAL;break;}
  if(b->plan.dma_only){b->phase=NB_EXPORT;b->index=0;break;}
  rc=start_expected(m,FW_NN_CORE,b->plan.core.words,b->ticket+FW_NN_CORE,RESERVED);
  if(!rc){b->core_owned=1;b->phase=NB_CORE_WAIT;}else block_error(b,rc);
  break;
 case NB_CORE_WAIT:
  if(block_stopped(b)){rc=cancel_any(m,FW_NN_CORE,b->ticket[FW_NN_CORE]);block_error(b,rc);}
  rc=poll_any(m,FW_NN_CORE,b->ticket[FW_NN_CORE],&p);if(rc){block_error(b,rc);break;}
  b->core_progress=p;if(p.poisoned){block_error(b,FW_PACKET);break;}if(!p.drained)break;
  if(p.cancelled)b->cancel_requested=1u;
  if(p.fault || p.accepted_terms!=p.committed_terms)block_error(b,FW_PACKET);
  b->phase=block_stopped(b)?NB_TERMINAL:NB_EXPORT;b->index=0;break;
 case NB_TERMINAL:break;
 default:block_error(b,FW_PACKET);break;
 }
 if(b->phase==NB_TERMINAL && !b->dma_owned && (!b->core_owned || b->core_progress.drained))b->done=1;
 block_receipt(b,out);return block_leave(b,b->done?FW_OK:FW_PENDING);
}
int fw_nn_block_summary(fw_nn_block *b,uint32_t identity,unsigned depthwise,uint32_t *out,unsigned words){
 int rc=block_enter(b,identity);if(rc)return rc;
 if(!b->done)rc=FW_PENDING;else if(block_stopped(b) || !b->core_owned)rc=FW_PACKET;else rc=summary_any(b->device,b->ticket[FW_NN_CORE],depthwise,out,words);
 return block_leave(b,rc);
}
int fw_nn_block_release(fw_nn_block *b,uint32_t identity){
 fw_nn_mmio *m;int rc=block_enter(b,identity);if(rc)return rc;m=b->device;
 if(!b->done || b->dma_owned)return block_leave(b,FW_PENDING);
 if(b->core_owned){rc=ack_to(m,FW_NN_CORE,b->ticket[FW_NN_CORE],RESERVED);if(rc)return block_leave(b,rc);b->core_owned=0;}
 if(__atomic_load_n(m->state,__ATOMIC_ACQUIRE)!=RESERVED || __atomic_load_n(m->state+1,__ATOMIC_ACQUIRE)!=RESERVED)return block_leave(b,FW_STALE);
 order(m);if(rd(m,FW_NN_REG_CORE_STATUS) || rd(m,FW_NN_REG_DMA_STATUS))return block_leave(b,FW_PENDING);
 __atomic_store_n(m->state,FREE,__ATOMIC_RELEASE);__atomic_store_n(m->state+1,FREE,__ATOMIC_RELEASE);
 b->active=0;__atomic_store_n(&m->reservation,0,__ATOMIC_RELEASE);return block_leave(b,FW_OK);
}
