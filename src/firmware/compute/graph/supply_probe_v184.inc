#include "supply_probe_v184.h"
typedef struct {g_context context;const rn_node *node;void *output;float *aux;unsigned index;} supply_work_v184;
_Static_assert(sizeof(supply_case_v184)==48,"fixture case ABI");
_Static_assert(sizeof(supply_blob_v184)==352,"fixture header ABI");
_Static_assert(sizeof(supply_sample_v184)==24,"timer sample ABI");
static unsigned supply_nodes_v184[SUPPLY_CASES_V184]={0,12,24,25,26,51,53};
size_t supply_work_bytes_v184(void){return sizeof(supply_work_v184);}
static int supply_case_valid_v184(const supply_blob_v184 *b,size_t bytes,unsigned index){
 const supply_case_v184 *c;const rn_node *n;unsigned i;
 if(!b || ((uintptr_t)b&63u) || bytes<sizeof(*b) || b->magic!=SUPPLY_MAGIC_V184 || b->version!=1 || b->count!=SUPPLY_CASES_V184 || b->bytes!=bytes || index>=SUPPLY_CASES_V184)return RN_ARGUMENT;
 c=b->cases+index;if(c->node!=supply_nodes_v184[index])return RN_IDENTITY;
 n=rn_nodes+c->node;if(c->kind!=(unsigned)n->op || c->reserved[0] || c->reserved[1])return RN_IDENTITY;
 for(i=0;i<(unsigned)n->inputs;i++){
  const rn_tensor_info *a=rn_tensors+n->input[i];unsigned expected=(unsigned)(a->c*a->h*a->w)*(g_layout[n->input[i]].kind==G_I16?2u:4u);
  if(c->source_bytes[i]!=expected || c->source_offset[i]<384 || (c->source_offset[i]&63u) || c->source_offset[i]>bytes || expected>bytes-c->source_offset[i])return RN_ARGUMENT;
 }
 if(c->out_offset<384 || (c->out_offset&63u) || c->out_offset>bytes || c->out_bytes>bytes-c->out_offset)return RN_ARGUMENT;
 {const rn_tensor_info *a=rn_tensors+n->input[0],*out=rn_tensors+n->output;unsigned expected=n->op==RN_OP_CONV?(unsigned)(a->c*a->h*a->w)*2u:(n->op==RN_OP_DECODE?17u*CN*4u:(unsigned)(out->c*out->h*out->w)*4u);if(c->out_bytes!=expected)return RN_ARGUMENT;}
 return RN_OK;
}
int supply_prepare_v184(const void *blob,size_t bytes,unsigned index,void *storage,size_t storage_bytes,void *arena,size_t arena_bytes,float *aux,void *output,size_t output_bytes){
 const supply_blob_v184 *b=(const supply_blob_v184 *)blob;const supply_case_v184 *c;const rn_node *n;supply_work_v184 next;unsigned i;int rc;
 if(!storage || storage_bytes<sizeof(next) || !arena || ((uintptr_t)arena&63u) || arena_bytes<G_ARENA_BYTES || !aux || !output)return RN_ARGUMENT;
 rc=supply_case_valid_v184(b,bytes,index);if(rc)return rc;c=b->cases+index;n=rn_nodes+c->node;if(output_bytes<c->out_bytes)return RN_ARGUMENT;
 memset(&next,0,sizeof(next));next.context.arena=(uint8_t *)arena;next.node=n;next.output=output;next.aux=aux;next.index=index;
 for(i=0;i<(unsigned)n->inputs;i++){
  int t=n->input[i];const uint8_t *source=(const uint8_t *)b+c->source_offset[i];
  if(t==0)next.context.left=(const float *)source;else if(t==1)next.context.right=(const float *)source;
  else{if(g_layout[t].offset<0 || (size_t)g_layout[t].offset+c->source_bytes[i]>arena_bytes)return RN_ARGUMENT;memcpy(next.context.arena+g_layout[t].offset,source,c->source_bytes[i]);}
 }
 memcpy(storage,&next,sizeof(next));return RN_OK;
}
int supply_execute_v184(void *storage,uint64_t (*tick)(void),supply_sample_v184 *sample){
 supply_work_v184 *w=(supply_work_v184 *)storage;int rc;
 if(!w || !tick || !sample || w->index>=SUPPLY_CASES_V184 || w->node!=rn_nodes+supply_nodes_v184[w->index] || !w->context.arena || !w->output || !w->aux)return RN_ARGUMENT;
 sample->node=supply_nodes_v184[w->index];sample->clock_error=0;sample->begin_tick=tick();rc=g_supply_produce_v184(&w->context,w->node,w->output,w->aux,w->aux+8u*CN);sample->end_tick=tick();sample->clock_error=sample->end_tick<sample->begin_tick;
 return rc?rc:(sample->clock_error?RN_ARGUMENT:RN_OK);
}
int supply_execute_v186(void *storage,uint64_t (*tick)(void),supply_sample_v184 *sample){
 supply_work_v184 *w=(supply_work_v184 *)storage;int rc;
 if(!w || !tick || !sample || w->index>=SUPPLY_CASES_V184 || w->node!=rn_nodes+supply_nodes_v184[w->index] || !w->context.arena || !w->output || !w->aux)return RN_ARGUMENT;
 sample->node=supply_nodes_v184[w->index];sample->clock_error=0;sample->begin_tick=tick();rc=g_supply_produce_v186(&w->context,w->node,w->output,w->aux,w->aux+8u*CN);sample->end_tick=tick();sample->clock_error=sample->end_tick<sample->begin_tick;return rc?rc:(sample->clock_error?RN_ARGUMENT:RN_OK);
}
int supply_quant_directed_v186(void){
 float source[32];int16_t original[32],candidate[32];int fraction,k,lane;
 for(fraction=8;fraction<=15;fraction++)for(k=-32770;k<=32770;k++){
  float value=ldexpf((float)k+.5f,-fraction);
  for(lane=0;lane<32;lane++){float x=lane%3==0?value:(lane%3==1?nextafterf(value,-INFINITY):nextafterf(value,INFINITY));source[lane]=x;original[lane]=quantize_scaled(x,ldexp(1.0,fraction));}
  r14_quantize_bias_v186(source,candidate,32,fraction);if(memcmp(original,candidate,sizeof(original)))return RN_HARDWARE;
 }
 return RN_OK;
}

