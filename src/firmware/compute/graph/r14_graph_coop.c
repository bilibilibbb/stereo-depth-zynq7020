/* Whole accepted graph with persistent integer tensors. The included source
 * supplies the frozen scalar convolution and continuous-boundary algorithms.
 * No learned parameter, channel, activation or disparity candidate changes.
 * ADD and 2x RESIZE use wider exact dyadic numerators, never an early I16 cast.
 */
#include "base_model.c"
#include "r14_graph_layout.h"
#include "r14_typed_nn.h"
#if !defined(_MSC_VER) && !defined(R14_HOST_REFERENCE)
#include "r14_neon_conversion_v1.h"
#endif

enum {G_EXTERNAL, G_I16, G_I32, G_F32};
typedef struct {
    uint8_t *arena;
    const float *left,*right,*native,*gray,*right_gray,*curve;
    uint64_t conv_calls,fp_quant_values,integer_transport_values;
    uint64_t fixed_integer_values,attribute_full_frame_bytes_avoided,direct_integer_values;
} g_context;

static const int16_t *g_i16(const g_context *g,int t){return (const int16_t*)(g->arena+g_layout[t].offset);}
static int32_t *g_i32(const g_context *g,int t){return (int32_t*)(g->arena+g_layout[t].offset);}
static float *g_fp(const g_context *g,int t){return (float*)(g->arena+g_layout[t].offset);}
static int32_t g_number(const g_context *g,int t,int at){
    const g_tensor_layout *l=g_layout+t;
    int32_t v=l->kind==G_I16?(int32_t)g_i16(g,t)[at]:g_i32(g,t)[at];
    if(l->relu){int32_t six=6*(1<<l->fraction);if(v<0)v=0;if(v>six)v=six;}
    return v;
}
static float g_value(const g_context *g,int t,int at){
    if(t==0)return g->left[at];
    if(t==1)return g->right[at];
    if(g_layout[t].kind==G_F32)return g_fp(g,t)[at];
    return (float)g_number(g,t,at)*r14_inverse_scale_v169(g_layout[t].fraction);
}
static int16_t g_round_even(int64_t v,int source_fraction,int target_fraction){
    int shift=source_fraction-target_fraction;
    uint64_t a=(uint64_t)(v<0?-v:v),q=a;
    if(shift>0){
        uint64_t half=(uint64_t)1<<(shift-1),rest=a&(((uint64_t)1<<shift)-1);
        q=a>>shift;if(rest>half || (rest==half && (q&1u)))q++;
    }else if(shift<0)q=a<<(-shift);
    if(q>32767u)q=32767u;
    return (int16_t)(v<0?-(int32_t)q:(int32_t)q);
}
static void g_input_original_v184(g_context *g,int tensor,int16_t *out,int count,int fraction){
    int i;const g_tensor_layout *l=g_layout+tensor;
    if(l->kind==G_EXTERNAL || l->kind==G_F32){
        const float *p=tensor==0?g->left:(tensor==1?g->right:g_fp(g,tensor));
        #if defined(_MSC_VER) || defined(R14_HOST_REFERENCE)
        {double scale=ldexp(1.0,fraction);for(i=0;i<count;i++)out[i]=quantize_scaled(p[i],scale);}
#else
        r14_quantize_neon_v1(p,out,(size_t)count,fraction);
#endif
        g->fp_quant_values+=(uint64_t)count;
    }else{
        for(i=0;i<count;i++)out[i]=g_round_even(g_number(g,tensor,i),l->fraction,fraction);
        g->integer_transport_values+=(uint64_t)count;
    }
}
#include "supply_quant_bias_v186.h"
/*187b measured original quantizer faster; preserve all186 conversion/correlation.*/
static void g_input(g_context *g,int tensor,int16_t *out,int count,int fraction){
 g_input_original_v184(g,tensor,out,count,fraction);
}
static void g_add(g_context *g,const rn_node *n,int count){
    int a=n->input[0],b=n->input[1],f=g_layout[n->output].fraction;
    int sa=f-g_layout[a].fraction,sb=f-g_layout[b].fraction,i;
    int32_t *dst=g_i32(g,n->output),six=6*(1<<f);
    for(i=0;i<count;i++){
        /* Multiplication avoids C's undefined left shift of a negative value. */
        int32_t v=g_number(g,a,i)*(1<<sa)+g_number(g,b,i)*(1<<sb);
        dst[i]=v<0?0:(v>six?six:v);
    }
    g->fixed_integer_values+=(uint64_t)count;
}
static void g_axis2(int at,int input,int *lo,int *hi,int *quarter){
    if(!at){*lo=0;*hi=input>1?1:0;*quarter=0;return;}
    *lo=(at-1)/2;*hi=clip(*lo+1,input);*quarter=(at&1)?1:3;
}
static void g_resize2(g_context *g,const rn_node *n){
    int t=n->input[0],ih=rn_tensors[t].h,iw=rn_tensors[t].w;
    int oh=rn_tensors[n->output].h,ow=rn_tensors[n->output].w;
    int c,y,x,y0,y1,x0,x1,dy,dx;int32_t *dst=g_i32(g,n->output);
    for(c=0;c<rn_tensors[t].c;c++)for(y=0;y<oh;y++){
        g_axis2(y,ih,&y0,&y1,&dy);
        for(x=0;x<ow;x++){
            int32_t a,b;g_axis2(x,iw,&x0,&x1,&dx);
            a=(4-dx)*g_number(g,t,(c*ih+y0)*iw+x0)+dx*g_number(g,t,(c*ih+y0)*iw+x1);
            b=(4-dx)*g_number(g,t,(c*ih+y1)*iw+x0)+dx*g_number(g,t,(c*ih+y1)*iw+x1);
            dst[(c*oh+y)*ow+x]=(4-dy)*a+dy*b;
        }
    }
    g->fixed_integer_values+=(uint64_t)rn_tensors[t].c*oh*ow;
}
/* Produce the original attributes directly into the 2x pooled tensor. Each
 * pixel uses the same scalar operation order; pooling still adds a,b,c,d.
 * The old 8x256x320 float attribute image is never allocated or traversed.
 */
/* Reuse only expressions that are identical for the four original 2x2
 * pixels. Every field and a+b+c+d pool operation keeps its FP32 order.
 * ax/ay are the frozen linear_axis(80,320)/(64,256) maps; both y samples
 * in a pooled cell have the same low/high rows, including clamped borders. */
