#include "retained_model.h"
#include "retained_post_buffers.h"
#include "retained_model_plan.h"
#include <math.h>
#include "r14_exact_cpu_hotspots_v169.h"
#include "r14_cpu_vectors_v172.h"
#include <string.h>
#include "r14_profile.h"
#define H 256
#define W 320
#define N (H*W)
#define CN (64*80)
#define PN (240*320)
size_t retained_filter_scratch_bytes(void);
int retained_color_context(const float *,const float *,float *,double *,size_t);
int retained_fast_gray4(const uint8_t *,const float *,float *,double *,size_t);
static size_t align8(size_t n){return (n+7u)&~(size_t)7u;}
RN_API size_t retained_model_workspace_bytes(void){
    return align8(RN_ARENA_FLOATS*4u)+align8(RN_MAX_TENSOR*4u)+align8((20u*CN+2u*PN)*4u+3u*PN)+retained_filter_scratch_bytes();
}
RN_API size_t retained_model_workspace_bytes_buffers(void){
    return retained_model_workspace_bytes()-align8(RN_MAX_TENSOR*4u);
}
static int span_overlap(const void *a,size_t an,const void *b,size_t bn){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(an>UINTPTR_MAX-x || bn>UINTPTR_MAX-y)return 1;
    return x<=y?y-x<an:x-y<bn;
}
static int buffer_overlap(const rn_i16_buffers *b,const void *p,size_t n){
    return span_overlap(b->input,b->input_bytes,p,n) || span_overlap(b->output,b->output_bytes,p,n);
}
static float clamp(float v,float low,float high){return v<low?low:(v>high?high:v);}
static int clip(int n,int high){return n<0?0:(n>=high?high-1:n);}
static int16_t quantize(float x,int fraction){
    double a=ldexp(fabs((double)x),fraction);uint32_t whole;double rest;
    if(a>=32767.0)return x<0?-32767:32767;
    whole=(uint32_t)a;rest=a-whole;if(rest>.5 || (rest==.5 && (whole&1u)))whole++;
    return (int16_t)(x<0?-(int32_t)whole:(int32_t)whole);
}
/* Float32 inputs promoted to double and a power-of-two scale are exact.
 * The frozen formats stay in range; ties-to-even and saturation are unchanged.
 * Compute the scale once for each convolution instead of for each element. */
static int16_t quantize_scaled(float x,double scale){
    double a=fabs((double)x)*scale;uint32_t whole;double rest;
    if(a>=32767.0)return x<0?-32767:32767;
    whole=(uint32_t)a;rest=a-whole;if(rest>.5 || (rest==.5 && (whole&1u)))whole++;
    return (int16_t)(x<0?-(int32_t)whole:(int32_t)whole);
}
static int16_t requant(int64_t x,int shift){
    uint64_t mag=(uint64_t)(x<0?-x:x),q=mag;
    if(shift){uint64_t rest=mag&(((uint64_t)1<<shift)-1u),half=(uint64_t)1<<(shift-1);q=mag>>shift;if(rest>half || (rest==half && (q&1u)))q++;}
    if(q>32767u)q=32767u;
    return (int16_t)(x<0?-(int32_t)q:(int32_t)q);
}

/* Frozen dyadic formats: identical to dequantize, ReLU6, then quantize_scaled.
 * Keep the symmetric32767 saturation and round-to-even of the reference. */
#include "r14_neon_transport_v3.h"
static int qchain_can_reuse(int index){
    const rn_node *node,*previous;const rn_parameter *a,*b;
    if(index<=0 || index>=RN_NODES)return 0;
    node=&rn_nodes[index];previous=&rn_nodes[index-1];
    if(node->op!=RN_OP_CONV || previous->op!=RN_OP_CONV || node->input[0]!=previous->output)return 0;
    a=&rn_parameters[previous->parameter];b=&rn_parameters[node->parameter];
    return a->output_frac>=0 && a->output_frac<=24 && b->input_frac>=0 && b->input_frac<=24;
}
static int qchain_skip_float(int index){
    int k,j,uses=0,tensor=rn_nodes[index].output;
    if(index+1>=RN_NODES || !qchain_can_reuse(index+1))return 0;
    /* These tensors also have implicit consumers outside the graph loop. */
    if(tensor==2 || tensor==3 || tensor==RN_COARSE || tensor==RN_FINAL)return 0;
    for(k=0;k<RN_NODES;k++)for(j=0;j<rn_nodes[k].inputs;j++)if(rn_nodes[k].input[j]==tensor)uses++;
    return uses==1;
}