#ifdef R14_HOST_REFERENCE
static unsigned supply_append_v184(uint8_t *blob,unsigned *offset,size_t capacity,const void *source,unsigned bytes){
 unsigned at=(*offset+63u)&~63u;if(at>capacity || bytes>capacity-at)return 0;memcpy(blob+at,source,bytes);*offset=at+bytes;return at;
}
int supply_extract_v184(void *storage,const void *frame,void *destination,size_t capacity){
 const r14_graph_frame *f=(const r14_graph_frame *)frame;g_coop_job *j=(g_coop_job *)storage;supply_blob_v184 *b=(supply_blob_v184 *)destination;unsigned count=0,offset=384,loops=0;int rc;
 if(!storage || !f || !b || ((uintptr_t)b&63u) || capacity<SUPPLY_CAPACITY_V184 || f->detach_gray || f->filter_memory)return RN_ARGUMENT;
 memset(b,0,sizeof(*b));if(r14_graph_coop_begin(storage,f,0,0,0))return RN_ARGUMENT;
 while(loops++<1000u){
  if(j->phase==G_COOP_NODE && count<SUPPLY_CASES_V184 && j->node==supply_nodes_v184[count]){
   const rn_node *n=rn_nodes+j->node;supply_case_v184 *c=b->cases+count;unsigned i;c->node=j->node;c->kind=(unsigned)n->op;
   for(i=0;i<(unsigned)n->inputs;i++){
    int t=n->input[i];const rn_tensor_info *a=rn_tensors+t;const void *source=t==0?(const void *)j->g.left:(t==1?(const void *)j->g.right:(const void *)(j->g.arena+g_layout[t].offset));
    c->source_bytes[i]=(unsigned)(a->c*a->h*a->w)*(g_layout[t].kind==G_I16?2u:4u);c->source_offset[i]=supply_append_v184((uint8_t *)b,&offset,capacity,source,c->source_bytes[i]);if(!c->source_offset[i])return RN_ARGUMENT;
   }
   {const rn_tensor_info *a=rn_tensors+n->input[0],*out=rn_tensors+n->output;c->out_bytes=n->op==RN_OP_CONV?(unsigned)(a->c*a->h*a->w)*2u:(n->op==RN_OP_DECODE?17u*CN*4u:(unsigned)(out->c*out->h*out->w)*4u);c->out_offset=(offset+63u)&~63u;if(c->out_offset>capacity || c->out_bytes>capacity-c->out_offset)return RN_ARGUMENT;offset=c->out_offset+c->out_bytes;}
   if(g_supply_produce_v184(&j->g,n,(uint8_t *)b+c->out_offset,j->fl,j->fr)){return RN_ARGUMENT;}
   count++;
  }
  rc=r14_graph_coop_step(storage);if(rc==RN_OK)break;if(rc!=RN_PENDING)return rc;
 }
 if(rc!=RN_OK || count!=SUPPLY_CASES_V184)return RN_HARDWARE;
 b->magic=SUPPLY_MAGIC_V184;b->version=1;b->count=count;b->bytes=offset;return RN_OK;
}
#endif
