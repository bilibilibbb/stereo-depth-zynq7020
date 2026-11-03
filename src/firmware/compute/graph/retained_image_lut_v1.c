#include "retained_image.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
static uint8_t clamp8(int value){return (uint8_t)(value<0?0:value>255?255:value);}
static int geometry(uint32_t w,uint32_t h){return w>0 && h>0 && w<=4096 && h<=4096;}
int r14_yuyv_rgb(const uint8_t *raw,size_t bytes,uint32_t w,uint32_t h,uint32_t stride,int full,uint8_t *rgb,uint8_t *gray){
    uint32_t x,y,j;
    if(!raw || !rgb || !gray || !geometry(w,h) || (w&1) || stride<w*2 || bytes!=(size_t)stride*h || (full!=0 && full!=1))return -1;
    for(y=0;y<h;y++)for(x=0;x<w;x+=2){
        const uint8_t *p=raw+(size_t)y*stride+x*2;int u=(int)p[1]-128,v=(int)p[3]-128;
        for(j=0;j<2;j++){
            uint32_t at=y*w+x+j;int yy=p[j*2],base=full?yy*256:(yy<16?0:yy-16)*298;
            rgb[at*3]=clamp8((base+(full?359:409)*v+128)>>8);
            rgb[at*3+1]=clamp8((base-(full?88:100)*u-(full?183:208)*v+128)>>8);
            rgb[at*3+2]=clamp8((base+(full?454:516)*u+128)>>8);gray[at]=(uint8_t)yy;
        }
    }return 0;
}
int r14_rectify_rgb(const uint8_t *src,uint32_t sw,uint32_t sh,const float *map,uint32_t dw,uint32_t dh,uint8_t *dst,uint8_t *valid){
    uint32_t i,c;int ix,iy,xx,yy;
    if(!src || !map || !dst || !valid || !geometry(sw,sh) || !geometry(dw,dh) || src==dst)return -1;
    for(i=0;i<dw*dh;i++){
        float x=map[i*2],y=map[i*2+1],fx,fy;
        valid[i]=0;memset(dst+i*3,0,3);
        if(!isfinite(x) || !isfinite(y) || x<-1 || y<-1 || x>=(float)sw || y>=(float)sh)continue;
        ix=(int)floorf(x);iy=(int)floorf(y);fx=x-(float)ix;fy=y-(float)iy;
        valid[i]=(uint8_t)(ix>=0 && iy>=0 && ix+1<(int)sw && iy+1<(int)sh);
        for(c=0;c<3;c++){
            float value=0;
            for(yy=0;yy<2;yy++)for(xx=0;xx<2;xx++)if(ix+xx>=0 && iy+yy>=0 && ix+xx<(int)sw && iy+yy<(int)sh)
                value+=(float)src[((uint32_t)(iy+yy)*sw+(uint32_t)(ix+xx))*3+c]*(xx?fx:1-fx)*(yy?fy:1-fy);
            dst[i*3+c]=clamp8((int)floorf(value+.5f));
        }
    }return 0;
}
int r14_normalize_pad(const uint8_t *rgb,uint32_t w,uint32_t h,uint32_t pw,uint32_t ph,float *out){
    static const float mean[3]={.485f,.456f,.406f},std[3]={.229f,.224f,.225f};uint32_t c,x,y,top;
    if(!rgb || !out || !geometry(w,h) || !geometry(pw,ph) || pw<w || ph<h || (pw%32) || (ph%32))return -1;
    /* Only 256 possible byte values. Keep the exact original float expression
       and compute a local table each call, preserving rounding and reentrancy. */
    float normalized[3][256];
    for(c=0;c<3;c++)for(x=0;x<256;x++)
        normalized[c][x]=((float)x/255.0f-mean[c])/std[c];
    top=ph-h;
    for(c=0;c<3;c++)for(y=0;y<ph;y++)for(x=0;x<pw;x++){
        uint32_t sy=y<top?0:y-top,sx=x<w?x:w-1;
        out[(c*ph+y)*pw+x]=normalized[c][rgb[(sy*w+sx)*3+c]];
    }return 0;
}
int r14_pack_disparity(const float *p,uint32_t w,uint32_t h,uint32_t pw,uint32_t ph,const uint8_t *roi,uint16_t *q,uint8_t *valid){
    uint32_t x,y;
    if(!p || !q || !valid || !roi || !geometry(w,h) || !geometry(pw,ph) || pw<w || ph<h)return -1;
    for(y=0;y<h;y++)for(x=0;x<w;x++){
        uint32_t i=y*w+x;float d=p[(y+ph-h)*pw+x];q[i]=0;valid[i]=0;
        if(roi[i] && isfinite(d) && d>=0 && d<64){
            float scaled=d*256.0f,bottom=floorf(scaled),fraction=scaled-bottom;uint32_t v=(uint32_t)bottom;
            if(fraction>.5f || (fraction==.5f && (v&1)))v++;
            if(v<64u*256u){q[i]=(uint16_t)v;valid[i]=1;}
        }
    }return 0;
}
int r14_depth_mm(const uint16_t *q,const uint8_t *valid,uint32_t w,uint32_t h,const float mq[16],float *z){
    uint32_t i;
    if(!q || !valid || !mq || !z || !geometry(w,h))return -1;
    for(i=0;i<16;i++)if(!isfinite(mq[i]))return -1;
    for(i=0;i<w*h;i++){
        float x=(float)(i%w),y=(float)(i/w),d=(float)q[i]/256.0f;
        float num=mq[8]*x+mq[9]*y+mq[10]*d+mq[11],den=mq[12]*x+mq[13]*y+mq[14]*d+mq[15];
        z[i]=NAN;if(valid[i] && q[i] && isfinite(den) && fabsf(den)>1e-12f){float v=num/den;if(isfinite(v) && v>0)z[i]=v;}
    }return 0;
}
static void digit(uint8_t *bgr,uint32_t width,uint32_t ox,uint32_t oy,unsigned value){
    static const uint16_t font[10]={0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf};
    uint32_t x,y,sx,sy;uint16_t mask=font[value%10];
    for(y=0;y<5;y++)for(x=0;x<3;x++)if(mask&(1u<<(14-y*3-x)))for(sy=0;sy<2;sy++)for(sx=0;sx<2;sx++){
        uint8_t *p=bgr+((oy+y*2+sy)*width+ox+x*2+sx)*3;p[0]=255;p[1]=255;p[2]=255;
    }
}
int r14_render_depth(const uint8_t *rgb,const uint16_t *q,const uint8_t *valid,uint32_t w,uint32_t h,
    uint32_t frame,uint32_t elapsed,int stale,uint8_t *bgr,uint32_t dw,uint32_t dh){
    uint32_t x,y,i;
    if(!rgb || !q || !valid || !bgr || !geometry(w,h) || dw!=640 || dh!=480)return -1;
    memset(bgr,0,(size_t)dw*dh*3);
    for(y=96;y<336;y++)for(x=0;x<640;x++){
        uint32_t sx=(x%320)*w/320,sy=(y-96)*h/240,at=sy*w+sx;uint8_t *o=bgr+(y*dw+x)*3;
        if(x<320){o[0]=rgb[at*3+2];o[1]=rgb[at*3+1];o[2]=rgb[at*3];}
        else if(valid[at]){int v=(int)((uint32_t)q[at]*1020/(64*256));o[2]=clamp8(v-255);o[1]=clamp8(510-abs(v-510));o[0]=clamp8(510-v);}
        else if(((x/8)^(y/8))&1){o[0]=32;o[1]=32;o[2]=32;}
    }
    /* Top status stripe: green=fresh, amber=stale. Numeric ID and age in ms. */
    for(y=0;y<20;y++)for(x=0;x<dw;x++){uint8_t *o=bgr+(y*dw+x)*3;o[0]=0;o[1]=stale?90:70;o[2]=stale?180:0;}
    for(i=0;i<10;i++){digit(bgr,dw,8+(9-i)*8,4,frame%10);frame/=10;digit(bgr,dw,160+(9-i)*8,4,elapsed%10);elapsed/=10;}
    for(y=460;y<480;y++)for(x=0;x<dw;x++){int v=(int)(x*1020/639);uint8_t *o=bgr+(y*dw+x)*3;o[2]=clamp8(v-255);o[1]=clamp8(510-abs(v-510));o[0]=clamp8(510-v);}
    return 0;
}
static void label(uint8_t *image,uint32_t ox,uint32_t oy,const char *text){
    static const char *letters[26]={
        "010101111101101","110101110101110","011100100100011","110101101101110","111100110100111",
        "111100110100100","011100101101011","101101111101101","111010010010111","001001001101010",
        "101101110101101","100100100100111","101111111101101","101111111111101","010101101101010",
        "110101110100100","010101101111011","110101110101101","011100010001110","111010010010010",
        "101101101101111","101101101101010","101101111111101","101101010101101","101101010010010","111001010100111"};
    uint32_t x,y,sx,sy;
    for(;*text && ox+6<640;text++,ox+=8){
        if(*text>='0' && *text<='9'){digit(image,640,ox,oy,(unsigned)(*text-'0'));continue;}
        for(y=0;y<5;y++)for(x=0;x<3;x++)if((*text>='A' && *text<='Z' && letters[*text-'A'][y*3+x]=='1') || (*text=='-' && y==2))
            for(sy=0;sy<2;sy++)for(sx=0;sx<2;sx++){uint8_t *q=image+((oy+y*2+sy)*640+ox+x*2+sx)*3;q[0]=255;q[1]=255;q[2]=255;}
    }
}
int r14_render_annotation(uint8_t *image,int sgm,int quant16,const float *depth,uint32_t w,uint32_t h,int calibrated){
    float sorted[169];uint32_t n=0,i,j,x,y,cx,cy;char text[64];
    if(!image || !depth || w<16 || h<16 || w>640 || h>480)return -1;
    label(image,8,32,"FRAME");label(image,160,32,"PAIR TO DEPTH MS");label(image,8,64,"RECTIFIED LEFT");label(image,328,64,sgm?"SGM D64":quant16?"NN 11459 INT16":"NN 11459 FLOAT");
    label(image,8,440,"0 PX");label(image,568,440,"64 PX");cx=w/2;cy=h/2;
    if(calibrated)for(y=cy-6;y<=cy+6;y++)for(x=cx-6;x<=cx+6;x++)if(isfinite(depth[y*w+x]) && depth[y*w+x]>0 && depth[y*w+x]<1000000){
        float value=depth[y*w+x];j=n;while(j && sorted[j-1]>value){sorted[j]=sorted[j-1];j--;}sorted[j]=value;n++;
    }
    if(!calibrated)snprintf(text,sizeof(text),"DISTANCE UNCALIBRATED");
    else if(n<43)snprintf(text,sizeof(text),"DISTANCE INVALID");
    else snprintf(text,sizeof(text),"CENTER DISTANCE %lu MM",(unsigned long)(sorted[n/2]+.5f));
    label(image,8,370,text);label(image,8,400,"BOTTOM STATUS GREEN FRESH  AMBER STALE");
    for(i=0;i<2;i++)for(y=210;y<=222;y++)for(x=154;x<=166;x++)if(y==210 || y==222 || x==154 || x==166){uint8_t *p=image+(y*640+x+i*320)*3;p[0]=255;p[1]=255;p[2]=255;}
    return 0;
}
