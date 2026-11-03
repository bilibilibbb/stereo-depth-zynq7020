/* Compile in one translation unit with the source-bound original graph.
 * This avoids a copied implementation of decode/pool/final/median/RGB. */
#include "r14_graph_coop.c"
#include "cpu_task_probe.h"
static void probe_start(cpu_probe_sample *s,unsigned task,uint64_t (*tick)(void)){
 s->task=task;s->clock_error=0;s->begin_tick=tick();
}
static int probe_end(cpu_probe_sample *s,uint64_t (*tick)(void)){
 s->end_tick=tick();s->clock_error=s->end_tick<s->begin_tick;return s->clock_error?RN_ARGUMENT:RN_OK;
}
int cpu_probe_run(const cpu_probe_input *p,cpu_probe_output *o,uint64_t (*tick)(void),cpu_probe_sample samples[CPU_TASK_COUNT]){
 g_context g;unsigned i;
 if(!p || !o || !tick || !samples || !p->left || !p->right || !p->native || !p->gray || !p->right_gray || !p->curve || !p->raw_logits || !p->filtered || !p->coarse || !p->final_i32 ||
    !o->priors || !o->logits || !o->guide || !o->decoded || !o->attributes || !o->prediction || !o->median || !o->rgb || !o->snapshot_rgb || !o->snapshot_median || !o->copy || !o->tensor_memory ||
    ((uintptr_t)o->tensor_memory&63u) || o->tensor_bytes<r14_graph_tensor_bytes())return RN_ARGUMENT;
 memset(&g,0,sizeof(g));g.arena=(uint8_t *)o->tensor_memory;g.left=p->left;g.right=p->right;g.native=p->native;g.gray=p->gray;g.right_gray=p->right_gray;g.curve=p->curve;
 memcpy(g_fp(&g,RN_COARSE),p->coarse,2u*CN*4u);memcpy(g_i32(&g,RN_FINAL),p->final_i32,2u*N*4u);
 memcpy(o->logits,p->raw_logits,17u*CN*4u);
 probe_start(samples+CPU_PRIORS,CPU_PRIORS,tick);priors(p->native,o->priors);if(probe_end(samples+CPU_PRIORS,tick))return RN_ARGUMENT;
 probe_start(samples+CPU_DECODE_PREPARE,CPU_DECODE_PREPARE,tick);g_decode_prepare(o->logits,p->left,p->curve,o->guide);if(probe_end(samples+CPU_DECODE_PREPARE,tick))return RN_ARGUMENT;
 probe_start(samples+CPU_DECODE_FINISH,CPU_DECODE_FINISH,tick);g_decode_finish(p->filtered,o->decoded);if(probe_end(samples+CPU_DECODE_FINISH,tick))return RN_ARGUMENT;
 probe_start(samples+CPU_ATTRIBUTES_POOL,CPU_ATTRIBUTES_POOL,tick);g_attributes_pool(&g,o->attributes);if(probe_end(samples+CPU_ATTRIBUTES_POOL,tick))return RN_ARGUMENT;
 probe_start(samples+CPU_FINAL_PREDICTION,CPU_FINAL_PREDICTION,tick);g_final_prediction(&g,o->prediction);if(probe_end(samples+CPU_FINAL_PREDICTION,tick))return RN_ARGUMENT;
 probe_start(samples+CPU_MEDIAN,CPU_MEDIAN,tick);median(o->prediction,o->median);if(probe_end(samples+CPU_MEDIAN,tick))return RN_ARGUMENT;
 probe_start(samples+CPU_RGB,CPU_RGB,tick);rgb_native(p->left,o->rgb);if(probe_end(samples+CPU_RGB,tick))return RN_ARGUMENT;
 probe_start(samples+CPU_EXPORT_COPY,CPU_EXPORT_COPY,tick);memcpy(o->copy,o->logits,17u*CN*4u);memcpy(o->copy+17u*CN,o->guide,3u*CN*4u);if(probe_end(samples+CPU_EXPORT_COPY,tick))return RN_ARGUMENT;
 probe_start(samples+CPU_COARSE_COPY,CPU_COARSE_COPY,tick);memcpy(o->copy+20u*CN,p->coarse,2u*CN*4u);if(probe_end(samples+CPU_COARSE_COPY,tick))return RN_ARGUMENT;
 probe_start(samples+CPU_GRAY_SNAPSHOT_COPY,CPU_GRAY_SNAPSHOT_COPY,tick);memcpy(o->snapshot_rgb,o->rgb,3u*PN);memcpy(o->snapshot_median,o->median,PN*4u);if(probe_end(samples+CPU_GRAY_SNAPSHOT_COPY,tick))return RN_ARGUMENT;
 for(i=0;i<CPU_TASK_COUNT;i++)if(samples[i].task!=i || samples[i].clock_error)return RN_ARGUMENT;
 return RN_OK;
}
int cpu_probe_extract(void *prefix,void *capsule,size_t capsule_bytes,void *filter,size_t filter_bytes,const r14_graph_frame *f,cpu_probe_fixture *out){
 g_coop_job *j=(g_coop_job *)prefix;g_color_checkpoint *c=(g_color_checkpoint *)capsule;r14_graph_frame tail;unsigned loops=0,i;int rc;
 if(!prefix || !capsule || !filter || !f || !out || !out->raw_logits || !out->filtered || !out->coarse || !out->attributes || !out->prediction || !out->median || !out->priors || !out->prepared_logits || !out->guide || !out->final_i32 || !out->rgb ||
    f->detach_gray || f->filter_memory || f->float_bytes<r14_graph_float_bytes() || filter_bytes<retained_filter_scratch_bytes())return RN_ARGUMENT;
 if(r14_graph_coop_prefix_begin(prefix,f,NULL,NULL,NULL))return RN_ARGUMENT;
 memcpy(out->priors,g_fp(&j->g,2),4u*CN*4u);
 while(!(j->node==51 && j->phase==G_COOP_NODE)){
  rc=r14_graph_coop_step(prefix);if(rc!=RN_PENDING || ++loops>1000u)return RN_HARDWARE;
 }
 if(rn_nodes[51].op!=RN_OP_DECODE)return RN_ARGUMENT;
 for(i=0;i<17u*CN;i++)out->raw_logits[i]=g_value(&j->g,rn_nodes[51].input[0],(int)i);
 if(r14_graph_coop_step(prefix)!=RN_PENDING || !r14_graph_coop_color_checkpoint_ready(prefix))return RN_HARDWARE;
 memcpy(out->prepared_logits,j->logits,17u*CN*4u);memcpy(out->guide,j->guide,3u*CN*4u);
 if(r14_graph_color_checkpoint_export(prefix,capsule,capsule_bytes,filter,filter_bytes,NULL,NULL))return RN_ARGUMENT;
 if(r14_graph_color_checkpoint_step(capsule)!=RN_OK)return RN_HARDWARE;
 memcpy(out->filtered,c->job.filtered,17u*CN*4u);memcpy(out->coarse,c->planes+37u*CN,2u*CN*4u);
 tail=*f;if(r14_graph_color_checkpoint_tail_begin(capsule,prefix,&tail,NULL,NULL,NULL))return RN_ARGUMENT;
 if(r14_graph_coop_step(prefix)!=RN_PENDING || j->node!=53)return RN_HARDWARE;
 memcpy(out->attributes,g_fp(&j->g,56),8u*128u*160u*4u);
 while(j->phase!=G_COOP_PRED){rc=r14_graph_coop_step(prefix);if(rc!=RN_PENDING || ++loops>2000u)return RN_HARDWARE;}
 memcpy(out->final_i32,g_i32(&j->g,RN_FINAL),2u*N*4u);
 if(r14_graph_coop_step(prefix)!=RN_PENDING || j->phase!=G_COOP_MEDIAN)return RN_HARDWARE;
 memcpy(out->prediction,j->pre,PN*4u);
 if(r14_graph_coop_step(prefix)!=RN_PENDING || j->phase!=G_COOP_GRAY)return RN_HARDWARE;
 memcpy(out->median,j->med,PN*4u);rgb_native(f->left,out->rgb);
 do{rc=r14_graph_coop_step(prefix);if(++loops>3000u)return RN_HARDWARE;}while(rc==RN_PENDING);
 if(rc!=RN_OK)return rc;
 return r14_graph_coop_release(prefix);
}

