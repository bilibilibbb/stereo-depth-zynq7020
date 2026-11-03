/* Portable Cortex-A9 implementations of the accepted colour context and post
 * filters. No Torch, heap, OS calls, fast-math or FP contraction. The caller
 * owns aligned DDR scratch space. Binary64 is retained for covariance solves.
 * These functions are implementation components; board latency is unmeasured.
 */
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <float.h>
#include <string.h>
#ifdef _MSC_VER
#define API __declspec(dllexport)
#define FINITE(x) _finite(x)
#else
#define API
#define FINITE(x) isfinite(x)
#endif
#define CW 80
#define CH 64
#define CN (CW*CH)
#define CD 17
#define CTX_DOUBLES ((9+9+17+68+2)*CN+9*CH*(CW+1))
#define PN (240*320)
#define LN (60*80)
#define POST_DOUBLES (PN+15*LN)

static int clip(int x,int hi) { return x<0?0:(x>=hi?hi-1:x); }
API size_t retained_filter_scratch_bytes(void) { return CTX_DOUBLES*sizeof(double); }


/* Software value-screen only. All box inputs must have |x|<=65536.
 * 81*65536*2^40 < 2^63 proves signed64 safety for both box axes.
 * Prefix inputs have the separate |x|<=16384 bound and at most80 terms.
 * Conversion rounds ties to even. No saturation or floating fallback.
 */
#define Q40_SCALE 1099511627776.0
#define Q40_INV 0.0000000000009094947017729282379150390625
static uint64_t q40_count[7],gray_interp_count[5],color_affine_count[6];
API void retained_q40_reset(void) {
    int i;for(i=0;i<7;i++)q40_count[i]=0;for(i=0;i<5;i++)gray_interp_count[i]=0;for(i=0;i<6;i++)color_affine_count[i]=0;
}
API int retained_color_affine_stats(uint64_t *out,size_t n) {
    int i;if(!out||n!=6)return 1;for(i=0;i<6;i++)out[i]=color_affine_count[i];return 0;
}
API int retained_gray_interp_stats(uint64_t *out,size_t n) {
    int i;if(!out||n!=5)return 1;for(i=0;i<5;i++)out[i]=gray_interp_count[i];return 0;
}
API int retained_q40_stats(uint64_t *out,size_t n) {
    int i;if(!out||n!=7)return 1;for(i=0;i<7;i++)out[i]=q40_count[i];return 0;
}
static int64_t q40_input(double x,double bound) {
    double mag=fabs(x),scaled,rest;uint64_t whole,bits;
    q40_count[1]++;
    if(!FINITE(x)||mag>bound){q40_count[3]++;return 0;}
    memcpy(&bits,&mag,sizeof(bits));if(bits>q40_count[4])q40_count[4]=bits;
    scaled=mag*Q40_SCALE;whole=(uint64_t)scaled;rest=scaled-(double)whole;
    if(rest>0.5 || (rest==0.5 && (whole&1u)))whole++;
    return x<0.0?-(int64_t)whole:(int64_t)whole;
}
static int64_t q40_add(int64_t a,int64_t b) {
    int64_t v=a+b;uint64_t mag=(uint64_t)(v<0?-v:v);
    q40_count[2]++;if(mag>q40_count[5])q40_count[5]=mag;return v;
}
static void vertical_integer(const int64_t *src,int64_t *dst,int h,int w) {
    int x,y,k;
    for(x=0;x<w;x++) {
        int64_t v=src[clip(-4,h)*w+x];
        for(k=-3;k<=4;k++)v=q40_add(v,src[clip(k,h)*w+x]);
        dst[x]=v;
        for(y=1;y<h;y++) {
            v=q40_add(v,-src[clip(y-5,h)*w+x]);
            v=q40_add(v,src[clip(y+4,h)*w+x]);dst[y*w+x]=v;
        }
    }
}
static void vertical(const double *src,double *dst,int h,int w) {
    int64_t input[CN],result[CN];int i;
    if(h*w>CN){q40_count[3]++;return;}
    for(i=0;i<h*w;i++)input[i]=q40_input(src[i],65536.0);
    vertical_integer(input,result,h,w);
    for(i=0;i<h*w;i++)dst[i]=(double)result[i]*Q40_INV;
    q40_count[0]++;
}
static void box(const double *src,double *dst,double *tmp,int h,int w) {
    int64_t input[CN],middle[CN];int x,y,k,i;
    (void)tmp;
    if(h*w>CN){q40_count[3]++;return;}
    for(i=0;i<h*w;i++)input[i]=q40_input(src[i],65536.0);
    vertical_integer(input,middle,h,w);
    for(y=0;y<h;y++) {
        int64_t v=middle[y*w+clip(-4,w)];
        for(k=-3;k<=4;k++)v=q40_add(v,middle[y*w+clip(k,w)]);
        dst[y*w]=(double)v*Q40_INV;
        for(x=1;x<w;x++) {
            v=q40_add(v,-middle[y*w+clip(x-5,w)]);
            v=q40_add(v,middle[y*w+clip(x+4,w)]);dst[y*w+x]=(double)v*Q40_INV;
        }
    }
    q40_count[0]++;
}

