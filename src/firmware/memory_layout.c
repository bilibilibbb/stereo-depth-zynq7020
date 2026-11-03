#include "memory_layout.h"
#include "camera_capture.h"
#include "latest_pair.h"
#include <string.h>

static int take(ss_memory_span *span,uint32_t *next,uint32_t end,uint32_t n){
    uint32_t padded;
    if(!n || n>UINT32_MAX-63u || *next>end)return SS_ARGUMENT;
    padded=(n+63u)&~63u;
    if(padded>end-*next)return SS_EXHAUSTED;
    *span=(ss_memory_span){*next,n};*next+=padded;return SS_OK;
}
int ss_memory_plan(ss_memory_layout *layout,const ss_memory_requirements *req){
    ss_memory_layout plan;uint32_t at=SS_ARENA_BEGIN,sgm=SS_SGM_BEGIN;unsigned i;int rc;
    uint32_t sizes[SS_MEM_COUNT];
    if(!layout || !req)return SS_ARGUMENT;
    memset(&plan,0,sizeof(plan));
    sizes[0]=req->system;sizes[1]=req->nn_device;sizes[2]=req->six_pool;
    sizes[3]=req->job;sizes[4]=req->context;sizes[5]=req->workspace;sizes[6]=req->sgm_provider;
    sizes[7]=sizes[8]=983040u;sizes[9]=1310720u;
    sizes[10]=sizes[11]=327680u;sizes[12]=696320u;sizes[13]=327680u;
    sizes[14]=SS_RAW_RGB_BYTES;sizes[15]=sizes[16]=SS_PIXELS*3u;
    sizes[17]=sizes[18]=SS_PIXELS;sizes[19]=SS_PREVIEW_MAX_BYTES;
    sizes[SS_MEM_PAIR]=sizes[SS_MEM_RESULT0]=sizes[SS_MEM_RESULT1]=SS_FROZEN_PACKET_BYTES;
    sizes[SS_MEM_RAW_RGB_RIGHT]=SS_RAW_RGB_BYTES;
    sizes[SS_MEM_PREVIEW_NEXT]=SS_PREVIEW_MAX_BYTES;
    sizes[SS_MEM_CENSUS_LEFT]=sizes[SS_MEM_CENSUS_RIGHT]=RS_CENSUS_BYTES;
    sizes[SS_MEM_CURVE]=RS_CURVE_BYTES;sizes[SS_MEM_NATIVE]=RS_NATIVE_BYTES;
    for(i=0;i<=SS_MEM_NATIVE;i++){
        /* The selected opaque legacy state owns its graph and both workers.
         * Former pipeline-only control allocations are deliberately absent. */
        if(i<SS_MEM_INPUT0&&!sizes[i])continue;
        rc=take(plan.span+i,i<SS_MEM_CENSUS_LEFT?&at:&sgm,
                i<SS_MEM_CENSUS_LEFT?SS_ARENA_END:SS_SGM_END,sizes[i]);
        if(rc)return rc;
    }
    /* The legacy driver encodes these addresses; relocating SGM alone keeps
     * its one run disjoint from both NN contexts and packed weights. Context1
     * is reserved for the real driver's two-bank ABI, not a second live frame. */
    plan.span[SS_MEM_TENSOR0]=(ss_memory_span){SS_LEGACY_TENSOR0,SS_LEGACY_TENSOR_BYTES};
    plan.span[SS_MEM_TENSOR1]=(ss_memory_span){SS_LEGACY_TENSOR1,SS_LEGACY_TENSOR_BYTES};
    plan.span[SS_MEM_WEIGHTS]=(ss_memory_span){SS_LEGACY_WEIGHTS,SS_LEGACY_WEIGHT_BYTES};
    plan.span[SS_MEM_FILTER]=(ss_memory_span){SS_LEGACY_FILTER,SS_LEGACY_FILTER_BYTES};
    plan.arena_used=at-SS_ARENA_BEGIN;plan.sgm_used=sgm-SS_SGM_BEGIN;
    *layout=plan;return SS_OK;
}