#ifdef R14_HOST_REFERENCE
#include "context_graph_probe_v224.inc"
int cpu_probe_extract_context_v224(void *prefix,void *capsule,size_t capsule_bytes,void *filter,size_t filter_bytes,const r14_graph_frame *f,cpu_probe_fixture *out){
 g_coop_job *j=(g_coop_job *)prefix;g_color_checkpoint *c=(g_color_checkpoint *)capsule;r14_graph_frame tail;unsigned loops=0,i;int rc;
 if(!prefix || !capsule || !filter || !f || !out || !out->raw_logits || !out->filtered || !out->coarse || !out->attributes || !out->prediction || !out->median || !out->priors || !out->prepared_logits || !out->guide || !out->final_i32 || !out->rgb ||
    f->detach_gray || f->filter_memory || f->float_bytes<r14_graph_float_bytes() || filter_bytes<retained_filter_scratch_bytes())return RN_ARGUMENT;
 context_probe_user_v224 user={j,0,0,0,0};
 if(r14_graph_coop_prefix_begin(prefix,f,&context_probe_ops_v224,&user,NULL))return RN_ARGUMENT;
 memcpy(out->priors,g_fp(&j->g,2),4u*CN*4u);
 while(!(j->node==51 && j->phase==G_COOP_NODE)){
  rc=r14_graph_coop_step(prefix);if(rc!=RN_PENDING || ++loops>1000u)return RN_HARDWARE;
 }
 if(rn_nodes[51].op!=RN_OP_DECODE)return RN_ARGUMENT;
 for(i=0;i<17u*CN;i++)out->raw_logits[i]=g_value(&j->g,rn_nodes[51].input[0],(int)i);
 if(r14_graph_coop_step(prefix)!=RN_PENDING || !r14_graph_coop_color_checkpoint_ready(prefix))return RN_HARDWARE;
 memcpy(out->prepared_logits,j->logits,17u*CN*4u);memcpy(out->guide,j->guide,3u*CN*4u);
 if(r14_graph_color_checkpoint_export(prefix,capsule,capsule_bytes,filter,filter_bytes,NULL,NULL))return RN_ARGUMENT;
 if(r14_graph_color_checkpoint_step(capsule)!=RN_OK)return RN_HARDWARE;
 memcpy(out->filtered,c->job.filtered,17u*CN*4u);memcpy(out->coarse,c->planes+37u*CN,2u*CN*4u);
 tail=*f;if(r14_graph_color_checkpoint_tail_begin(capsule,prefix,&tail,&context_probe_ops_v224,&user,NULL))return RN_ARGUMENT;
 if(r14_graph_coop_step(prefix)!=RN_PENDING || j->node!=53)return RN_HARDWARE;
 memcpy(out->attributes,g_fp(&j->g,56),8u*128u*160u*4u);
 while(j->phase!=G_COOP_PRED){rc=r14_graph_coop_step(prefix);if(rc!=RN_PENDING || ++loops>2000u)return RN_HARDWARE;}
 memcpy(out->final_i32,g_i32(&j->g,RN_FINAL),2u*N*4u);
 if(r14_graph_coop_step(prefix)!=RN_PENDING || j->phase!=G_COOP_MEDIAN)return RN_HARDWARE;
 memcpy(out->prediction,j->pre,PN*4u);
 if(r14_graph_coop_step(prefix)!=RN_PENDING || j->phase!=G_COOP_GRAY)return RN_HARDWARE;
 memcpy(out->median,j->med,PN*4u);rgb_native(f->left,out->rgb);
 do{rc=r14_graph_coop_step(prefix);if(++loops>3000u)return RN_HARDWARE;}while(rc==RN_PENDING);
 if(rc!=RN_OK)return rc;
 if(user.context_calls!=1 || user.context_inputs!=1 || user.neural_pending || user.integer_pending)return RN_HARDWARE;
 return r14_graph_coop_release(prefix);
}