API int retained_color_context(const float *logits,const float *guide,float *out,
                               double *work,size_t bytes) {
    double *gf=work,*gv=gf+9*CN,*gp=gv+9*CN,*cost=gp+9*CH*(CW+1);
    double *cs=cost+CD*CN,*tmp=cs+4*CD*CN,*vertical_tmp=tmp+CN;
    double inv[82];int y,x,d,c,k,j,i;
    if(!logits||!guide||!out||!work||bytes<CTX_DOUBLES*sizeof(double))return 1;
    for(k=0;k<82;k++)inv[k]=1.0/(k?k:1);
    for(i=0;i<3*CN;i++)if(!FINITE(guide[i]))return 2;
    for(i=0;i<CD*CN;i++)if(!FINITE(logits[i]))return 2;
    for(y=0;y<CH;y++)for(x=0;x<CW;x++) {
        int p=y*CW+x,legal=0;
        double sum=0.0,g[3];
        for(d=0;d<CD;d++)if(y>=4 && x>=d){sum=sum+(double)logits[d*CN+p];legal++;}
        for(d=0;d<CD;d++)cost[d*CN+p]=(y>=4&&x>=d)?(double)logits[d*CN+p]-sum*inv[legal]:0.0;
        for(c=0;c<3;c++){g[c]=(double)guide[c*CN+p];gf[c*CN+p]=g[c]*(y>=4);}
        k=3;
        for(c=0;c<3;c++)for(j=c;j<3;j++)gf[(k++)*CN+p]=(g[c]*g[j])*(y>=4);
    }
    for(c=0;c<9;c++) {
        vertical(gf+c*CN,gv+c*CN,CH,CW);
        for(y=0;y<CH;y++) {
            int64_t v=0;int base=(c*CH+y)*(CW+1);
            gp[base]=0.0;
            for(x=0;x<CW;x++){
                v=q40_add(v,q40_input(gv[c*CN+y*CW+x],16384.0));
                gp[base+x+1]=(double)v*Q40_INV;q40_count[6]++;
            }
        }
    }
    for(c=0;c<4;c++)for(d=0;d<CD;d++) {
        for(i=0;i<CN;i++)tmp[i]=c?cost[d*CN+i]*(double)guide[(c-1)*CN+i]:cost[d*CN+i];
        box(tmp,cs+(c*CD+d)*CN,vertical_tmp,CH,CW);
    }
    for(y=0;y<CH;y++)for(x=0;x<CW;x++) {
        int p=y*CW+x,previous=-1,ny=0;
        double mu[3]={0,0,0},q[3]={0,0,0},weight[4]={0,0,0,0},cached_norm=0;
        for(k=-4;k<=4;k++)ny+=clip(y+k,CH)>=4;
        for(d=0;d<CD;d++) {
            int key=0,nx=0,lo=x-4,hi=x+4,left=0,right=0;
            double norm,value;
            if(y<4||x<d){out[d*CN+p]=logits[d*CN+p];color_affine_count[2]++;continue;}
            for(k=-4;k<=4;k++){int keep=clip(x+k,CW)>=d;key|=keep<<(k+4);nx+=keep;}
            norm=inv[nx*ny];
            if(key!=previous) {
                double s[9],s00,s01,s02,s11,s12,s22,c00,c01,c02,c11,c12,c22,det,r,delta[3];
                if(lo<0)lo=0;
                if(lo<d)lo=d;
                if(hi>=CW)hi=CW-1;
                if(d==0&&x<4)left=4-x;
                if(x+4>=CW)right=x+4-(CW-1);
                for(c=0;c<9;c++) {
                    int base=(c*CH+y)*(CW+1);
                    double v=lo<=hi?gp[base+hi+1]-gp[base+lo]:gp[base]-gp[base];
                    v=v+gv[c*CN+y*CW]*(double)left;
                    v=v+gv[c*CN+y*CW+CW-1]*(double)right;
                    s[c]=v*norm;
                }
                for(c=0;c<3;c++)mu[c]=s[c];
                s00=s[3]-mu[0]*mu[0]+1e-4;s01=s[4]-mu[0]*mu[1];s02=s[5]-mu[0]*mu[2];
                s11=s[6]-mu[1]*mu[1]+1e-4;s12=s[7]-mu[1]*mu[2];s22=s[8]-mu[2]*mu[2]+1e-4;
                c00=s11*s22-s12*s12;c01=s02*s12-s01*s22;c02=s01*s12-s02*s11;
                c11=s00*s22-s02*s02;c12=s01*s02-s00*s12;c22=s00*s11-s01*s01;
                det=(s00*c00+s01*c01)+s02*c02;if(!(det>0.0))return 3;r=1.0/det;
                for(c=0;c<3;c++)delta[c]=(double)guide[c*CN+p]-mu[c];
                q[0]=((c00*delta[0]+c01*delta[1])+c02*delta[2])*r;
                q[1]=((c01*delta[0]+c11*delta[1])+c12*delta[2])*r;
                q[2]=((c02*delta[0]+c12*delta[1])+c22*delta[2])*r;
                /* RGB covariance, guide pixel and exact normalization are
                 * identical while this pixel's original mask key is retained.
                 * Read all means before producing the reusable affine weights.
                 */
                weight[0]=1.0-q[0]*mu[0];
                weight[0]=weight[0]-q[1]*mu[1];
                weight[0]=weight[0]-q[2]*mu[2];
                weight[0]=weight[0]*norm;
                for(c=0;c<3;c++)weight[c+1]=q[c]*norm;
                for(c=0;c<4;c++)if(!FINITE(weight[c])){color_affine_count[3]++;return 11;}
                cached_norm=norm;color_affine_count[0]++;color_affine_count[5]+=10;
                previous=key;
            }
            if(norm!=cached_norm){color_affine_count[4]++;return 12;}
            value=weight[0]*cs[d*CN+p]+weight[1]*cs[(CD+d)*CN+p];
            value=value+weight[2]*cs[(2*CD+d)*CN+p];
            value=value+weight[3]*cs[(3*CD+d)*CN+p];
            if(!FINITE(value)){color_affine_count[3]++;return 13;}
            out[d*CN+p]=(float)value;
            color_affine_count[1]++;color_affine_count[5]+=8;
        }
    }
    return q40_count[3]?10:0;
}