static int conv_integer(const rn_parameter *p,const int16_t *src,int ih,int iw,int16_t *dst,int oh,int ow){
    int o,y,x,c,ky,kx,iy,ix,ci0,ci1;int pad=p->dilation*(p->k-1)/2;
    for(o=0;o<p->co;o++){
        ci0=p->groups==1?0:o;ci1=p->groups==1?p->ci:o+1;
        for(y=0;y<oh;y++)for(x=0;x<ow;x++){
            int64_t sum=p->bias[o];
            for(c=ci0;c<ci1;c++)for(ky=0;ky<p->k;ky++){
                iy=y*p->stride-pad+ky*p->dilation;if(iy<0 || iy>=ih)continue;
                for(kx=0;kx<p->k;kx++){
                    int wi=((o*(ci1-ci0)+(c-ci0))*p->k+ky)*p->k+kx;
                    ix=x*p->stride-pad+kx*p->dilation;
                    if(ix>=0 && ix<iw)sum+=(int32_t)src[(c*ih+iy)*iw+ix]*(int32_t)p->weights[wi];
                }
            }
            dst[(o*oh+y)*ow+x]=requant(sum,p->right_shift);
        }
    }
    return 0;
}
static float bilinear_at(const float *src,int ih,int iw,int y,int x,int oh,int ow){
    float fy=((float)y+.5f)*((float)ih/(float)oh)-.5f,fx=((float)x+.5f)*((float)iw/(float)ow)-.5f;
    int y0,x0,y1,x1;float dy,dx,a,b;
    if(fy<0)fy=0;
    if(fx<0)fx=0;
    y0=(int)fy;x0=(int)fx;y1=clip(y0+1,ih);x1=clip(x0+1,iw);dy=fy-(float)y0;dx=fx-(float)x0;
    a=(1-dx)*src[y0*iw+x0]+dx*src[y0*iw+x1];b=(1-dx)*src[y1*iw+x0]+dx*src[y1*iw+x1];return (1-dy)*a+dy*b;
}
typedef struct {int low,high;float fraction,keep;} r14_linear_axis;
static void linear_axis(int input,int output,r14_linear_axis *axis){
    float scale=(float)input/(float)output;int i;
    for(i=0;i<output;i++){
        float position=((float)i+.5f)*scale-.5f;
        if(position<0)position=0;
        axis[i].low=(int)position;axis[i].high=clip(axis[i].low+1,input);
        axis[i].fraction=position-(float)axis[i].low;axis[i].keep=1-axis[i].fraction;
    }
}
static float bilinear_cached(const float *src,int iw,const r14_linear_axis *y,const r14_linear_axis *x){
    float a=x->keep*src[y->low*iw+x->low]+x->fraction*src[y->low*iw+x->high];
    float b=x->keep*src[y->high*iw+x->low]+x->fraction*src[y->high*iw+x->high];
    return y->keep*a+y->fraction*b;
}
static void resize(const float *src,int c,int ih,int iw,float *dst,int oh,int ow){
    r14_linear_axis ax[W],ay[H];int n,y,x;
    if(ow>W || oh>H){
        for(n=0;n<c;n++)for(y=0;y<oh;y++)for(x=0;x<ow;x++)dst[(n*oh+y)*ow+x]=bilinear_at(src+n*ih*iw,ih,iw,y,x,oh,ow);
        return;
    }
    linear_axis(iw,ow,ax);linear_axis(ih,oh,ay);
    for(n=0;n<c;n++)for(y=0;y<oh;y++)for(x=0;x<ow;x++)
        dst[(n*oh+y)*ow+x]=bilinear_cached(src+n*ih*iw,iw,ay+y,ax+x);
}
static void pool2(const float *src,int c,int oh,int ow,float *dst){
    int n,y,x;int iw=ow*2,ih=oh*2;
    for(n=0;n<c;n++)for(y=0;y<oh;y++)for(x=0;x<ow;x++){
        int at=(n*ih+2*y)*iw+2*x;float sum=src[at];sum+=src[at+1];sum+=src[at+iw];sum+=src[at+iw+1];dst[(n*oh+y)*ow+x]=sum*.25f;
    }
}
static void priors_range_v179(const float *native,float *dst,int first,int last){
    int y,x,dy,dx;for(y=first/80;y<(last+79)/80;y++)for(x=0;x<80;x++){
        float coverage=0,d=0,margin=0,other=0;
        if(y*80+x<first || y*80+x>=last)continue;
        for(dy=0;dy<4;dy++)for(dx=0;dx<4;dx++){
            int at=(y*4+dy)*W+x*4+dx;float valid=native[2*N+at];coverage+=valid;d+=native[at]*valid;margin+=native[N+at];other+=native[3*N+at];
        }
        coverage*=.0625f;d*=.0625f;
        dst[y*80+x]=d/(coverage>.0625f?coverage:.0625f);dst[CN+y*80+x]=margin*.0625f;dst[2*CN+y*80+x]=coverage;dst[3*CN+y*80+x]=other*.0625f;
    }
}
static void priors(const float *native,float *dst){priors_range_v179(native,dst,0,CN);}
static void correlation(const float *left,const float *right,float *dst){
    int d,y,x,c;for(d=0;d<17;d++)for(y=0;y<64;y++)for(x=0;x<80;x++){
        int shift=d==16?15:d;float sum,partial[4]={0,0,0,0};
        for(c=0;c<8;c++){
            float r=x>=shift?right[(c*64+y)*80+x-shift]:0;
            if(d==16){float r1=x>=16?right[(c*64+y)*80+x-16]:0;r=r*.25f+r1*.75f;}
            /* Match the frozen CUDA reduction's four unrolled accumulators. */
            partial[c&3]+=left[(c*64+y)*80+x]*r;
        }
        sum=partial[0]+partial[1];sum+=partial[2];sum+=partial[3];
        dst[(d*64+y)*80+x]=sum*.125f;
    }
}
static void colour_guide_scalar_v172(const float *left,float *dst){
    static const float mean[3]={.485f,.456f,.406f},sd[3]={.229f,.224f,.225f};int c,y,x,dy,dx;
    for(c=0;c<3;c++)for(y=0;y<64;y++)for(x=0;x<80;x++){
        float sum=0;for(dy=0;dy<4;dy++)for(dx=0;dx<4;dx++){float v=left[(c*H+y*4+dy)*W+x*4+dx]*sd[c];v+=mean[c];sum+=clamp(v,0,1);}
        dst[(c*64+y)*80+x]=sum*.0625f;
    }
}
static void colour_guide(const float *,float *);
static int decode(float *logits,const float *left,const float *curve,float *dst,float *guide,float *filtered,void *scratch,size_t bytes){
    int at,d;float e[17];
    for(at=0;at<CN;at++){
        float sum=0;int count=0;
        for(d=0;d<17;d++)if(curve[(17+d)*CN+at]>0){sum+=curve[d*CN+at];count++;}
        sum/=(float)(count?count:1);
        for(d=0;d<17;d++){
            float centered=curve[d*CN+at]-sum;centered*=8;centered*=curve[(17+d)*CN+at]>0?1.0f:0.0f;logits[d*CN+at]-=centered;
        }
    }
    colour_guide(left,guide);
    {int context_error;R14P_BEGIN(R14P_COLOR_CONTEXT);context_error=retained_color_context(logits,guide,filtered,scratch,bytes);R14P_END(R14P_COLOR_CONTEXT,context_error);if(context_error)return 20;}
    for(at=0;at<CN;at++){
        int peak=0;float maximum=-1e4f,sum=0,entropy=0,local_sum=0,disparity=0;
        for(d=0;d<17;d++){float v=filtered[d*CN+at];if((d==16?15.75f:(float)d)>(float)(at%80))v=-1e4f;e[d]=v;if(v>maximum){maximum=v;peak=d;}}
        for(d=0;d<17;d++){e[d]=expf(e[d]-maximum);sum+=e[d];}
        for(d=0;d<17;d++){float p=e[d]/sum;entropy+=p*logf(p>1e-12f?p:1e-12f);if(d>=peak-1 && d<=peak+1)local_sum+=e[d];}
        for(d=0;d<17;d++)if(d>=peak-1 && d<=peak+1)disparity+=(e[d]/local_sum)*(float)(d==16?63:4*d);
        dst[at]=disparity;dst[CN+at]=-entropy/logf(17.0f);
    }
    return 0;
}
static void attributes(const float *coarse,const float *native,const float *gray,const float *right_gray,float *dst){
    r14_linear_axis ax[W],ay[H];int y,x;
    linear_axis(80,W,ax);linear_axis(64,H,ay);
    for(y=0;y<H;y++)for(x=0;x<W;x++){
        int at=y*W+x;float base=coarse[(y/4)*80+x/4],coord=(float)x-base,lo_float=floorf(coord),fraction=coord-lo_float;
        int lo=clip((int)lo_float,W),hi=clip(lo+1,W);float warped=right_gray[y*W+lo]*(1-fraction)+right_gray[y*W+hi]*fraction;
        dst[at]=base/64;dst[N+at]=native[at];dst[2*N+at]=bilinear_cached(coarse+CN,80,ay+y,ax+x);dst[3*N+at]=native[N+at];dst[4*N+at]=gray[at];
        dst[5*N+at]=x+1<W?gray[at+1]-gray[at]:0;dst[6*N+at]=y+1<H?gray[at+W]-gray[at]:0;dst[7*N+at]=fabsf(gray[at]-warped)*(coord>=0?1.0f:0.0f);
    }
}
#include "r14_final_lut_v1.h"
static void final_prediction(const float *fine,const float *coarse,const float *native,float *pred){
    int y,x;for(y=0;y<240;y++)for(x=0;x<W;x++){
        int at=(y+16)*W+x;float gate=r14_final_gate_v1(fine[N+at]),valid=native[2*N+at];
        float blend=gate*valid+(1-valid),prior=native[at]*64,base=coarse[((y+16)/4)*80+x/4];
        float value=prior*(1-blend)+(base+4*r14_final_tanh_v1(fine[at]))*blend;pred[y*W+x]=clamp(value,0,63.99609375f);
    }
}
#include "r14_median_simd_v2.h"
static float median_scalar_pixel_v2(const float *src,int y,int x){
    int ky,kx;float v[9];
    for(ky=0;ky<3;ky++)for(kx=0;kx<3;kx++)v[ky*3+kx]=src[clip(y+ky-1,240)*W+clip(x+kx-1,W)];
        if(v[0]>v[1]){float t=v[0];v[0]=v[1];v[1]=t;}
        if(v[2]>v[3]){float t=v[2];v[2]=v[3];v[3]=t;}
        if(v[4]>v[5]){float t=v[4];v[4]=v[5];v[5]=t;}
        if(v[6]>v[7]){float t=v[6];v[6]=v[7];v[7]=t;}
        if(v[1]>v[2]){float t=v[1];v[1]=v[2];v[2]=t;}
        if(v[3]>v[4]){float t=v[3];v[3]=v[4];v[4]=t;}
        if(v[5]>v[6]){float t=v[5];v[5]=v[6];v[6]=t;}
        if(v[7]>v[8]){float t=v[7];v[7]=v[8];v[8]=t;}
        if(v[0]>v[1]){float t=v[0];v[0]=v[1];v[1]=t;}
        if(v[2]>v[3]){float t=v[2];v[2]=v[3];v[3]=t;}
        if(v[4]>v[5]){float t=v[4];v[4]=v[5];v[5]=t;}
        if(v[6]>v[7]){float t=v[6];v[6]=v[7];v[7]=t;}
        if(v[1]>v[2]){float t=v[1];v[1]=v[2];v[2]=t;}
        if(v[3]>v[4]){float t=v[3];v[3]=v[4];v[4]=t;}
        if(v[5]>v[6]){float t=v[5];v[5]=v[6];v[6]=t;}
        if(v[7]>v[8]){float t=v[7];v[7]=v[8];v[8]=t;}
        if(v[0]>v[1]){float t=v[0];v[0]=v[1];v[1]=t;}
        if(v[2]>v[3]){float t=v[2];v[2]=v[3];v[3]=t;}
        if(v[4]>v[5]){float t=v[4];v[4]=v[5];v[5]=t;}
        if(v[6]>v[7]){float t=v[6];v[6]=v[7];v[7]=t;}
        if(v[1]>v[2]){float t=v[1];v[1]=v[2];v[2]=t;}
        if(v[3]>v[4]){float t=v[3];v[3]=v[4];v[4]=t;}
        if(v[5]>v[6]){float t=v[5];v[5]=v[6];v[6]=t;}
        if(v[7]>v[8]){float t=v[7];v[7]=v[8];v[8]=t;}
        if(v[0]>v[1]){float t=v[0];v[0]=v[1];v[1]=t;}
        if(v[2]>v[3]){float t=v[2];v[2]=v[3];v[3]=t;}
        if(v[4]>v[5]){float t=v[4];v[4]=v[5];v[5]=t;}
        if(v[6]>v[7]){float t=v[6];v[6]=v[7];v[7]=t;}
        if(v[1]>v[2]){float t=v[1];v[1]=v[2];v[2]=t;}
        if(v[3]>v[4]){float t=v[3];v[3]=v[4];v[4]=t;}
        if(v[5]>v[6]){float t=v[5];v[5]=v[6];v[6]=t;}
        if(v[7]>v[8]){float t=v[7];v[7]=v[8];v[8]=t;}
        if(v[0]>v[1]){float t=v[0];v[0]=v[1];v[1]=t;}
        if(v[2]>v[3]){float t=v[2];v[2]=v[3];v[3]=t;}
        if(v[4]>v[5]){float t=v[4];v[4]=v[5];v[5]=t;}
        if(v[6]>v[7]){float t=v[6];v[6]=v[7];v[7]=t;}
    return v[4];
}
static void median(const float *src,float *dst){
    r14_median_simd_v2(src,dst,median_scalar_pixel_v2);
}
static void rgb_native_scalar_v172(const float *left,uint8_t *dst){
    static const float mean[3]={.485f,.456f,.406f},sd[3]={.229f,.224f,.225f};int c,y,x;
    for(c=0;c<3;c++)for(y=0;y<240;y++)for(x=0;x<W;x++){
        float v=left[(c*H+y+16)*W+x]*sd[c];v+=mean[c];v=clamp(v*255,0,255);dst[(c*240+y)*W+x]=(v>=0 && v<=255)?r14_rgb_round_v169(v):(uint8_t)quantize(v,0);
    }
}
#include "colour_rgb_v172.inc"
static int profiled_model_forward(const float *left,const float *right,const float *native,const float *gray,const float *right_gray,const float *curve,
    float *output,void *workspace,size_t bytes,rn_conv_backend backend,void *user,const rn_i16_buffers *buffers,const rn_post_buffers *post){
    float *values[RN_TENSORS],*arena=(float*)workspace,*guide,*filtered,*pre,*med;int16_t *qin,*qout;uint8_t *rgb;void *scratch;
    size_t filter_bytes=retained_filter_scratch_bytes();int i,j,rc=0;
    if(!left || !right || !native || !gray || !right_gray || !curve || !output || !workspace || ((uintptr_t)workspace&7u) ||
       bytes<(buffers?retained_model_workspace_bytes_buffers():retained_model_workspace_bytes()))return 1;
    if(post){
        if(!post->source || !post->destination || !post->median || !post->ownership ||
           ((uintptr_t)post->source&31u) || ((uintptr_t)post->destination&31u) ||
           post->source_bytes<PN*4u || post->destination_bytes<PN*4u ||
           span_overlap(post->source,post->source_bytes,post->destination,post->destination_bytes) ||
           span_overlap(post,sizeof(*post),post->source,post->source_bytes) || span_overlap(post,sizeof(*post),post->destination,post->destination_bytes) ||
           span_overlap(post,sizeof(*post),workspace,bytes) ||
           span_overlap(post->ownership,sizeof(*post->ownership),post->source,post->source_bytes) ||
           span_overlap(post->ownership,sizeof(*post->ownership),post->destination,post->destination_bytes))return 1;
        if(*post->ownership)return 4;
        {
            const void *p[8]={left,right,native,gray,right_gray,curve,output,workspace};
            size_t n[8]={3u*N*4u,3u*N*4u,4u*N*4u,N*4u,N*4u,34u*CN*4u,N*4u,bytes};
            int k;for(k=0;k<8;k++)if(span_overlap(post->source,post->source_bytes,p[k],n[k]) ||
                span_overlap(post->destination,post->destination_bytes,p[k],n[k]))return 1;
        }
        if(buffers && (buffer_overlap(buffers,post->source,post->source_bytes) ||
            buffer_overlap(buffers,post->destination,post->destination_bytes)))return 1;
    }
    if(buffers){
        if(!backend || !buffers->input || !buffers->output || ((uintptr_t)buffers->input&1u) || ((uintptr_t)buffers->output&1u) ||
           buffers->input_bytes<RN_MAX_TENSOR*2u || buffers->output_bytes<RN_MAX_TENSOR*2u ||
           span_overlap(buffers->input,buffers->input_bytes,buffers->output,buffers->output_bytes) ||
           buffer_overlap(buffers,workspace,bytes) || buffer_overlap(buffers,left,3u*N*4u) || buffer_overlap(buffers,right,3u*N*4u) ||
           buffer_overlap(buffers,native,4u*N*4u) || buffer_overlap(buffers,gray,N*4u) || buffer_overlap(buffers,right_gray,N*4u) ||
           buffer_overlap(buffers,curve,34u*CN*4u) || buffer_overlap(buffers,output,N*4u))return 1;
        qin=buffers->input;qout=buffers->output;
        guide=(float*)((uint8_t*)workspace+align8(RN_ARENA_FLOATS*4u));
    }else{
        qin=(int16_t*)((uint8_t*)workspace+align8(RN_ARENA_FLOATS*4u));qout=qin+RN_MAX_TENSOR;
        guide=(float*)((uint8_t*)qin+align8(RN_MAX_TENSOR*4u));
    }
    filtered=guide+3*CN;pre=filtered+17*CN;med=pre+PN;rgb=(uint8_t*)(med+PN);
    scratch=(uint8_t*)guide+align8((20u*CN+2u*PN)*4u+3u*PN);
    if(post){pre=post->source;med=post->destination;}
    values[0]=(float*)left;values[1]=(float*)right;
    for(i=2;i<RN_TENSORS;i++)values[i]=arena+rn_tensors[i].offset;
    priors(native,values[2]);
    for(i=0;i<RN_NODES;i++){
        const rn_node *node=&rn_nodes[i];const rn_tensor_info *out=&rn_tensors[node->output];
        const rn_tensor_info *in=&rn_tensors[node->input[0]];float *dst=values[node->output],*src=values[node->input[0]];int count=out->c*out->h*out->w;
        if(node->op!=RN_OP_CONV)R14P_BEGIN(R14P_FIXED_NODE);
        if(node->op==RN_OP_CONV){
            const rn_parameter *p=&rn_parameters[node->parameter];int inputs=in->c*in->h*in->w;
            double input_scale=ldexp(1.0,p->input_frac);float output_scale=ldexpf(1.0f,-p->output_frac);
            R14P_BEGIN(R14P_QUANTIZE);
            if(qchain_can_reuse(i)){
                const rn_node *previous=&rn_nodes[i-1];const rn_parameter *a=&rn_parameters[previous->parameter];
                r14_transport_neon_v3(qout,qin,(size_t)inputs,a->output_frac,p->input_frac,previous->relu);
            }else for(j=0;j<inputs;j++)qin[j]=quantize_scaled(src[j],input_scale);
            R14P_END(R14P_QUANTIZE,0);
            rc=backend?backend(user,node->parameter,p,qin,in->h,in->w,qout,out->h,out->w):conv_integer(p,qin,in->h,in->w,qout,out->h,out->w);
            if(rc)return 1000+i;
            if(!qchain_skip_float(i)){
                R14P_BEGIN(R14P_DEQUANTIZE);
                for(j=0;j<count;j++){float v=(float)qout[j]*output_scale;dst[j]=node->relu?clamp(v,0,6):v;}
                R14P_END(R14P_DEQUANTIZE,0);
            }
        }else if(node->op==RN_OP_ADD){for(j=0;j<count;j++)dst[j]=clamp(src[j]+values[node->input[1]][j],0,6);}
        else if(node->op==RN_OP_RESIZE)resize(src,in->c,in->h,in->w,dst,out->h,out->w);
        else if(node->op==RN_OP_POOL){attributes(values[RN_COARSE],native,gray,right_gray,values[3]);pool2(src,in->c,out->h,out->w,dst);}
        else if(node->op==RN_OP_CORRELATION)correlation(src,values[node->input[1]],dst);
        else if(node->op==RN_OP_CONCAT){int offset=0,k;for(k=0;k<node->inputs;k++){const rn_tensor_info *a=&rn_tensors[node->input[k]];int n=a->c*a->h*a->w;memcpy(dst+offset,values[node->input[k]],(size_t)n*sizeof(float));offset+=n;}}
        else if(node->op==RN_OP_DECODE){rc=decode(src,left,curve,dst,guide,filtered,scratch,filter_bytes);if(rc)return rc;}
        else return 2;
        if(node->op!=RN_OP_CONV)R14P_END(R14P_FIXED_NODE,0);
    }
    R14P_BEGIN(R14P_FINAL_PREDICTION);final_prediction(values[RN_FINAL],values[RN_COARSE],native,pre);R14P_END(R14P_FINAL_PREDICTION,0);
    R14P_BEGIN(R14P_MEDIAN);
    if(post)rc=post->median(post->user,pre,med);else {median(pre,med);rc=0;}
    R14P_END(R14P_MEDIAN,rc);if(rc)return 6000+rc;
    R14P_BEGIN(R14P_RGB_NATIVE);rgb_native(left,rgb);R14P_END(R14P_RGB_NATIVE,0);
    R14P_BEGIN(R14P_GRAY_FILTER);rc=retained_fast_gray4(rgb,med,output+16*W,scratch,filter_bytes);R14P_END(R14P_GRAY_FILTER,rc);if(rc)return 30;
    for(i=0;i<16;i++)memcpy(output+i*W,output+16*W,W*sizeof(float));
    return 0;
}
RN_API int retained_model_forward(const float *left,const float *right,const float *native,const float *gray,const float *right_gray,const float *curve,
    float *output,void *workspace,size_t bytes,rn_conv_backend backend,void *user){
    return retained_model_forward_buffers(left,right,native,gray,right_gray,curve,output,workspace,bytes,backend,user,0);
}
RN_API int retained_model_forward_buffers(const float *left,const float *right,const float *native,const float *gray,const float *right_gray,const float *curve,
    float *output,void *workspace,size_t bytes,rn_conv_backend backend,void *user,const rn_i16_buffers *buffers){
    int rc;R14P_BEGIN(R14P_MODEL);
    rc=profiled_model_forward(left,right,native,gray,right_gray,curve,output,workspace,bytes,backend,user,buffers,0);
    R14P_ABORT(R14P_QUANTIZE,R14P_GRAY_FILTER,rc);R14P_END(R14P_MODEL,rc);return rc;
}

RN_API int retained_model_forward_post(const float *left,const float *right,const float *native,const float *gray,const float *right_gray,const float *curve,
    float *output,void *workspace,size_t bytes,rn_conv_backend backend,void *user,const rn_i16_buffers *buffers,const rn_post_buffers *post){
    int rc;R14P_BEGIN(R14P_MODEL);
    rc=profiled_model_forward(left,right,native,gray,right_gray,curve,output,workspace,bytes,backend,user,buffers,post);
    R14P_ABORT(R14P_QUANTIZE,R14P_GRAY_FILTER,rc);R14P_END(R14P_MODEL,rc);return rc;
}
#ifdef R14_POST_VALIDATION_EXPORTS
RN_API int retained_conv_reference_export(const rn_parameter *p,const int16_t *src,int ih,int iw,int16_t *dst,int oh,int ow){
    return conv_integer(p,src,ih,iw,dst,oh,ow);
}
RN_API int retained_median_reference_export(const float *src,float *dst){
    int y,x;for(y=0;y<240;y++)for(x=0;x<W;x++)dst[y*W+x]=median_scalar_pixel_v2(src,y,x);
    return 0;
}
#endif