#endif

/* Direct comparison with original functions, also run on actual ARM before
 * any timed task. No approximation or errno/FPSCR control change. */
int cpu_hotspot_directed_tests_v169(void){
 int fraction,i;uint32_t rng=0x169u,a,b;float x,y;
 for(fraction=-130;fraction<=130;fraction++){
  x=ldexpf(1.0f,-fraction);y=r14_inverse_scale_v169(fraction);memcpy(&a,&x,4);memcpy(&b,&y,4);if(a!=b)return 1;
 }
 for(i=-32768;i<=32768;i++){
  x=(float)i*.125f;y=r14_floor_v169(x);x=floorf(x);memcpy(&a,&x,4);memcpy(&b,&y,4);if(a!=b)return 2;
 }
 for(i=0;i<1048576;i++){
  rng=rng*1664525u+1013904223u;memcpy(&x,&rng,4);
  if(x>=-4096 && x<=4096){y=r14_floor_v169(x);x=floorf(x);memcpy(&a,&x,4);memcpy(&b,&y,4);if(a!=b)return 3;}
  x=(float)(rng&0xffffffu)*(255.0f/16777216.0f);if(r14_rgb_round_v169(x)!=(uint8_t)quantize(x,0))return 4;
 }
 for(i=0;i<255;i++){
  x=(float)i+.5f;if(r14_rgb_round_v169(x)!=(uint8_t)quantize(x,0))return 5;
  y=nextafterf(x,-INFINITY);if(r14_rgb_round_v169(y)!=(uint8_t)quantize(y,0))return 6;
  y=nextafterf(x,INFINITY);if(r14_rgb_round_v169(y)!=(uint8_t)quantize(y,0))return 7;
 }
 a=0x80000000u;memcpy(&x,&a,4);y=r14_floor_v169(x);memcpy(&b,&y,4);if(a!=b)return 8;
 return 0;
}

