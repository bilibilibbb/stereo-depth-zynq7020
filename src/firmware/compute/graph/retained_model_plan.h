#ifndef RETAINED_MODEL_PLAN_H
#define RETAINED_MODEL_PLAN_H
typedef struct {int c,h,w,offset;} rn_tensor_info;
typedef struct {int op,parameter,output,inputs,input[3],relu;} rn_node;
enum {RN_OP_CONV,RN_OP_ADD,RN_OP_RESIZE,RN_OP_POOL,RN_OP_CORRELATION,RN_OP_CONCAT,RN_OP_DECODE};
#define RN_TENSORS 64
#define RN_NODES 60
#define RN_ARENA_FLOATS 1822720
#define RN_MAX_TENSOR 327680
#define RN_COARSE 55
#define RN_FINAL 63
extern const rn_parameter rn_parameters[34];
extern const rn_tensor_info rn_tensors[RN_TENSORS];
extern const rn_node rn_nodes[RN_NODES];
#endif
