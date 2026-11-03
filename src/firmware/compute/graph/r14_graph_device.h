#ifndef R14_GRAPH_DEVICE_H
#define R14_GRAPH_DEVICE_H
#include "r14_graph_coop.h"
#include "r14_integer_coop.h"
#include "r14_color_coop.h"
#include "r14_gray_coop.h"
typedef struct {
 uint32_t cookie,frame,filter_frame;r14_nn_coop *neural;r14_integer_coop *integer;
 r14_layer_job layer;r14_integer_job integer_job;
 rcl_coop *color;rg_coop *gray;void *filter_arena;uint32_t filter_bytes;
 rcl_job color_job;rg_job gray_job;
} r14_graph_device;
int r14_graph_device_init(r14_graph_device *,uint32_t,r14_nn_coop *,r14_integer_coop *);
/* Set once before graph begin; the graph must lease this same private arena.
 * Color releases its arena before gray reuses it. Errors/cancel retain leases
 * until explicit recover and physical drain; never a global batch barrier. */
int r14_graph_device_filter_init(r14_graph_device *,rcl_coop *,rg_coop *,void *,uint32_t);
int r14_graph_device_filter_recover(r14_graph_device *);
/* Connect this frame-specific object as graph user. Callers retain graph and
 * frame storage until the graph's explicit release, including after errors. */
const r14_graph_ops *r14_graph_device_callbacks(void);
int r14_graph_device_bind_compute_frame(r14_graph_device *,unsigned,unsigned);
#endif
