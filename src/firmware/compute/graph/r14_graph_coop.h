#ifndef R14_GRAPH_COOP_H
#define R14_GRAPH_COOP_H
#include "r14_typed_nn.h"
#include "retained_post_buffers.h"
typedef struct {
 const float *left,*right,*native,*gray,*right_gray,*curve;
 float *output;void *tensor_memory,*float_memory;size_t tensor_bytes,float_bytes;
 const rn_post_buffers *post;uint64_t *statistics;
 void *filter_memory;size_t filter_bytes;unsigned detach_gray;
} r14_graph_frame;
typedef struct {
 uint32_t op,channels,height,width;
 const void *a,*b;int32_t *destination;
 int a_wide,b_wide,a_fraction,b_fraction,destination_fraction,a_relu,b_relu;
 const void *prior; /* op3: frozen four FP32 prior planes; destination is I16 Q11 */
} r14_integer_request;
typedef int (*r14_integer_backend)(void *,const r14_integer_request *);

typedef struct {
 r14_typed_backend begin;
 int (*poll)(void *);
 int (*cancel)(void *);
 int (*quiescent)(void *);
 r14_integer_backend integer_begin;
 int (*integer_poll)(void *);
 int (*integer_cancel)(void *);
 int (*integer_quiescent)(void *);
 int (*color_begin)(void *,const float *,const float *,float *,void *,size_t);
 int (*color_poll)(void *);
 int (*color_cancel)(void *);
 int (*color_quiescent)(void *);
 int (*gray_begin)(void *,const uint8_t *,const float *,float *,void *,size_t);
 int (*gray_poll)(void *);
 int (*gray_cancel)(void *);
 int (*gray_quiescent)(void *);
} r14_graph_ops;
/* Minimum float workspace when both hardware filter families are used.
 * The separate private filter arena is required and leased independently. */
RN_API size_t r14_graph_coop_float_bytes_with_filters(void);
RN_API size_t r14_graph_coop_bytes(void);
RN_API int r14_graph_coop_init(void *,size_t);
/* Other job may be NULL or an initialized state. Cross-frame memory leases
 * are checked BEFORE initialization or priors write; no tensor can alias any
 * buffer still leased by the other job. All frame buffers remain leased on
 * failure/cancel until explicit drained release. Optional color/gray callback families run the real CP43 FIELD/BOX.
 * Their shared scratch arena is private to this frame and leased by this graph.
 * Other CPU fixed nodes remain synchronous; their time is not simulated. Pending is progress/state, not a measuredFPS. */
RN_API int r14_graph_coop_begin(void *,const r14_graph_frame *,const r14_graph_ops *,void *,const void *);
RN_API int r14_graph_coop_step(void *);
RN_API int r14_graph_coop_cancel(void *);
RN_API int r14_graph_coop_release(void *);
RN_API int r14_graph_coop_owned(const void *);
RN_API int r14_graph_coop_node(const void *);
/* Detached mode defers the filter arena lease until color_begin accepts.
 * The color callback must return RN_PENDING while an external gray queue
 * owns that arena. Only immutable post inputs permit frontend release. */
RN_API int r14_graph_coop_gray_handoff_ready(const void *);
RN_API int r14_graph_coop_detach_gray(void *,uint8_t *,size_t,float *,size_t,float *);
RN_API int r14_graph_coop_release_frontend(void *);
/* A prefix owns only NN/integer buffers until original colour inputs exist.
 * The checkpoint owns immutable inputs and its copied colour planes, not a
 * completed depth. It may be passed as other_storage to begin's lease check.
 * Colour and tail transfer never report a depth completion. */
RN_API int r14_graph_coop_prefix_begin(void *,const r14_graph_frame *,const r14_graph_ops *,void *,const void *);
RN_API int r14_graph_coop_color_checkpoint_ready(const void *);
RN_API size_t r14_graph_color_checkpoint_bytes(void);
RN_API int r14_graph_color_checkpoint_init(void *,size_t);
RN_API int r14_graph_color_checkpoint_export(void *,void *,size_t,void *,size_t,const r14_graph_ops *,void *);
RN_API int r14_graph_color_checkpoint_step(void *);
RN_API int r14_graph_color_checkpoint_tail_begin(void *,void *,const r14_graph_frame *,const r14_graph_ops *,void *,const void *);
RN_API int r14_graph_color_checkpoint_cancel(void *);
RN_API int r14_graph_color_checkpoint_release(void *);
RN_API int r14_graph_color_checkpoint_owned(const void *);
RN_API int r14_graph_coop_validate_frame(const void *,const r14_graph_frame *,const void *const *,unsigned,const void *const *,const size_t *,unsigned);
RN_API int r14_graph_coop_span_leased(const void *,const void *,size_t);
#endif