int cpu_vector_directed_tests_v172(void){
 float *left=(float*)malloc(3u*N*4u),*guide0=(float*)malloc(3u*CN*4u),*guide1=(float*)malloc(3u*CN*4u);
 uint8_t *rgb0=(uint8_t*)malloc(3u*PN),*rgb1=(uint8_t*)malloc(3u*PN);
 uint8_t *arena=(uint8_t*)calloc(r14_graph_tensor_bytes(),1);float *native=(float*)malloc(4u*N*4u),*pred0=(float*)malloc(PN*4u),*pred1=(float*)malloc(PN*4u);
 g_context g;uint32_t rng=0x172u,bits;unsigned i;int result=0;
 if(!left || !guide0 || !guide1 || !rgb0 || !rgb1 || !arena || !native || !pred0 || !pred1){result=1;goto finish;}
 for(i=0;i<3u*N;i++){rng=rng*1664525u+1013904223u;left[i]=(float)(rng&0xffffu)*(1.0f/2048.0f)-16;}
 /* Domain boundaries, nonfinite and tiny values exercise scalar fallback. */
 for(i=0;i<64;i++){static const uint32_t special[]={0,0x80000000u,1,0x80000001u,0x7fc00001u,0x7f800000u,0xff800000u,0x42800000u};bits=special[i%8];memcpy(left+i*128,&bits,4);}
 colour_guide_scalar_v172(left,guide0);colour_guide(left,guide1);if(memcmp(guide0,guide1,3u*CN*4u)){result=2;goto finish;}
 rgb_native_scalar_v172(left,rgb0);rgb_native(left,rgb1);if(memcmp(rgb0,rgb1,3u*PN)){result=3;goto finish;}
 memset(&g,0,sizeof(g));g.arena=arena;g.native=native;
 for(i=0;i<4u*N;i++){rng=rng*1664525u+1013904223u;native[i]=(float)(rng&0xffffu)*(1.0f/65536.0f);}
 for(i=0;i<2u*CN;i++){rng=rng*1664525u+1013904223u;g_fp(&g,RN_COARSE)[i]=(float)(rng&0xffffu)*(63.0f/65536.0f);}
 for(i=0;i<2u*N;i++){rng=rng*1664525u+1013904223u;g_i32(&g,RN_FINAL)[i]=(int32_t)(rng%1048561u)-524288;}
 /* Some valid/priors and final values fall outside the fast domain. */
 for(i=0;i<32;i++){bits=i&1u?1u:0x7fc00001u;memcpy(native+16u*W+i*32,&bits,4);g_i32(&g,RN_FINAL)[16u*W+i*32]=600000;}
 if(retained_final_lut_init()){result=4;goto finish;}
 g_final_prediction_scalar_v172(&g,pred0);g_final_prediction(&g,pred1);if(memcmp(pred0,pred1,PN*4u))result=5;
finish:
 retained_final_lut_release();free(left);free(guide0);free(guide1);free(rgb0);free(rgb1);free(arena);free(native);free(pred0);free(pred1);return result;
}

#include "supply_probe_v184.inc"
