#ifndef SNAPSHOT_MEMORY_LAYOUT_H
#define SNAPSHOT_MEMORY_LAYOUT_H
#include <stdint.h>

/* Physical DDR ownership for the selected v351a/v279 image. End addresses
 * are exclusive. Legacy compute addresses are reserved, never camera/SGM aliases. */
#define SS_IMAGE_BEGIN 0x02000000u
#define SS_IMAGE_END   0x04000000u
#define SS_HEAP_BEGIN  0x04000000u
#define SS_HEAP_END    0x04800000u
#define SS_STACK_BEGIN 0x04800000u
#define SS_STACK_END   0x04d00000u
#define SS_GEM_BEGIN   0x07000000u
#define SS_GEM_END     0x07100000u
#define SS_MAILBOX_BEGIN 0x07100000u
#define SS_MAILBOX_END   0x07200000u
#define SS_ARENA_BEGIN 0x10000000u
#define SS_ARENA_END   0x18000000u
#define SS_CAPTURE_BEGIN 0x18000000u
#define SS_CAPTURE_END   0x18400000u
#define SS_LEGACY_TENSOR0 0x19000000u
#define SS_LEGACY_TENSOR1 0x19260000u
#define SS_LEGACY_TENSOR_BYTES 2457600u
#define SS_LEGACY_WEIGHTS 0x194c0000u
#define SS_LEGACY_WEIGHT_BYTES 65536u
#define SS_SGM_BEGIN   0x194e0000u
#define SS_SGM_END     0x19666000u
#define SS_LEGACY_FILTER 0x19950000u
#define SS_LEGACY_FILTER_BYTES 1552384u
#define SS_CALIBRATION_BEGIN 0x1b000000u
#define SS_CALIBRATION_END   0x1b200000u
#define SS_DMA_END     0x1c000000u

enum ss_memory_id {
    SS_MEM_SYSTEM,SS_MEM_NN_DEVICE,SS_MEM_SIX_POOL,SS_MEM_JOB,
    SS_MEM_CONTEXT,SS_MEM_WORKSPACE,SS_MEM_SGM_PROVIDER,
    SS_MEM_INPUT0,SS_MEM_INPUT1,SS_MEM_INPUT2,SS_MEM_INPUT3,
    SS_MEM_INPUT4,SS_MEM_INPUT5,SS_MEM_OUTPUT,
    SS_MEM_RAW_RGB,SS_MEM_RECT_LEFT,SS_MEM_RECT_RIGHT,
    SS_MEM_ROI_LEFT,SS_MEM_ROI_RIGHT,SS_MEM_PREVIEW,SS_MEM_PAIR,
    SS_MEM_RAW_RGB_RIGHT,
    SS_MEM_RESULT0,SS_MEM_RESULT1,SS_MEM_PREVIEW_NEXT,
    SS_MEM_CENSUS_LEFT,SS_MEM_CENSUS_RIGHT,SS_MEM_CURVE,SS_MEM_NATIVE,
    SS_MEM_TENSOR0,SS_MEM_TENSOR1,SS_MEM_WEIGHTS,SS_MEM_FILTER,
    SS_MEM_COUNT
};
typedef struct {uint32_t address,bytes;} ss_memory_span;
typedef struct {
    /* Query the actual compiled backend size functions. No sizeof estimate
     * from the PC ABI is a target allocation. */
    uint32_t system,nn_device,six_pool,job,context,workspace,sgm_provider;
} ss_memory_requirements;
typedef struct {
    ss_memory_span span[SS_MEM_COUNT];
    uint32_t arena_used,sgm_used;
} ss_memory_layout;
/* Pure address planning; no hardware access, malloc or memory clearing.
 * One graph slot suffices for one in-flight shot; both CPU workers remain.
 * Every DMA boundary is 64B aligned, including the allocation's tail. */
int ss_memory_plan(ss_memory_layout *,const ss_memory_requirements *);
#endif
