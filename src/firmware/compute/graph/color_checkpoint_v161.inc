/* An independent colour continuation releases the NN tensor pool at node51.
 * It retains captured input/output/statistics leases through tail transfer.
 * The actual colour callbacks retain their private DMA arena until quiescent.
 * No old prefix tensor is read by the tail: only the copied coarse planes and
 * the immutable native/gray/right-gray inputs are needed after this boundary.
 */
#define G_CHECKPOINT_MAGIC 0x43433161u
enum {GC_WAITING,GC_COLOR_ACTIVE,GC_TAIL_READY,GC_HANDED,GC_CANCELLED,GC_FAILED,GC_CPU_DECODE};
typedef struct {
 g_coop_job job;uint32_t checkpoint_magic,state,cpu_bind_required;
 float planes[39u*CN]; /* logits17,guide3,filtered17,coarse2 */
} g_color_checkpoint;
RN_API int r14_graph_coop_begin(void *s,const r14_graph_frame *f,const r14_graph_ops *o,void *u,const void *other){return g_coop_begin(s,f,o,u,other,0);}
RN_API int r14_graph_coop_prefix_begin(void *s,const r14_graph_frame *f,const r14_graph_ops *o,void *u,const void *other){return g_coop_begin(s,f,o,u,other,1);}
RN_API int r14_graph_coop_color_checkpoint_ready(const void *s){
 const g_coop_job *j=(const g_coop_job *)s;
 return j && j->magic==G_COOP_MAGIC && j->owned && j->phase==G_COOP_COLOR_READY;
}
RN_API size_t r14_graph_color_checkpoint_bytes(void){return sizeof(g_color_checkpoint);}
RN_API int r14_graph_color_checkpoint_init(void *s,size_t bytes){
 g_color_checkpoint *c=(g_color_checkpoint *)s;
 if(!c || ((uintptr_t)c&63u) || bytes<sizeof(*c))return RN_ARGUMENT;
 if(c->checkpoint_magic==G_CHECKPOINT_MAGIC && c->job.owned)return RN_BUSY_ERROR;
 memset(c,0,sizeof(*c));c->checkpoint_magic=G_CHECKPOINT_MAGIC;c->job.magic=G_COOP_MAGIC;return RN_OK;
}
RN_API int r14_graph_color_checkpoint_export(void *source,void *storage,size_t bytes,void *filter,size_t filter_bytes,const r14_graph_ops *ops,void *user){
 g_coop_job *j=(g_coop_job *)source;g_color_checkpoint *c=(g_color_checkpoint *)storage;unsigned i;
 if(!j || j->magic!=G_COOP_MAGIC || !j->owned || !c || ((uintptr_t)c&63u) || c->checkpoint_magic!=G_CHECKPOINT_MAGIC || bytes<sizeof(*c))return RN_ARGUMENT;
 if(c->job.owned || j->phase!=G_COOP_COLOR_READY || j->lease_bytes[15])return RN_BUSY_ERROR;
 if(!filter || ((uintptr_t)filter&63u) || filter_bytes>UINTPTR_MAX-(uintptr_t)filter ||
    filter_bytes<((ops && ops->color_begin)?G_FILTER_ARENA_BYTES:retained_filter_scratch_bytes()) ||
    sizeof(*c)>UINTPTR_MAX-(uintptr_t)c || span_overlap(c,sizeof(*c),filter,filter_bytes))return RN_ARGUMENT;
 if(ops && ((ops->color_begin!=0)+(ops->color_poll!=0)+(ops->color_cancel!=0)+(ops->color_quiescent!=0))%4)return RN_ARGUMENT;
 for(i=0;i<16;i++)if(j->lease_bytes[i] && (span_overlap(c,sizeof(*c),j->lease[i],j->lease_bytes[i]) || span_overlap(filter,filter_bytes,j->lease[i],j->lease_bytes[i])))return RN_ARGUMENT;
 if((j->ops.quiescent && !j->ops.quiescent(j->user)) || (j->ops.integer_quiescent && !j->ops.integer_quiescent(j->user)) ||
    (j->ops.color_quiescent && !j->ops.color_quiescent(j->user)) || (j->post && *j->post->ownership) || (j->cpu.begin && !j->cpu.quiescent(j->cpu_user)))return RN_BUSY_ERROR;
 c->job=*j;c->job.post=j->post?&c->job.post_value:0;
 c->job.g.arena=0;c->job.qin=0;c->job.fl=0;c->job.fr=0;
 c->job.logits=c->planes;c->job.guide=c->planes+17u*CN;c->job.filtered=c->planes+20u*CN;
 c->job.pre=0;c->job.med=0;c->job.rgb=0;c->job.scratch=filter;c->job.filter_bytes=filter_bytes;
 c->job.filter_memory=filter;c->job.filter_capacity=filter_bytes;c->job.prefix_checkpoint=0;
 memset(&c->job.ops,0,sizeof(c->job.ops));
 if(ops){c->job.ops.color_begin=ops->color_begin;c->job.ops.color_poll=ops->color_poll;c->job.ops.color_cancel=ops->color_cancel;c->job.ops.color_quiescent=ops->color_quiescent;}
 c->job.user=user;c->job.lease[7]=0;c->job.lease_bytes[7]=0;c->job.lease[8]=0;c->job.lease_bytes[8]=0;
 c->job.lease[9]=c;c->job.lease_bytes[9]=sizeof(*c);c->job.lease[15]=filter;c->job.lease_bytes[15]=0;
 memcpy(c->job.logits,j->logits,17u*CN*sizeof(float));memcpy(c->job.guide,j->guide,3u*CN*sizeof(float));
 c->cpu_bind_required=j->cpu.begin!=0;memset(&c->job.cpu,0,sizeof(c->job.cpu));c->job.cpu_user=0;c->job.cpu_started=0;
 c->state=GC_WAITING;c->job.owned=1;c->job.status=RN_PENDING;
 j->owned=0;j->phase=G_COOP_IDLE;memset(j->lease_bytes,0,sizeof(j->lease_bytes));return RN_OK;
}
static int g_checkpoint_valid(const g_color_checkpoint *c){return c && c->checkpoint_magic==G_CHECKPOINT_MAGIC && c->job.magic==G_COOP_MAGIC && c->job.owned;}
RN_API int r14_graph_color_checkpoint_bind_cpu_v182(void *s,const r14_cpu_graph_ops_v182 *ops,void *user){
 g_color_checkpoint *c=(g_color_checkpoint*)s;if(!g_checkpoint_valid(c) || !g_cpu_family_valid_v182(ops) || !user)return RN_ARGUMENT;
 if(c->state!=GC_WAITING || c->job.cpu_started)return RN_BUSY_ERROR;
 c->job.cpu=*ops;c->job.cpu_user=user;c->cpu_bind_required=0;return RN_OK;
}
RN_API int r14_graph_color_checkpoint_step(void *s){
 g_color_checkpoint *c=(g_color_checkpoint *)s;g_coop_job *j;int rc;
 if(!g_checkpoint_valid(c))return RN_ARGUMENT;
 j=&c->job;if(c->cpu_bind_required)return RN_BUSY_ERROR;
 if(c->state==GC_CPU_DECODE){rc=g_cpu_math_step_v182(j,2,j->filtered,0,0,0,c->planes+37u*CN);if(rc==RN_PENDING)return rc;if(rc){c->state=GC_FAILED;j->status=rc;return rc;}c->state=GC_TAIL_READY;return RN_OK;}
 if(c->state==GC_TAIL_READY)return RN_OK;
 if(c->state==GC_CANCELLED || c->state==GC_FAILED)return j->status;
 if(c->state==GC_WAITING){
  if(j->ops.color_begin){
   rc=j->ops.color_begin(j->user,j->logits,j->guide,j->filtered,j->filter_memory,j->filter_capacity);
   if(rc==RN_PENDING)return rc;
   if(rc){c->state=GC_FAILED;j->status=rc;return rc;}
   j->lease_bytes[15]=j->filter_capacity;c->state=GC_COLOR_ACTIVE;return RN_PENDING;
  }
  rc=retained_color_context(j->logits,j->guide,j->filtered,j->scratch,j->filter_bytes);
  if(rc){c->state=GC_FAILED;j->status=rc;return rc;}
 }else if(c->state==GC_COLOR_ACTIVE){
  rc=j->ops.color_poll(j->user);if(rc==RN_PENDING)return rc;
  if(rc){c->state=GC_FAILED;j->status=rc;return rc;}
  if(!j->ops.color_quiescent(j->user))return RN_PENDING;
 }else return RN_ARGUMENT;
 j->lease_bytes[15]=0;/* Filter arena is drained; CPU1 reads private capsule. */
 if(j->cpu.begin){c->state=GC_CPU_DECODE;return RN_PENDING;}
 g_decode_finish(j->filtered,c->planes+37u*CN);c->state=GC_TAIL_READY;return RN_OK;
}
RN_API int r14_graph_color_checkpoint_tail_begin(void *storage,void *tail,const r14_graph_frame *cfg,const r14_graph_ops *ops,void *user,const void *other){
 g_color_checkpoint *c=(g_color_checkpoint *)storage;g_coop_job *j=(g_coop_job *)tail;g_context counters;int rc;unsigned next;
 if(!g_checkpoint_valid(c) || !cfg || !j)return RN_ARGUMENT;
 if(c->state!=GC_TAIL_READY)return RN_BUSY_ERROR;
 if(cfg->left!=c->job.g.left || cfg->right!=c->job.g.right || cfg->native!=c->job.g.native || cfg->gray!=c->job.g.gray ||
    cfg->right_gray!=c->job.g.right_gray || cfg->curve!=c->job.g.curve || cfg->output!=c->job.output || cfg->statistics!=c->job.statistics)return RN_ARGUMENT;
 if(span_overlap(c,sizeof(*c),tail,sizeof(*j)) || span_overlap(c,sizeof(*c),cfg->tensor_memory,cfg->tensor_bytes) ||
    span_overlap(c,sizeof(*c),cfg->float_memory,cfg->float_bytes))return RN_ARGUMENT;
 if(cfg->post && (span_overlap(c,sizeof(*c),cfg->post->source,cfg->post->source_bytes) || span_overlap(c,sizeof(*c),cfg->post->destination,cfg->post->destination_bytes)))return RN_ARGUMENT;
 next=c->job.node+1u;if(next>=RN_NODES || rn_nodes[c->job.node].op!=RN_OP_DECODE || rn_nodes[next].op!=RN_OP_POOL)return RN_ARGUMENT;
 counters=c->job.g;rc=g_coop_begin(tail,cfg,ops,user,other,2);if(rc)return rc;
 j->g.conv_calls=counters.conv_calls;j->g.fp_quant_values=counters.fp_quant_values;j->g.integer_transport_values=counters.integer_transport_values;
 j->g.fixed_integer_values=counters.fixed_integer_values;j->g.attribute_full_frame_bytes_avoided=counters.attribute_full_frame_bytes_avoided;j->g.direct_integer_values=counters.direct_integer_values;
 memcpy(g_fp(&j->g,RN_COARSE),c->planes+37u*CN,2u*CN*sizeof(float));j->node=next;j->phase=G_COOP_NODE;
 j->ops.color_begin=0;j->ops.color_poll=0;j->ops.color_cancel=0;j->ops.color_quiescent=0;
 c->job.owned=0;c->state=GC_HANDED;memset(c->job.lease_bytes,0,sizeof(c->job.lease_bytes));return RN_OK;
}
RN_API int r14_graph_color_checkpoint_cancel(void *s){
 g_color_checkpoint *c=(g_color_checkpoint *)s;int rc=RN_CANCELLED_ERROR;
 if(!g_checkpoint_valid(c) || c->state==GC_CANCELLED || c->state==GC_FAILED)return RN_ARGUMENT;
 if(c->job.cpu_started){rc=c->job.cpu.cancel(c->job.cpu_user);if(rc!=RN_CANCELLED_ERROR){c->state=GC_FAILED;c->job.status=rc?rc:RN_HARDWARE;return c->job.status;}}
 if(c->state==GC_COLOR_ACTIVE){rc=c->job.ops.color_cancel(c->job.user);if(rc!=RN_CANCELLED_ERROR){c->state=GC_FAILED;c->job.status=rc?rc:RN_HARDWARE;return c->job.status;}}
 c->state=GC_CANCELLED;c->job.status=RN_CANCELLED_ERROR;return RN_CANCELLED_ERROR;
}
RN_API int r14_graph_color_checkpoint_release(void *s){
 g_color_checkpoint *c=(g_color_checkpoint *)s;
 if(!g_checkpoint_valid(c))return RN_ARGUMENT;
 if(c->state!=GC_CANCELLED && c->state!=GC_FAILED)return RN_BUSY_ERROR;
 if((c->job.ops.color_quiescent && !c->job.ops.color_quiescent(c->job.user)) || (c->job.post && *c->job.post->ownership) || (c->job.cpu.begin && !c->job.cpu.quiescent(c->job.cpu_user)))return RN_BUSY_ERROR;
 c->job.owned=0;c->job.phase=G_COOP_IDLE;memset(c->job.lease_bytes,0,sizeof(c->job.lease_bytes));return RN_OK;
}
RN_API int r14_graph_color_checkpoint_owned(const void *s){return g_checkpoint_valid((const g_color_checkpoint *)s);}
