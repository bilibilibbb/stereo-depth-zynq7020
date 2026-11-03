#include "context_fusion_kernel_v199.h"

RN_API int64_t r14_round_fp24_v199(int64_t value){
    uint64_t a=(uint64_t)(value<0?-value:value),q,remainder,half;
    unsigned bits=0,shift;uint64_t t=a;
    while(t){bits++;t>>=1;}
    if(bits<=24u)return value;
    shift=bits-24u;half=UINT64_C(1)<<(shift-1u);
    q=a>>shift;remainder=a&((UINT64_C(1)<<shift)-1u);
    if(remainder>half || (remainder==half && (q&1u)))q++;
    a=q<<shift;
    return value<0?-(int64_t)a:(int64_t)a;
}

static int64_t r199_sum(const int16_t left[8],const int32_t right[8]){
    int64_t p[4]={0,0,0,0},sum;unsigned c;
    for(c=0;c<8u;c++){
        int64_t product=r14_round_fp24_v199((int64_t)left[c]*right[c]);
        p[c&3u]=r14_round_fp24_v199(p[c&3u]+product);
    }
    /* CUDA/NEON order: ((p0+p1)+p2)+p3, not a balanced tree. */
    sum=r14_round_fp24_v199(p[0]+p[1]);
    sum=r14_round_fp24_v199(sum+p[2]);
    return r14_round_fp24_v199(sum+p[3]);
}

static int16_t r199_q11(int64_t sum,unsigned fraction){
    uint64_t a=(uint64_t)(sum<0?-sum:sum),q,rest,half;
    unsigned shift=fraction+3u-11u;
    q=a>>shift;half=UINT64_C(1)<<(shift-1u);
    rest=a&((UINT64_C(1)<<shift)-1u);
    if(rest>half || (rest==half && (q&1u)))q++;
    if(q>32767u)q=32767u;
    return (int16_t)(sum<0?-(int32_t)q:(int32_t)q);
}

static int64_t r199_pixel(const int16_t *left,const int16_t *right,int d,int y,int x){
    int16_t l[8];int32_t r[8];int c;
    for(c=0;c<8;c++){
        unsigned base=(unsigned)(c*64+y)*80u;
        l[c]=left[base+(unsigned)x];
        if(d==16){
            /* Candidate16 means15.75. x15 retains its quarter contribution. */
            int32_t r0=x>=15?right[base+(unsigned)(x-15)]:0;
            int32_t r1=x>=16?right[base+(unsigned)(x-16)]:0;
            r[c]=r0+3*r1; /* Exact denominator2^13, no rounding needed here. */
        }else r[c]=x>=d?right[base+(unsigned)(x-d)]:0;
    }
    return r199_sum(l,r);
}

static int r199_overlap(const void *a,size_t an,const void *b,size_t bn){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x<an:x-y<bn;
}

RN_API int r14_context_fuse_v199(const int16_t *left,const int16_t *right,
    const float *prior,int16_t *out,size_t values){
    int d,y,x,i;const size_t feature_bytes=8u*5120u*2u;
    const size_t prior_bytes=4u*5120u*4u,out_bytes=29u*5120u*2u;
    if(!left || !right || !prior || !out || values<29u*5120u ||
        ((uintptr_t)left&1u) || ((uintptr_t)right&1u) ||
        ((uintptr_t)prior&3u) || ((uintptr_t)out&1u))return RN_ARGUMENT;
    if(r199_overlap(out,out_bytes,left,feature_bytes) ||
       r199_overlap(out,out_bytes,right,feature_bytes) ||
       r199_overlap(out,out_bytes,prior,prior_bytes))return RN_ARGUMENT;
    for(d=0;d<17;d++)for(y=0;y<64;y++)for(x=0;x<80;x++){
        int64_t sum=r199_pixel(left,right,d,y,x);
        out[(d*64+y)*80+x]=r199_q11(sum,d==16?24u:22u);
    }
    memcpy(out+17u*5120u,left,feature_bytes);
    /* Existing prior quantization remains CPU work and is unchanged. */
    for(i=0;i<4*5120;i++)out[25*5120+i]=quantize_scaled(prior[i],2048.0);
    return RN_OK;
}

#ifdef R14_HOST_REFERENCE
/* Independent scalar FP32 oracle: no numerator-rounding helper is reused. */
static uint32_t r199_bits(float x){uint32_t b;memcpy(&b,&x,4);return b;}
static int r199_directed_one(const int16_t l[8],const int16_t r0[8],const int16_t r1[8],int interp){
    float p[4]={0,0,0,0},sum;int32_t weighted[8];unsigned c;
    int64_t actual;
    for(c=0;c<8u;c++){
        float left=(float)l[c]*(1.0f/2048.0f);
        float right=(float)r0[c]*(1.0f/2048.0f);
        if(interp){float next=(float)r1[c]*(1.0f/2048.0f);right=right*.25f+next*.75f;}
        weighted[c]=interp?(int32_t)r0[c]+3*(int32_t)r1[c]:r0[c];
        p[c&3u]+=left*right;
    }
    sum=p[0]+p[1];sum+=p[2];sum+=p[3];sum*=.125f;
    actual=r199_sum(l,weighted);
    if(r199_bits(sum)!=r199_bits(ldexpf((float)actual,interp?-27:-25)))return RN_HARDWARE;
    if(quantize_scaled(sum,2048.0)!=r199_q11(actual,interp?24u:22u))return RN_HARDWARE;
    return RN_OK;
}
RN_API int r14_context_directed_v199(uint32_t count){
    static const int16_t extremes[]={-32768,-32767,-16384,-1,0,1,16384,32766,32767};
    uint32_t seed=0x1997042u,i;unsigned c,k;int16_t l[8],r0[8],r1[8];
    if(count!=262144u)return RN_ARGUMENT;
    for(k=0;k<9u*9u*9u;k++){
        for(c=0;c<8u;c++){
            l[c]=extremes[(k+c)%9u];r0[c]=extremes[(k/9u+2*c)%9u];r1[c]=extremes[(k/81u+3*c)%9u];
        }
        if(r199_directed_one(l,r0,r1,0) || r199_directed_one(l,r0,r1,1))return RN_HARDWARE;
    }
    for(i=0;i<count;i++){
        for(c=0;c<8u;c++){
            seed=1664525u*seed+1013904223u;l[c]=(int16_t)(seed>>16);
            seed=1664525u*seed+1013904223u;r0[c]=(int16_t)(seed>>16);
            seed=1664525u*seed+1013904223u;r1[c]=(int16_t)(seed>>16);
        }
        if(r199_directed_one(l,r0,r1,0) || r199_directed_one(l,r0,r1,1))return RN_HARDWARE;
    }
    return RN_OK;
}
RN_API int r14_context_fixture_v199(const int16_t *left,const int16_t *right,const float *prior,
    const float *corr,const int16_t *expected,int16_t *out,size_t values){
    int d,y,x,rc=r14_context_fuse_v199(left,right,prior,out,values);
    if(rc || !corr || !expected)return RN_ARGUMENT;
    for(d=0;d<17;d++)for(y=0;y<64;y++)for(x=0;x<80;x++){
        int at=(d*64+y)*80+x;int64_t q=r199_pixel(left,right,d,y,x);
        if(r199_bits(corr[at])!=r199_bits(ldexpf((float)q,d==16?-27:-25)))return RN_HARDWARE;
    }
    return memcmp(out,expected,29u*5120u*2u)?RN_HARDWARE:RN_OK;
}
#endif