static void g_attributes_range_v179(const float *coarse,const float *native,const float *gray,const float *right_gray,float *dst,const r14_linear_axis *ax,const r14_linear_axis *ay,int first,int last){
 int cell,c,k,sample;
 for(cell=first;cell<last;cell++){
  int y=cell/160,x=cell%160,yy=2*y,xx=2*x,at0=yy*W+xx;
  int lo[2],hi[2];float coord[2],fraction[2],keep[2];
  float fields[4][8],top[2],bottom[2],g[4];
  float base=coarse[(y/2)*80+x/2],scaled=base/64;
  const r14_linear_axis *a0=ay+yy,*a1=ay+yy+1;
  const float *confidence=coarse+CN;
  /* The private caller always constructs these exact frozen axes. */
  for(k=0;k<2;k++){
   const r14_linear_axis *a=ax+xx+k;
   float floored;coord[k]=(float)(xx+k)-base;floored=r14_floor_v169(coord[k]);
   fraction[k]=coord[k]-floored;keep[k]=1-fraction[k];
   lo[k]=clip((int)floored,W);hi[k]=clip(lo[k]+1,W);
   top[k]=a->keep*confidence[a0->low*80+a->low]+a->fraction*confidence[a0->low*80+a->high];
   bottom[k]=a->keep*confidence[a0->high*80+a->low]+a->fraction*confidence[a0->high*80+a->high];
  }
  g[0]=gray[at0];g[1]=gray[at0+1];g[2]=gray[at0+W];g[3]=gray[at0+W+1];
  for(sample=0;sample<4;sample++){
   int row=sample/2,lane=sample%2,at=at0+row*W+lane;
   const r14_linear_axis *a=row?a1:a0;float *v=fields[sample];
   float warped=right_gray[(yy+row)*W+lo[lane]]*keep[lane]+right_gray[(yy+row)*W+hi[lane]]*fraction[lane];
   v[0]=scaled;v[1]=native[at];v[2]=a->keep*top[lane]+a->fraction*bottom[lane];
   v[3]=native[N+at];v[4]=g[sample];
   v[5]=xx+lane+1<W?gray[at+1]-g[sample]:0;
   v[6]=yy+row+1<H?gray[at+W]-g[sample]:0;
   v[7]=fabsf(g[sample]-warped)*(coord[lane]>=0?1.0f:0.0f);
  }
  for(c=0;c<8;c++){
   float sum=fields[0][c];sum+=fields[1][c];sum+=fields[2][c];sum+=fields[3][c];
   dst[c*20480+cell]=sum*.25f;
  }
 }
}
static void g_attributes_pool(g_context *g,float *dst){
    r14_linear_axis ax[W],ay[H];linear_axis(80,W,ax);linear_axis(64,H,ay);
    g_attributes_range_v179(g_fp(g,RN_COARSE),g->native,g->gray,g->right_gray,dst,ax,ay,0,128*160);
    g->attribute_full_frame_bytes_avoided=8u*N*4u;
}

static void g_final_prediction_scalar_rows_v179(g_context *g,float *pred,int first,int last){
    const float *coarse=g_fp(g,RN_COARSE);int y,x;
    for(y=first;y<last;y++)for(x=0;x<W;x++){
        int at=(y+16)*W+x;float fine0=g_value(g,RN_FINAL,at),fine1=g_value(g,RN_FINAL,N+at);
        float gate=r14_final_gate_v1(fine1),valid=g->native[2*N+at];
        float blend=gate*valid+(1-valid),prior=g->native[at]*64,base=coarse[((y+16)/4)*80+x/4];
        float value=prior*(1-blend)+(base+4*r14_final_tanh_v1(fine0))*blend;
        pred[y*W+x]=clamp(value,0,63.99609375f);
    }
}

static void g_final_prediction_scalar_v172(g_context *g,float *p){g_final_prediction_scalar_rows_v179(g,p,0,240);}
#include "final_vector_v172.inc"

RN_API size_t r14_graph_workspace_bytes(void){
    return G_ARENA_BYTES+align8(RN_MAX_TENSOR*2u)+align8((2u*8u*CN+17u*CN+20u*CN+2u*PN)*4u+3u*PN)+retained_filter_scratch_bytes();
}
RN_API size_t r14_graph_tensor_bytes(void){return G_ARENA_BYTES+align8(RN_MAX_TENSOR*2u);}
RN_API size_t r14_graph_float_bytes(void){return r14_graph_workspace_bytes()-r14_graph_tensor_bytes();}
RN_API int r14_graph_typed_forward_parts(const float *left,const float *right,const float *native,const float *gray,const float *right_gray,const float *curve,
                            float *output,void *tensor_memory,size_t tensor_bytes,void *float_memory,size_t float_bytes,
                            r14_typed_backend backend,void *user,const rn_post_buffers *post,uint64_t *statistics){
    g_context g;int16_t *qin;float *fl,*fr,*logits,*guide,*filtered,*pre,*med;uint8_t *rgb;void *scratch;
    size_t filter_bytes=retained_filter_scratch_bytes();int i,j,rc;
    if(!left||!right||!native||!gray||!right_gray||!curve||!output||!tensor_memory||!float_memory||
       ((uintptr_t)tensor_memory&63u)||((uintptr_t)float_memory&63u)||tensor_bytes<r14_graph_tensor_bytes()||float_bytes<r14_graph_float_bytes()||
       span_overlap(tensor_memory,tensor_bytes,float_memory,float_bytes))return 1;
    {const void *p[7]={left,right,native,gray,right_gray,curve,output};size_t n[7]={3u*N*4u,3u*N*4u,4u*N*4u,N*4u,N*4u,34u*CN*4u,N*4u};
     for(i=0;i<7;i++)if(span_overlap(tensor_memory,tensor_bytes,p[i],n[i]) || span_overlap(float_memory,float_bytes,p[i],n[i]))return 1;}
    if(post){
        const void *p[11]={left,right,native,gray,right_gray,curve,output,tensor_memory,float_memory,post,post->ownership};
        size_t n[11]={3u*N*4u,3u*N*4u,4u*N*4u,N*4u,N*4u,34u*CN*4u,N*4u,tensor_bytes,float_bytes,sizeof(*post),sizeof(*post->ownership)};
        if(!post->source||!post->destination||!post->median||!post->ownership||*post->ownership||
           ((uintptr_t)post->source&31u)||((uintptr_t)post->destination&31u)||post->source_bytes<PN*4u||post->destination_bytes<PN*4u||
           span_overlap(post->source,post->source_bytes,post->destination,post->destination_bytes))return 4;
        for(i=0;i<11;i++)if(span_overlap(post->source,post->source_bytes,p[i],n[i]) || span_overlap(post->destination,post->destination_bytes,p[i],n[i]))return 4;
    }
    memset(&g,0,sizeof(g));g.arena=(uint8_t*)tensor_memory;g.left=left;g.right=right;g.native=native;g.gray=gray;g.right_gray=right_gray;g.curve=curve;
    qin=(int16_t*)(g.arena+G_ARENA_BYTES);fl=(float*)float_memory;fr=fl+8*CN;logits=fr+8*CN;
    guide=logits+17*CN;filtered=guide+3*CN;pre=filtered+17*CN;med=pre+PN;rgb=(uint8_t*)(med+PN);
    scratch=(uint8_t*)fl+align8((2u*8u*CN+17u*CN+20u*CN+2u*PN)*4u+3u*PN);
    if(post){pre=post->source;med=post->destination;}
    priors(native,g_fp(&g,2));
    for(i=0;i<RN_NODES;i++){
        const rn_node *n=rn_nodes+i;const rn_tensor_info *a=rn_tensors+n->input[0],*b=rn_tensors+n->output;
        int count=b->c*b->h*b->w;
        if(n->op==RN_OP_CONV){
            const rn_parameter *p=rn_parameters+n->parameter;int16_t *dst=(int16_t*)(g.arena+g_layout[n->output].offset);
            const g_tensor_layout *source_layout=g_layout+n->input[0];
            if(backend && (source_layout->kind==G_I16 || source_layout->kind==G_I32)){
                const void *source=g.arena+source_layout->offset;
                rc=backend(user,n->parameter,p,source,a->h,a->w,dst,b->h,b->w,
                           source_layout->kind==G_I32,source_layout->fraction,source_layout->relu);
                g.direct_integer_values+=(uint64_t)a->c*a->h*a->w;
            }else{
                g_input(&g,n->input[0],qin,a->c*a->h*a->w,p->input_frac);
                rc=backend?backend(user,n->parameter,p,qin,a->h,a->w,dst,b->h,b->w,0,p->input_frac,0):conv_integer(p,qin,a->h,a->w,dst,b->h,b->w);
            }
            if(rc)return 1000+i;
            g.conv_calls++;
        }else if(n->op==RN_OP_ADD)g_add(&g,n,count);
        else if(n->op==RN_OP_RESIZE)g_resize2(&g,n);
        else if(n->op==RN_OP_CORRELATION){
            for(j=0;j<8*CN;j++){fl[j]=g_value(&g,n->input[0],j);fr[j]=g_value(&g,n->input[1],j);}
            correlation(fl,fr,g_fp(&g,n->output));
        }else if(n->op==RN_OP_CONCAT){
            int k,offset=0;float *dst=g_fp(&g,n->output);
            for(k=0;k<n->inputs;k++){int t=n->input[k],length=rn_tensors[t].c*rn_tensors[t].h*rn_tensors[t].w;
                for(j=0;j<length;j++)dst[offset+j]=g_value(&g,t,j);
                offset+=length;}
        }else if(n->op==RN_OP_DECODE){
            for(j=0;j<17*CN;j++)logits[j]=g_value(&g,n->input[0],j);
            rc=decode(logits,left,curve,g_fp(&g,n->output),guide,filtered,scratch,filter_bytes);if(rc)return rc;
        }else if(n->op==RN_OP_POOL)g_attributes_pool(&g,g_fp(&g,n->output));
        else return 2;
    }
    g_final_prediction(&g,pre);
    if(post)rc=post->median(post->user,pre,med);else{median(pre,med);rc=0;}if(rc)return 6000+rc;
    rgb_native(left,rgb);rc=retained_fast_gray4(rgb,med,output+16*W,scratch,filter_bytes);if(rc)return 30;
    for(i=0;i<16;i++)memcpy(output+i*W,output+16*W,W*sizeof(float));
    if(statistics){statistics[0]=g.conv_calls;statistics[1]=g.fp_quant_values;statistics[2]=g.integer_transport_values;
        statistics[3]=g.fixed_integer_values;statistics[4]=g.attribute_full_frame_bytes_avoided;statistics[5]=G_ARENA_BYTES;statistics[6]=g.direct_integer_values;}
    return 0;
}

