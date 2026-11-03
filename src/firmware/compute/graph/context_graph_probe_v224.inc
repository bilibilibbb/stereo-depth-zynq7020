#ifdef R14_HOST_REFERENCE
/* Test shadow only: exercises production graph dependency/layout changes.
 * Actual DMA arithmetic/transport is checked separately with real RTL.
 * Never linked into the ARM deployment or reported as PL execution. */
#include "context_fusion_kernel_v199.inc"
typedef struct {g_coop_job *job;unsigned context_calls,context_inputs,neural_pending,integer_pending;} context_probe_user_v224;
static int context_probe_nn_begin_v224(void *opaque,int id,const rn_parameter *p,const void *source,int ih,int iw,int16_t *destination,int oh,int ow,int wide,int fraction,int relu){
 context_probe_user_v224 *u=(context_probe_user_v224*)opaque;
 g_coop_job *j=u->job;const rn_node *n=rn_nodes+j->node;const rn_tensor_info *a=rn_tensors+n->input[0];
 unsigned i,count=(unsigned)(a->c*ih*iw);int rc;
 if(u->neural_pending || n->op!=RN_OP_CONV || id!=n->parameter || !source || !destination)return RN_ARGUMENT;
 if(j->node==26){
  if(!j->context_q11_ready || source!=j->g.arena+g_layout[28].offset || count!=148480 || wide || fraction!=11 || relu)return RN_ARGUMENT;
  u->context_inputs++;
 }
 for(i=0;i<count;i++){
  int64_t value=wide?((const int32_t*)source)[i]:((const int16_t*)source)[i];
  if(relu){int64_t six=6*(INT64_C(1)<<fraction);if(value<0)value=0;if(value>six)value=six;}
  j->qin[i]=g_round_even(value,fraction,p->input_frac);
 }
 rc=conv_integer(p,j->qin,ih,iw,destination,oh,ow);if(rc)return rc;
 u->neural_pending=1;return RN_OK;
}
static int context_probe_nn_poll_v224(void *opaque){context_probe_user_v224 *u=(context_probe_user_v224*)opaque;if(!u->neural_pending)return RN_ARGUMENT;u->neural_pending=0;return RN_OK;}
static int context_probe_nn_cancel_v224(void *opaque){context_probe_user_v224 *u=(context_probe_user_v224*)opaque;u->neural_pending=0;return RN_CANCELLED_ERROR;}
static int context_probe_nn_quiet_v224(void *opaque){return !((context_probe_user_v224*)opaque)->neural_pending;}
static int context_probe_integer_begin_v224(void *opaque,const r14_integer_request *r){
 context_probe_user_v224 *u=(context_probe_user_v224*)opaque;g_coop_job *j=u->job;const rn_node *n=rn_nodes+j->node;
 if(u->integer_pending || !r)return RN_ARGUMENT;
 if(r->op==3){
  if(j->node!=24 || r->a!=g_i16(&j->g,15) || r->b!=g_i16(&j->g,27) || r->prior!=g_fp(&j->g,2) ||
     (void*)r->destination!=j->g.arena+g_layout[28].offset)return RN_ARGUMENT;
  if(r14_context_fuse_v199(r->a,r->b,r->prior,(int16_t*)r->destination,148480))return RN_ARGUMENT;
  u->context_calls++;
 }else if(r->op==1 && n->op==RN_OP_ADD)g_add(&j->g,n,r->channels*r->height*r->width);
 else if(r->op==2 && n->op==RN_OP_RESIZE)g_resize2(&j->g,n);
 else return RN_ARGUMENT;
 u->integer_pending=1;return RN_OK;
}
static int context_probe_integer_poll_v224(void *opaque){context_probe_user_v224 *u=(context_probe_user_v224*)opaque;if(!u->integer_pending)return RN_ARGUMENT;u->integer_pending=0;return RN_OK;}
static int context_probe_integer_cancel_v224(void *opaque){context_probe_user_v224 *u=(context_probe_user_v224*)opaque;u->integer_pending=0;return RN_CANCELLED_ERROR;}
static int context_probe_integer_quiet_v224(void *opaque){return !((context_probe_user_v224*)opaque)->integer_pending;}
static const r14_graph_ops context_probe_ops_v224={
 .begin=context_probe_nn_begin_v224,.poll=context_probe_nn_poll_v224,
 .cancel=context_probe_nn_cancel_v224,.quiescent=context_probe_nn_quiet_v224,
 .integer_begin=context_probe_integer_begin_v224,.integer_poll=context_probe_integer_poll_v224,
 .integer_cancel=context_probe_integer_cancel_v224,.integer_quiescent=context_probe_integer_quiet_v224
};
#endif
