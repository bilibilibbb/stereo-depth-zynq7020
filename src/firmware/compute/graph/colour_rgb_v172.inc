static void colour_guide_rows_v179(const float *left,float *dst,int first,int last){
 static const float mean[3]={.485f,.456f,.406f},sd[3]={.229f,.224f,.225f};int c,y,x,dy,dx,k;
 for(c=0;c<3;c++)for(y=first;y<last;y++)for(x=0;x<80;x+=4){
  r14_vf172 sum=v172_splat(0);int safe=1;
  for(dy=0;dy<4;dy++){
   const float *row=left+(c*H+y*4+dy)*W+x*4;
   for(k=0;k<16;k++)if(!(row[k]>=-16 && row[k]<=16))safe=0;
  }
  if(!safe){
   for(k=0;k<4;k++){float scalar=0;for(dy=0;dy<4;dy++)for(dx=0;dx<4;dx++){float v=left[(c*H+y*4+dy)*W+(x+k)*4+dx]*sd[c];v+=mean[c];scalar+=clamp(v,0,1);}dst[(c*64+y)*80+x+k]=scalar*.0625f;}
   continue;
  }
  {r14_vf172 scale=v172_splat(sd[c]),offset=v172_splat(mean[c]);
   for(dy=0;dy<4;dy++){
    v172_quad row=v172_row_once(left+(c*H+y*4+dy)*W+x*4);
    sum=v172_colour_acc(sum,row.c0,scale,offset);
    sum=v172_colour_acc(sum,row.c1,scale,offset);
    sum=v172_colour_acc(sum,row.c2,scale,offset);
    sum=v172_colour_acc(sum,row.c3,scale,offset);
   }
  }
  v172_store(dst+(c*64+y)*80+x,v172_mul(sum,v172_splat(.0625f)));
 }
}
static void rgb_native_rows_v179(const float *left,uint8_t *dst,int first,int last){
 static const float mean[3]={.485f,.456f,.406f},sd[3]={.229f,.224f,.225f};int c,y,x,k;
 for(c=0;c<3;c++)for(y=first;y<last;y++)for(x=0;x<W;x+=4){
  const float *src=left+(c*H+y+16)*W+x;uint8_t *out=dst+(c*240+y)*W+x;
  if(!v172_image_four(src)){for(k=0;k<4;k++){float v=src[k]*sd[c];v+=mean[c];v=clamp(v*255,0,255);out[k]=(uint8_t)quantize(v,0);}continue;}
  {r14_vf172 v=v172_mul(v172_load(src),v172_splat(sd[c]));v=v172_add(v,v172_splat(mean[c]));v=v172_mul(v,v172_splat(255));v172_rgb_store(out,v172_clamp(v,0,255));}
 }
}

static void colour_guide(const float *left,float *dst){colour_guide_rows_v179(left,dst,0,64);}

static void rgb_native(const float *left,uint8_t *dst){rgb_native_rows_v179(left,dst,0,240);}