/* Exact test hook also supplies independent RTL vectors in the next stage. */
RN_API int r14_graph_round_export(int32_t v,int source_fraction,int target_fraction){return g_round_even(v,source_fraction,target_fraction);}


RN_API int r14_graph_typed_forward(const float *left,const float *right,const float *native,const float *gray,const float *right_gray,const float *curve,
                            float *output,void *workspace,size_t bytes,r14_typed_backend backend,void *user,const rn_post_buffers *post,uint64_t *statistics){
    size_t tensors=r14_graph_tensor_bytes();
    if(!workspace || bytes<r14_graph_workspace_bytes())return 1;
    return r14_graph_typed_forward_parts(left,right,native,gray,right_gray,curve,output,workspace,tensors,(uint8_t*)workspace+tensors,bytes-tensors,backend,user,post,statistics);
}

#include "r14_graph_coop.h"
#include "r14_cpu_graph_v182.h"
#define G_COOP_MAGIC 0x47433434u
#define G_FILTER_ARENA_BYTES 1552384u
/* Copied verbatim from the frozen decode; no libm approximation. */
static void g_decode_center_range_v179(float *logits,const float *curve,int first,int last){
    int at,d;
    for(at=first;at<last;at++){
        float sum=0;int count=0;
        for(d=0;d<17;d++)if(curve[(17+d)*CN+at]>0){sum+=curve[d*CN+at];count++;}
        sum/=(float)(count?count:1);
        for(d=0;d<17;d++){
            float centered=curve[d*CN+at]-sum;centered*=8;centered*=curve[(17+d)*CN+at]>0?1.0f:0.0f;logits[d*CN+at]-=centered;
        }
    }
}
static void g_decode_prepare(float *logits,const float *left,const float *curve,float *guide){
    g_decode_center_range_v179(logits,curve,0,CN);colour_guide(left,guide);
}
static void g_decode_finish_range_v179(const float *filtered,float *dst,int first,int last){
    int at,d;float e[17];
    /* Force the same runtime libm value as the original variable argument;
     * volatile prevents a different compiler-folded log constant. */
    volatile float log_floor_input=1e-12f;
    const float log_floor=logf(log_floor_input);
    for(at=first;at<last;at++){
        int peak=0;float maximum=-1e4f,sum=0,entropy=0,local_sum=0,disparity=0;
        for(d=0;d<17;d++){float v=filtered[d*CN+at];if((d==16?15.75f:(float)d)>(float)(at%80))v=-1e4f;e[d]=v;if(v>maximum){maximum=v;peak=d;}}
        for(d=0;d<17;d++){float delta=e[d]-maximum;e[d]=delta==0.0f?1.0f:expf(delta);sum+=e[d];}
        for(d=0;d<17;d++){float p=e[d]/sum;entropy+=p*(p>1e-12f?logf(p):log_floor);if(d>=peak-1 && d<=peak+1)local_sum+=e[d];}
        for(d=0;d<17;d++)if(d>=peak-1 && d<=peak+1)disparity+=(e[d]/local_sum)*(float)(d==16?63:4*d);
        dst[at]=disparity;dst[CN+at]=-entropy/logf(17.0f);
    }
}

static void g_decode_finish(const float *p,float *d){g_decode_finish_range_v179(p,d,0,CN);}
#if defined(R14_POST_VALIDATION_EXPORTS)
static void g_decode_finish_original_v253(const float *filtered,float *dst,int first,int last){
    int at,d;float e[17];
    for(at=first;at<last;at++){
        int peak=0;float maximum=-1e4f,sum=0,entropy=0,local_sum=0,disparity=0;
        for(d=0;d<17;d++){float v=filtered[d*CN+at];if((d==16?15.75f:(float)d)>(float)(at%80))v=-1e4f;e[d]=v;if(v>maximum){maximum=v;peak=d;}}
        for(d=0;d<17;d++){e[d]=expf(e[d]-maximum);sum+=e[d];}
        for(d=0;d<17;d++){float p=e[d]/sum;entropy+=p*logf(p>1e-12f?p:1e-12f);if(d>=peak-1 && d<=peak+1)local_sum+=e[d];}
        for(d=0;d<17;d++)if(d>=peak-1 && d<=peak+1)disparity+=(e[d]/local_sum)*(float)(d==16?63:4*d);
        dst[at]=disparity;dst[CN+at]=-entropy/logf(17.0f);
    }
}

RN_API int r14_decode_reuse_probe_v253(unsigned version,const float *filtered,float *dst,int first,int last){
 if(!filtered || !dst || first<0 || last>CN || first>last || version>1)return 1;
 if(version)g_decode_finish_range_v179(filtered,dst,first,last);
 else g_decode_finish_original_v253(filtered,dst,first,last);
 return 0;
}

#endif