/* Each coefficient satisfies |q|<=2^56. Nonnegative integer weights
 * sum to64, so every numerator is bounded by2^62 in signed64. Keep the
 * original align_corners=false coordinates and replicated edge neighbours.
 * No rounding at the horizontal stage, no saturation or FP fallback.
 */
static int64_t gray_round64(int64_t v) {
    uint64_t a=(uint64_t)(v<0?-v:v),whole=a>>6,rest=a&63u;
    if(a>gray_interp_count[3])gray_interp_count[3]=a;
    if(a>UINT64_C(4611686018427387904))gray_interp_count[4]++;
    if(rest>32u || (rest==32u && (whole&1u)))whole++;
    gray_interp_count[1]++;
    return v<0?-(int64_t)whole:(int64_t)whole;
}

API int retained_fast_gray4(const uint8_t *rgb,const float *disparity,float *out,
                           double *work,size_t bytes) {
    double *guide=work,*low=guide+PN,*pl=low+LN,*fields=pl+LN,*means=fields+4*LN;
    double *a=means+4*LN,*bb=a+LN,*coeff=bb+LN,*tmp=coeff+3*LN;
    double inv81=1.0/81.0,inv255=1.0/255.0;int y,x,dy,dx,c,i;
    int64_t cq[3*LN];
    if(!rgb||!disparity||!out||!work||bytes<(PN+16*LN)*sizeof(double))return 1;
    for(i=0;i<PN;i++) {
        if(!FINITE(disparity[i]))return 2;
        guide[i]=(double)((77*(int)rgb[i]+150*(int)rgb[PN+i]+29*(int)rgb[2*PN+i]+128)/256)*inv255;
    }
    for(y=0;y<60;y++)for(x=0;x<80;x++) {
        double g=0.0,p=0.0;int k=y*80+x;
        for(dy=0;dy<4;dy++)for(dx=0;dx<4;dx++){int j=(4*y+dy)*320+4*x+dx;g=g+guide[j];p=p+(double)disparity[j];}
        low[k]=g/16.0;pl[k]=p/16.0;
        fields[k]=pl[k];fields[LN+k]=low[k];fields[2*LN+k]=low[k]*pl[k];fields[3*LN+k]=low[k]*low[k];
    }
    for(c=0;c<4;c++)box(fields+c*LN,means+c*LN,tmp,60,80);
    for(i=0;i<LN;i++) {
        double mp=means[i]*inv81,mu=means[LN+i]*inv81;
        double rhs=means[2*LN+i]*inv81-mu*mp;
        double var=means[3*LN+i]*inv81-mu*mu,r;
        if(var<0.0)var=0.0;
        r=1.0/(var+1e-4);
        a[i]=rhs*r;bb[i]=mp-(0.0+a[i]*mu);coeff[2*LN+i]=1e-4*r;
    }
    box(a,coeff,tmp,60,80);box(bb,coeff+LN,tmp,60,80);
    for(i=0;i<2*LN;i++)coeff[i]*=inv81;
    for(i=0;i<3*LN;i++) {
        uint64_t mag;
        cq[i]=q40_input(coeff[i],65536.0);gray_interp_count[0]++;
        mag=(uint64_t)(cq[i]<0?-cq[i]:cq[i]);
        if(mag>gray_interp_count[2])gray_interp_count[2]=mag;
    }
    if(q40_count[3])return 10;
    for(y=0;y<240;y++)for(x=0;x<320;x++) {
        double sy=((double)y+0.5)/4.0-0.5,sx=((double)x+0.5)/4.0-0.5;
        double fy,fx,weight[3],proposed,value;int y0,x0,y1,x1,p=y*320+x;
        if(sy<0.0)sy=0.0;
        if(sx<0.0)sx=0.0;
        y0=(int)sy;x0=(int)sx;y1=clip(y0+1,60);x1=clip(x0+1,80);fy=sy-y0;fx=sx-x0;
        for(c=0;c<3;c++) {
            int nx=(int)(fx*8.0),ny=(int)(fy*8.0);
            int64_t upper=(8-nx)*cq[c*LN+y0*80+x0]+nx*cq[c*LN+y0*80+x1];
            int64_t lower=(8-nx)*cq[c*LN+y1*80+x0]+nx*cq[c*LN+y1*80+x1];
            int64_t numerator=(8-ny)*upper+ny*lower;
            weight[c]=(double)gray_round64(numerator)*Q40_INV;
        }
        proposed=weight[0]*guide[p]+weight[1];value=(double)disparity[p]+weight[2]*(proposed-(double)disparity[p]);
        if(value<0.0)value=0.0;
        if(value>63.99609375)value=63.99609375;
        out[p]=(float)value;
    }
    return (q40_count[3]||gray_interp_count[4])?10:0;
}