enum {G_COOP_IDLE=0,G_COOP_NODE=1,G_COOP_WAIT=2,G_COOP_PRED=3,G_COOP_MEDIAN=4,G_COOP_GRAY=5,G_COOP_DONE=6,G_COOP_FAILED=7,G_COOP_CANCELLED=8,G_COOP_INTEGER_WAIT=9,G_COOP_COLOR_WAIT=10,G_COOP_GRAY_WAIT=11,G_COOP_COLOR_READY=12,G_COOP_POST_HANDED=13,G_COOP_CPU_PRIORS=14,G_COOP_CPU_PREPARE=15,G_COOP_CPU_FINISH=16,G_COOP_CPU_ATTRS=17,G_COOP_CPU_RGB=18,G_COOP_CPU_CORRELATION_V205=19,G_COOP_CONTEXT_WAIT_V221=20};
typedef struct {
 uint32_t magic,owned,phase,node;int status;
 g_context g;int16_t *qin;
 float *fl,*fr,*logits,*guide,*filtered,*pre,*med,*output;
 uint8_t *rgb;void *scratch;size_t filter_bytes;
 r14_graph_ops ops;void *user;rn_post_buffers post_value;const rn_post_buffers *post;uint64_t *statistics;
 const void *lease[16];size_t lease_bytes[16];
 unsigned detach_gray,prefix_checkpoint,context_q11_ready;void *filter_memory;size_t filter_capacity;
 r14_cpu_graph_ops_v182 cpu;void *cpu_user;unsigned cpu_cursor,cpu_started,cpu_rgb_ready;
} g_coop_job;
static int g_cpu_family_valid_v182(const r14_cpu_graph_ops_v182 *c){return c && c->begin && c->poll && c->cancel && c->quiescent;}
RN_API int r14_graph_coop_bind_cpu_v182(void *s,const r14_cpu_graph_ops_v182 *c,void *u){
 g_coop_job *j=(g_coop_job*)s;if(!j || j->magic!=G_COOP_MAGIC || !g_cpu_family_valid_v182(c) || !u)return RN_ARGUMENT;
 if(j->owned)return RN_BUSY_ERROR;
 j->cpu=*c;j->cpu_user=u;return RN_OK;
}
static int g_cpu_math_step_v182(g_coop_job *j,unsigned task,const float *a,const float *b,const float *c,const float *d,float *out){
 int rc;if(!j->cpu_started){r14_cpu_math_v182 r={task,{a,b,c,d},out};rc=j->cpu.begin(j->cpu_user,&r);if(rc==RN_PENDING)return rc;if(rc)return rc;j->cpu_started=1;return RN_PENDING;}
 rc=j->cpu.poll(j->cpu_user);if(rc==RN_PENDING)return rc;j->cpu_started=0;return rc;
}
RN_API size_t r14_graph_coop_bytes(void){return sizeof(g_coop_job);}
RN_API int r14_graph_coop_init(void *storage,size_t bytes){
 g_coop_job *job=(g_coop_job*)storage;
 if(!job || ((uintptr_t)job&63u) || bytes<sizeof(*job))return RN_ARGUMENT;
 if(job->magic==G_COOP_MAGIC && job->owned)return RN_BUSY_ERROR;
 memset(job,0,sizeof(*job));job->magic=G_COOP_MAGIC;return RN_OK;
}
static int g_supply_produce_v184(g_context *g,const rn_node *n,void *output,float *fl,float *fr){
 int i,k,offset=0;float *dst=(float *)output;
 if(n->op==RN_OP_CONV){const rn_tensor_info *a=rn_tensors+n->input[0];g_input_original_v184(g,n->input[0],(int16_t *)output,a->c*a->h*a->w,rn_parameters[n->parameter].input_frac);return RN_OK;}
 if(n->op==RN_OP_CORRELATION){for(i=0;i<8*CN;i++){fl[i]=g_value(g,n->input[0],i);fr[i]=g_value(g,n->input[1],i);}correlation(fl,fr,dst);return RN_OK;}
 if(n->op==RN_OP_CONCAT){for(k=0;k<n->inputs;k++){int t=n->input[k],length=rn_tensors[t].c*rn_tensors[t].h*rn_tensors[t].w;for(i=0;i<length;i++)dst[offset+i]=g_value(g,t,i);offset+=length;}return RN_OK;}
 if(n->op==RN_OP_DECODE){for(i=0;i<17*CN;i++)dst[i]=g_value(g,n->input[0],i);return RN_OK;}
 return RN_ARGUMENT;
}

#include "supply_vectors_v186.inc"
static int g_coop_fault(g_coop_job *job,int status){job->phase=G_COOP_FAILED;job->status=status;return status;}
RN_API size_t r14_graph_coop_float_bytes_with_filters(void){
 return align8((2u*8u*CN+17u*CN+20u*CN+2u*PN)*4u+3u*PN);
}
static int g_coop_begin(void *storage,const r14_graph_frame *cfg,const r14_graph_ops *ops,void *user,const void *other_storage,unsigned modes){
 unsigned prefix_checkpoint=modes&1u;
 g_coop_job *job=(g_coop_job*)storage;const g_coop_job *other=(const g_coop_job*)other_storage;
 g_context g;int16_t *qin;float *fl,*fr,*logits,*guide,*filtered,*pre,*med;uint8_t *rgb;void *scratch;
 int i,j;size_t filter_bytes=retained_filter_scratch_bytes();
 const float *left,*right,*native,*gray,*right_gray,*curve;float *output;void *tensor_memory,*float_memory;
 size_t tensor_bytes,float_bytes;const rn_post_buffers *post;
 const void *lease[16];size_t lease_bytes[16];
 if(!job || job->magic!=G_COOP_MAGIC || !cfg || other==job)return RN_ARGUMENT;
 if(job->owned)return RN_BUSY_ERROR;
 if(prefix_checkpoint && (cfg->detach_gray || cfg->filter_memory || (ops && (ops->color_begin || ops->gray_begin))))return RN_ARGUMENT;
 if(cfg->detach_gray>1 || (cfg->detach_gray && (!ops || !ops->color_begin || !ops->gray_begin)))return RN_ARGUMENT;
 if(ops && (!ops->begin || !ops->poll || !ops->cancel || !ops->quiescent))return RN_ARGUMENT;
 if(ops && ((ops->integer_begin!=0)+(ops->integer_poll!=0)+(ops->integer_cancel!=0)+(ops->integer_quiescent!=0))%4)return RN_ARGUMENT;
 if(ops && ((ops->color_begin!=0)+(ops->color_poll!=0)+(ops->color_cancel!=0)+(ops->color_quiescent!=0))%4)return RN_ARGUMENT;
 if(ops && ((ops->gray_begin!=0)+(ops->gray_poll!=0)+(ops->gray_cancel!=0)+(ops->gray_quiescent!=0))%4)return RN_ARGUMENT;
 if(ops && (ops->color_begin || ops->gray_begin) &&
    (!cfg->filter_memory || ((uintptr_t)cfg->filter_memory&63u) || cfg->filter_bytes<G_FILTER_ARENA_BYTES))return RN_ARGUMENT;
 if(other && other->magic!=G_COOP_MAGIC)return RN_ARGUMENT;
 left=cfg->left;right=cfg->right;native=cfg->native;gray=cfg->gray;right_gray=cfg->right_gray;curve=cfg->curve;
 output=cfg->output;tensor_memory=cfg->tensor_memory;tensor_bytes=cfg->tensor_bytes;
 float_memory=cfg->float_memory;float_bytes=cfg->float_bytes;post=cfg->post;
    if(!left||!right||!native||!gray||!right_gray||!curve||!output||!tensor_memory||!float_memory||
       ((uintptr_t)tensor_memory&63u)||((uintptr_t)float_memory&63u)||tensor_bytes<r14_graph_tensor_bytes()||float_bytes<((prefix_checkpoint || (ops && ops->color_begin && ops->gray_begin))?r14_graph_coop_float_bytes_with_filters():r14_graph_float_bytes())||
       span_overlap(tensor_memory,tensor_bytes,float_memory,float_bytes))return 1;
    {const void *p[7]={left,right,native,gray,right_gray,curve,output};size_t n[7]={3u*N*4u,3u*N*4u,4u*N*4u,N*4u,N*4u,34u*CN*4u,N*4u};
     for(i=0;i<7;i++)if(span_overlap(tensor_memory,tensor_bytes,p[i],n[i]) || span_overlap(float_memory,float_bytes,p[i],n[i]))return 1;}
    if(post){
        const void *p[11]={left,right,native,gray,right_gray,curve,output,tensor_memory,float_memory,post,post->ownership};
        size_t n[11]={3u*N*4u,3u*N*4u,4u*N*4u,N*4u,N*4u,34u*CN*4u,N*4u,tensor_bytes,float_bytes,sizeof(*post),sizeof(*post->ownership)};
        if(!post->source||!post->destination||!post->median||!post->ownership||*post->ownership||
           ((uintptr_t)post->source&31u)||((uintptr_t)post->destination&31u)||post->source_bytes<PN*4u||post->destination_bytes<PN*4u||
           span_overlap(post->source,post->source_bytes,post->destination,post->destination_bytes))return 4;
        for(i=0;i<11;i++)if(span_overlap(post->source,post->source_bytes,p[i],n[i]) || span_overlap(post->destination,post->destination_bytes,p[i],n[i]))return 4;
    }

 lease[0]=left;lease_bytes[0]=3u*N*4u;lease[1]=right;lease_bytes[1]=3u*N*4u;
 lease[2]=native;lease_bytes[2]=4u*N*4u;lease[3]=gray;lease_bytes[3]=N*4u;
 lease[4]=right_gray;lease_bytes[4]=N*4u;lease[5]=curve;lease_bytes[5]=34u*CN*4u;
 lease[6]=output;lease_bytes[6]=N*4u;lease[7]=tensor_memory;lease_bytes[7]=tensor_bytes;
 lease[8]=float_memory;lease_bytes[8]=float_bytes;lease[9]=job;lease_bytes[9]=sizeof(*job);
 lease[10]=cfg->statistics;lease_bytes[10]=cfg->statistics?7u*sizeof(uint64_t):0;
 lease[11]=post;lease_bytes[11]=post?sizeof(*post):0;
 lease[12]=post?post->source:0;lease_bytes[12]=post?post->source_bytes:0;
 lease[13]=post?post->destination:0;lease_bytes[13]=post?post->destination_bytes:0;
 lease[14]=post?post->ownership:0;lease_bytes[14]=post?sizeof(*post->ownership):0;
 lease[15]=cfg->filter_memory;lease_bytes[15]=(cfg->filter_memory && !cfg->detach_gray)?cfg->filter_bytes:0;
 for(i=0;i<16;i++)if(lease_bytes[i]){
  if(!lease[i] || lease_bytes[i]>UINTPTR_MAX-(uintptr_t)lease[i])return RN_ARGUMENT;
  for(j=0;j<i;j++)if(lease_bytes[j] && (i>=6 || j>=6) && span_overlap(lease[i],lease_bytes[i],lease[j],lease_bytes[j]))return RN_ARGUMENT;
  if(other && other->owned)for(j=0;j<16;j++)if(other->lease_bytes[j] && span_overlap(lease[i],lease_bytes[i],other->lease[j],other->lease_bytes[j]))return RN_BUSY_ERROR;
 }
    memset(&g,0,sizeof(g));g.arena=(uint8_t*)tensor_memory;g.left=left;g.right=right;g.native=native;g.gray=gray;g.right_gray=right_gray;g.curve=curve;
    qin=(int16_t*)(g.arena+G_ARENA_BYTES);fl=(float*)float_memory;fr=fl+8*CN;logits=fr+8*CN;
    guide=logits+17*CN;filtered=guide+3*CN;pre=filtered+17*CN;med=pre+PN;rgb=(uint8_t*)(med+PN);
    scratch=(uint8_t*)fl+align8((2u*8u*CN+17u*CN+20u*CN+2u*PN)*4u+3u*PN);
    if(post){pre=post->source;med=post->destination;}

 job->g=g;job->qin=qin;job->fl=fl;job->fr=fr;job->logits=logits;job->guide=guide;
 job->filtered=filtered;job->pre=pre;job->med=med;job->output=output;job->rgb=rgb;
 job->scratch=scratch;job->filter_bytes=filter_bytes;
 if(post){job->post_value=*post;job->post=&job->post_value;}else job->post=0;
 job->statistics=cfg->statistics;
 if(ops)job->ops=*ops;else memset(&job->ops,0,sizeof(job->ops));job->user=user;
 for(i=0;i<16;i++){job->lease[i]=lease[i];job->lease_bytes[i]=lease_bytes[i];}
 job->prefix_checkpoint=prefix_checkpoint;job->detach_gray=cfg->detach_gray;job->filter_memory=cfg->filter_memory;job->filter_capacity=cfg->filter_bytes;
 if(job->detach_gray)job->lease_bytes[15]=0;
 job->owned=1;job->node=0;job->phase=G_COOP_NODE;job->status=RN_PENDING;
 job->cpu_cursor=job->cpu_started=job->cpu_rgb_ready=0;job->context_q11_ready=0;
 if(!(modes&2u)){if(job->cpu.begin)job->phase=G_COOP_CPU_PRIORS;else priors(native,g_fp(&job->g,2));}
 return RN_OK;
}
static int g_submit_integer(g_coop_job *job,const rn_node *n){
 const rn_tensor_info *a=rn_tensors+n->input[0],*o=rn_tensors+n->output;
 const g_tensor_layout *la=g_layout+n->input[0],*lo=g_layout+n->output;
 r14_integer_request r;memset(&r,0,sizeof(r));
 if((la->kind!=G_I16 && la->kind!=G_I32) || lo->kind!=G_I32 || a->c!=o->c)return RN_ARGUMENT;
 r.op=n->op==RN_OP_ADD?1u:2u;r.channels=(uint32_t)a->c;r.height=(uint32_t)a->h;r.width=(uint32_t)a->w;
 r.a=job->g.arena+la->offset;r.destination=(int32_t*)(job->g.arena+lo->offset);
 r.a_wide=la->kind==G_I32;r.a_fraction=la->fraction;r.a_relu=la->relu;r.destination_fraction=lo->fraction;
 if(r.op==1){
  const rn_tensor_info *b=rn_tensors+n->input[1];const g_tensor_layout *lb=g_layout+n->input[1];
  if((lb->kind!=G_I16 && lb->kind!=G_I32) || a->c!=b->c || a->h!=b->h || a->w!=b->w || a->h!=o->h || a->w!=o->w)return RN_ARGUMENT;
  r.b=job->g.arena+lb->offset;r.b_wide=lb->kind==G_I32;r.b_fraction=lb->fraction;r.b_relu=lb->relu;
 }else if(o->h!=2*a->h || o->w!=2*a->w)return RN_ARGUMENT;
 return job->ops.integer_begin(job->user,&r);
}
RN_API int r14_graph_coop_step(void *storage){
 g_coop_job *job=(g_coop_job*)storage;int j,rc;
 if(!job || job->magic!=G_COOP_MAGIC || !job->owned)return RN_ARGUMENT;
 if(job->phase==G_COOP_CPU_CORRELATION_V205){
  unsigned end=job->cpu_cursor+8u;const rn_node *n=rn_nodes+job->node;
  if(n->op!=RN_OP_CORRELATION || end>17u*64u)return g_coop_fault(job,RN_ARGUMENT);
  g_supply_correlation_rows_v205(job->fl,job->fr,g_fp(&job->g,n->output),job->cpu_cursor,end);
  job->cpu_cursor=end;
  if(end==17u*64u){job->cpu_cursor=0;job->node++;job->phase=G_COOP_NODE;}
  return RN_PENDING;
 }
 if(job->phase==G_COOP_CPU_PRIORS){unsigned last=job->cpu_cursor+64;if(last>CN)last=CN;priors_range_v179(job->g.native,g_fp(&job->g,2),(int)job->cpu_cursor,(int)last);job->cpu_cursor=last;if(last==CN){job->cpu_cursor=0;job->phase=G_COOP_NODE;}return RN_PENDING;}
 if(job->phase==G_COOP_CPU_PREPARE){unsigned last;
  if(job->cpu_cursor<CN){last=job->cpu_cursor+64;if(last>CN)last=CN;g_decode_center_range_v179(job->logits,job->g.curve,(int)job->cpu_cursor,(int)last);}
  else{last=job->cpu_cursor+1;colour_guide_rows_v179(job->g.left,job->guide,(int)(job->cpu_cursor-CN),(int)(last-CN));}
  job->cpu_cursor=last;if(last==CN+64){job->cpu_cursor=0;job->phase=G_COOP_COLOR_READY;}return RN_PENDING;
 }
 if(job->phase==G_COOP_CPU_FINISH){rc=g_cpu_math_step_v182(job,2,job->filtered,0,0,0,g_fp(&job->g,rn_nodes[job->node].output));if(rc==RN_PENDING)return rc;if(rc)return g_coop_fault(job,rc);job->node++;job->phase=G_COOP_NODE;return RN_PENDING;}
 if(job->phase==G_COOP_CPU_ATTRS){rc=g_cpu_math_step_v182(job,3,g_fp(&job->g,RN_COARSE),job->g.native,job->g.gray,job->g.right_gray,g_fp(&job->g,rn_nodes[job->node].output));if(rc==RN_PENDING)return rc;if(rc)return g_coop_fault(job,rc);job->g.attribute_full_frame_bytes_avoided=8u*N*4u;job->node++;job->phase=G_COOP_NODE;return RN_PENDING;}
 if(job->phase==G_COOP_CPU_RGB){rgb_native_rows_v179(job->g.left,job->rgb,(int)job->cpu_cursor,(int)job->cpu_cursor+1);if(++job->cpu_cursor==240){job->cpu_cursor=0;job->cpu_rgb_ready=1;job->phase=G_COOP_GRAY;}return RN_PENDING;}
 if(job->phase==G_COOP_DONE)return RN_OK;
 if(job->phase==G_COOP_FAILED || job->phase==G_COOP_CANCELLED)return job->status;
 if(job->phase==G_COOP_WAIT){
  rc=job->ops.poll(job->user);if(rc==RN_PENDING)return rc;
  if(rc)return g_coop_fault(job,1000+(int)job->node);
  job->g.conv_calls++;job->node++;job->phase=G_COOP_NODE;return RN_PENDING;
 }
 if(job->phase==G_COOP_CONTEXT_WAIT_V221){
  rc=job->ops.integer_poll(job->user);if(rc==RN_PENDING)return rc;
  if(rc)return g_coop_fault(job,2224);
  job->context_q11_ready=1;job->node+=2;job->phase=G_COOP_NODE;return RN_PENDING;
 }
 if(job->phase==G_COOP_INTEGER_WAIT){
  const rn_tensor_info *out;rc=job->ops.integer_poll(job->user);if(rc==RN_PENDING)return rc;
  if(rc)return g_coop_fault(job,2000+(int)job->node);
  out=rn_tensors+rn_nodes[job->node].output;job->g.fixed_integer_values+=(uint64_t)out->c*out->h*out->w;
  job->node++;job->phase=G_COOP_NODE;return RN_PENDING;
 }
 if(job->phase==G_COOP_POST_HANDED)return RN_PENDING;
 if(job->phase==G_COOP_COLOR_READY){
  if(job->prefix_checkpoint)return RN_PENDING;
  rc=job->ops.color_begin(job->user,job->logits,job->guide,job->filtered,job->filter_memory,job->filter_capacity);
  if(rc==RN_PENDING)return rc;
  if(rc)return g_coop_fault(job,20);
  job->lease_bytes[15]=job->filter_capacity;job->phase=G_COOP_COLOR_WAIT;return RN_PENDING;
 }
 if(job->phase==G_COOP_COLOR_WAIT){
  rc=job->ops.color_poll(job->user);if(rc==RN_PENDING)return rc;
  if(rc)return g_coop_fault(job,20);
  if(job->detach_gray)job->lease_bytes[15]=0;
  if(job->cpu.begin){job->phase=G_COOP_CPU_FINISH;return RN_PENDING;}
  g_decode_finish(job->filtered,g_fp(&job->g,rn_nodes[job->node].output));
  job->node++;job->phase=G_COOP_NODE;return RN_PENDING;
 }
 if(job->phase==G_COOP_GRAY_WAIT){
  rc=job->ops.gray_poll(job->user);if(rc==RN_PENDING)return rc;
  if(rc)return g_coop_fault(job,30);
  job->phase=G_COOP_GRAY;/* Complete through the common output footer below. */
  goto gray_complete;
 }
 if(job->phase==G_COOP_NODE){
  const rn_node *n;const rn_tensor_info *a,*b;int count;
  g_context g=job->g;int16_t *qin=job->qin;float *fl=job->fl,*fr=job->fr,*logits=job->logits;
  const float *left=g.left,*curve=g.curve;float *guide=job->guide,*filtered=job->filtered;
  void *scratch=job->scratch;size_t filter_bytes=job->filter_bytes;
  if(job->node==RN_NODES){job->phase=G_COOP_PRED;return RN_PENDING;}
  n=rn_nodes+job->node;a=rn_tensors+n->input[0];b=rn_tensors+n->output;count=b->c*b->h*b->w;
  if(n->op==RN_OP_CONV){
   const rn_parameter *p=rn_parameters+n->parameter;int16_t *dst=(int16_t*)(g.arena+g_layout[n->output].offset);
   const g_tensor_layout *source_layout=g_layout+n->input[0];const void *source;
   int wide=0,fraction=p->input_frac,relu=0;
   if(job->node==26 && job->context_q11_ready){
    source=g.arena+g_layout[28].offset;fraction=11;wide=0;relu=0;
    if(!job->ops.begin || a->c!=29 || a->h!=64 || a->w!=80 || p->input_frac!=11)return g_coop_fault(job,RN_ARGUMENT);
    g.direct_integer_values+=(uint64_t)a->c*a->h*a->w;
   }else if(job->ops.begin && (source_layout->kind==G_I16 || source_layout->kind==G_I32)){
    source=g.arena+source_layout->offset;wide=source_layout->kind==G_I32;
    fraction=source_layout->fraction;relu=source_layout->relu;g.direct_integer_values+=(uint64_t)a->c*a->h*a->w;
   }else{g_input(&g,n->input[0],qin,a->c*a->h*a->w,p->input_frac);source=qin;}
   if(job->ops.begin){
    rc=job->ops.begin(job->user,n->parameter,p,source,a->h,a->w,dst,b->h,b->w,wide,fraction,relu);
    job->g=g;if(rc)return g_coop_fault(job,1000+(int)job->node);
    job->phase=G_COOP_WAIT;return RN_PENDING;
   }
   rc=conv_integer(p,qin,a->h,a->w,dst,b->h,b->w);
   if(rc)return g_coop_fault(job,1000+(int)job->node);
   g.conv_calls++;
  }else if((n->op==RN_OP_ADD || n->op==RN_OP_RESIZE) && job->ops.integer_begin){
   rc=g_submit_integer(job,n);if(rc)return g_coop_fault(job,2000+(int)job->node);
   job->phase=G_COOP_INTEGER_WAIT;return RN_PENDING;
  }else if(n->op==RN_OP_ADD)g_add(&g,n,count);
        else if(n->op==RN_OP_RESIZE)g_resize2(&g,n);
        else if(n->op==RN_OP_CORRELATION && job->ops.integer_begin && job->ops.begin){
            r14_integer_request request;memset(&request,0,sizeof(request));
            if(job->node!=24 || g_layout[n->input[0]].kind!=G_I16 || g_layout[n->input[1]].kind!=G_I16 ||
               g_layout[n->input[0]].fraction!=11 || g_layout[n->input[1]].fraction!=11)return g_coop_fault(job,RN_ARGUMENT);
            request.op=3;request.channels=29;request.height=64;request.width=80;
            request.a=g_i16(&g,n->input[0]);request.b=g_i16(&g,n->input[1]);request.prior=g_fp(&g,2);
            request.destination=(int32_t*)(g.arena+g_layout[28].offset);
            request.a_fraction=request.b_fraction=request.destination_fraction=11;
            rc=job->ops.integer_begin(job->user,&request);if(rc==RN_PENDING)return rc;
            if(rc)return g_coop_fault(job,2224);
            job->g=g;job->phase=G_COOP_CONTEXT_WAIT_V221;return RN_PENDING;
        }else if(n->op==RN_OP_CORRELATION){
            if(job->cpu.begin){
             /* Immutable feature leases survive all 136 owner dispatches.
              * No NN consumer may see the partially filled correlation. */
             g_supply_values_v186(&g,n->input[0],fl,8u*CN);g_supply_values_v186(&g,n->input[1],fr,8u*CN);
             job->g=g;job->cpu_cursor=0;job->phase=G_COOP_CPU_CORRELATION_V205;return RN_PENDING;
            }
            if(g_supply_produce_v186(&g,n,g_fp(&g,n->output),fl,fr))return g_coop_fault(job,RN_ARGUMENT);
        }else if(n->op==RN_OP_CONCAT){
            if(g_supply_produce_v186(&g,n,g_fp(&g,n->output),fl,fr))return g_coop_fault(job,RN_ARGUMENT);
        }else if(n->op==RN_OP_DECODE){
            if(g_supply_produce_v186(&g,n,logits,fl,fr))return g_coop_fault(job,RN_ARGUMENT);
            if(job->prefix_checkpoint || job->ops.color_begin){
             if(job->cpu.begin){job->g=g;job->cpu_cursor=0;job->phase=G_COOP_CPU_PREPARE;return RN_PENDING;}
             g_decode_prepare(logits,left,curve,guide);
             if(job->prefix_checkpoint){job->g=g;job->phase=G_COOP_COLOR_READY;return RN_PENDING;}
             if(job->detach_gray){job->g=g;job->phase=G_COOP_COLOR_READY;return RN_PENDING;}
             rc=job->ops.color_begin(job->user,logits,guide,filtered,(void *)job->lease[15],job->lease_bytes[15]);
             if(rc)return g_coop_fault(job,20);
             job->g=g;job->phase=G_COOP_COLOR_WAIT;return RN_PENDING;
            }
            rc=decode(logits,left,curve,g_fp(&g,n->output),guide,filtered,scratch,filter_bytes);if(rc)return g_coop_fault(job,rc);
        }else if(n->op==RN_OP_POOL){if(job->cpu.begin){job->g=g;job->phase=G_COOP_CPU_ATTRS;return RN_PENDING;}g_attributes_pool(&g,g_fp(&g,n->output));}
        else return g_coop_fault(job,2);
  job->g=g;job->node++;return RN_PENDING;
 }
 if(job->phase==G_COOP_PRED){if(job->cpu.begin){g_final_prediction_rows_v179(&job->g,job->pre,(int)job->cpu_cursor,(int)job->cpu_cursor+1);if(++job->cpu_cursor<240)return RN_PENDING;job->cpu_cursor=0;}else g_final_prediction(&job->g,job->pre);job->phase=G_COOP_MEDIAN;return RN_PENDING;}
 if(job->phase==G_COOP_MEDIAN){
  if(job->post)rc=job->post->median(job->post->user,job->pre,job->med);else if(job->cpu.begin){r14_median_rows_v179(job->pre,job->med,median_scalar_pixel_v2,(int)job->cpu_cursor,(int)job->cpu_cursor+1);if(++job->cpu_cursor<240)return RN_PENDING;job->cpu_cursor=0;rc=0;}else{median(job->pre,job->med);rc=0;}
  if(rc)return g_coop_fault(job,6000+rc);
  job->phase=job->cpu.begin?G_COOP_CPU_RGB:G_COOP_GRAY;return RN_PENDING;
 }
 if(job->phase==G_COOP_GRAY){
  if(!job->cpu_rgb_ready)rgb_native(job->g.left,job->rgb);
  if(job->ops.gray_begin){
   rc=job->ops.gray_begin(job->user,job->rgb,job->med,job->output+16*W,(void *)job->lease[15],job->lease_bytes[15]);
   if(rc)return g_coop_fault(job,30);
   job->phase=G_COOP_GRAY_WAIT;return RN_PENDING;
  }
  rc=retained_fast_gray4(job->rgb,job->med,job->output+16*W,job->scratch,job->filter_bytes);
  if(rc)return g_coop_fault(job,30);
gray_complete:
  for(j=0;j<16;j++)memcpy(job->output+j*W,job->output+16*W,W*sizeof(float));
  if(job->statistics){
   job->statistics[0]=job->g.conv_calls;job->statistics[1]=job->g.fp_quant_values;job->statistics[2]=job->g.integer_transport_values;
   job->statistics[3]=job->g.fixed_integer_values;job->statistics[4]=job->g.attribute_full_frame_bytes_avoided;
   job->statistics[5]=G_ARENA_BYTES;job->statistics[6]=job->g.direct_integer_values;
  }
  job->phase=G_COOP_DONE;job->status=RN_OK;return RN_OK;
 }
 return g_coop_fault(job,RN_HARDWARE);
}
RN_API int r14_graph_coop_cancel(void *storage){
 g_coop_job *job=(g_coop_job*)storage;int rc=RN_CANCELLED_ERROR;
 if(!job || job->magic!=G_COOP_MAGIC || !job->owned || job->phase==G_COOP_DONE || job->phase==G_COOP_FAILED || job->phase==G_COOP_CANCELLED)return RN_ARGUMENT;
 if(job->cpu_started){rc=job->cpu.cancel(job->cpu_user);if(rc!=RN_CANCELLED_ERROR)return g_coop_fault(job,rc?rc:RN_HARDWARE);}
 if(job->phase==G_COOP_WAIT){rc=job->ops.cancel(job->user);if(rc!=RN_CANCELLED_ERROR)return g_coop_fault(job,rc?rc:RN_HARDWARE);}
 if(job->phase==G_COOP_INTEGER_WAIT || job->phase==G_COOP_CONTEXT_WAIT_V221){rc=job->ops.integer_cancel(job->user);if(rc!=RN_CANCELLED_ERROR)return g_coop_fault(job,rc?rc:RN_HARDWARE);}
 if(job->phase==G_COOP_COLOR_WAIT){rc=job->ops.color_cancel(job->user);if(rc!=RN_CANCELLED_ERROR)return g_coop_fault(job,rc?rc:RN_HARDWARE);}
 if(job->phase==G_COOP_GRAY_WAIT){rc=job->ops.gray_cancel(job->user);if(rc!=RN_CANCELLED_ERROR)return g_coop_fault(job,rc?rc:RN_HARDWARE);}
 job->phase=G_COOP_CANCELLED;job->status=RN_CANCELLED_ERROR;return RN_CANCELLED_ERROR;
}
RN_API int r14_graph_coop_release(void *storage){
 g_coop_job *job=(g_coop_job*)storage;
 if(!job || job->magic!=G_COOP_MAGIC || !job->owned)return RN_ARGUMENT;
 if(job->phase!=G_COOP_DONE && job->phase!=G_COOP_FAILED && job->phase!=G_COOP_CANCELLED)return RN_BUSY_ERROR;
 if((job->ops.quiescent && !job->ops.quiescent(job->user)) || (job->ops.integer_quiescent && !job->ops.integer_quiescent(job->user)) || (job->ops.color_quiescent && !job->ops.color_quiescent(job->user)) ||
    (job->ops.gray_quiescent && !job->ops.gray_quiescent(job->user)) || (job->post && *job->post->ownership) || (job->cpu.begin && !job->cpu.quiescent(job->cpu_user)))return RN_BUSY_ERROR;
 job->owned=0;job->phase=G_COOP_IDLE;memset(job->lease_bytes,0,sizeof(job->lease_bytes));return RN_OK;
}
RN_API int r14_graph_coop_owned(const void *storage){const g_coop_job *j=(const g_coop_job*)storage;return j && j->magic==G_COOP_MAGIC?(int)j->owned:0;}
RN_API int r14_graph_coop_node(const void *storage){const g_coop_job *j=(const g_coop_job*)storage;return j && j->magic==G_COOP_MAGIC?(int)j->node:-1;}

/* This interface detaches actual immutable post inputs, not a completed depth.
 * No output completion is reported here. Queue RG completion remains explicit.
 */
RN_API int r14_graph_coop_gray_handoff_ready(const void *storage){
 const g_coop_job *j=(const g_coop_job *)storage;
 return j && j->magic==G_COOP_MAGIC && j->owned && j->detach_gray && j->phase==G_COOP_GRAY;
}
RN_API int r14_graph_coop_detach_gray(void *storage,uint8_t *rgb,size_t rgb_bytes,float *median,size_t median_bytes,float *output){
 g_coop_job *j=(g_coop_job *)storage;unsigned i;
 if(!j || j->magic!=G_COOP_MAGIC || !j->owned || !j->detach_gray)return RN_ARGUMENT;
 if(j->phase!=G_COOP_GRAY)return RN_BUSY_ERROR;
 if(!rgb || !median || !output || output!=j->output || rgb_bytes<3u*PN || median_bytes<PN*4u ||
    ((uintptr_t)rgb&63u) || ((uintptr_t)median&63u) || rgb_bytes>UINTPTR_MAX-(uintptr_t)rgb ||
    median_bytes>UINTPTR_MAX-(uintptr_t)median || span_overlap(rgb,rgb_bytes,median,median_bytes))return RN_ARGUMENT;
 for(i=0;i<16;i++)if(j->lease_bytes[i] && (span_overlap(rgb,rgb_bytes,j->lease[i],j->lease_bytes[i]) ||
    span_overlap(median,median_bytes,j->lease[i],j->lease_bytes[i])))return RN_ARGUMENT;
 if((j->ops.quiescent && !j->ops.quiescent(j->user)) ||
    (j->ops.integer_quiescent && !j->ops.integer_quiescent(j->user)) ||
    (j->ops.color_quiescent && !j->ops.color_quiescent(j->user)) ||
    (j->post && *j->post->ownership) || (j->cpu.begin && !j->cpu.quiescent(j->cpu_user)))return RN_BUSY_ERROR;
 if(!j->cpu_rgb_ready){rgb_native(j->g.left,j->rgb);}memcpy(rgb,j->rgb,3u*PN);memcpy(median,j->med,PN*4u);
 j->phase=G_COOP_POST_HANDED;return RN_OK;
}
RN_API int r14_graph_coop_release_frontend(void *storage){
 g_coop_job *j=(g_coop_job *)storage;
 if(!j || j->magic!=G_COOP_MAGIC || !j->owned || !j->detach_gray)return RN_ARGUMENT;
 if(j->phase!=G_COOP_POST_HANDED || j->lease_bytes[15])return RN_BUSY_ERROR;
 if((j->ops.quiescent && !j->ops.quiescent(j->user)) ||
    (j->ops.integer_quiescent && !j->ops.integer_quiescent(j->user)) ||
    (j->ops.color_quiescent && !j->ops.color_quiescent(j->user)) ||
    (j->post && *j->post->ownership) || (j->cpu.begin && !j->cpu.quiescent(j->cpu_user)))return RN_BUSY_ERROR;
 if(j->statistics){
  j->statistics[0]=j->g.conv_calls;j->statistics[1]=j->g.fp_quant_values;j->statistics[2]=j->g.integer_transport_values;
  j->statistics[3]=j->g.fixed_integer_values;j->statistics[4]=j->g.attribute_full_frame_bytes_avoided;
  j->statistics[5]=G_ARENA_BYTES;j->statistics[6]=j->g.direct_integer_values;
 }
 j->owned=0;j->phase=G_COOP_IDLE;memset(j->lease_bytes,0,sizeof(j->lease_bytes));return RN_OK;
}

#include "phase_leases_v163.inc"
#include "color_checkpoint_v161.inc"

#ifdef R14_HOST_REFERENCE
#include "attributes_original_v238.inc"
RN_API int r14_attributes_reuse_probe_v238(unsigned mode,const float *coarse,const float *native,const float *gray,const float *right_gray,float *output,int first,int last){
 r14_linear_axis ax[W],ay[H];int y;
 if(mode>1 || !coarse || !native || !gray || !right_gray || !output || first<0 || last<first || last>20480)return RN_ARGUMENT;
 linear_axis(80,W,ax);linear_axis(64,H,ay);
 for(y=0;y<128;y++)if(ay[2*y].low!=ay[2*y+1].low || ay[2*y].high!=ay[2*y+1].high)return RN_HARDWARE;
 if(mode)g_attributes_range_v179(coarse,native,gray,right_gray,output,ax,ay,first,last);
 else g_attributes_original_v238(coarse,native,gray,right_gray,output,ax,ay,first,last);
 return RN_OK;
}
#endif
