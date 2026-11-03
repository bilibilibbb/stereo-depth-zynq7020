/* Preserve the CUDA four-accumulator reduction, vectorizing distinct pixels.
 * The two dyadic I16 features have no denormal products. Each multiplication
 * and addition is separate, with the same channel and reduction order. */
static void g_supply_values_v186(g_context *g,int tensor,float *out,unsigned count){
 const g_tensor_layout *l=g_layout+tensor;unsigned i=0;
 if(tensor==0 || tensor==1 || l->kind==G_F32){const float *p=tensor==0?g->left:(tensor==1?g->right:g_fp(g,tensor));memcpy(out,p,count*4u);return;}
#if !defined(_MSC_VER) && !defined(R14_HOST_REFERENCE)
 if(l->kind==G_I16){r14_dequantize_neon_v1(g_i16(g,tensor),out,count,l->fraction,l->relu);return;}
 if(l->kind==G_I32){
  const int32_t *p=g_i32(g,tensor);float32x4_t scale=vdupq_n_f32(r14_inverse_scale_v169(l->fraction));
  for(;i+4<=count;i+=4){int32x4_t q=vld1q_s32(p+i);if(l->relu)q=vminq_s32(vmaxq_s32(q,vdupq_n_s32(0)),vdupq_n_s32(6*(1<<l->fraction)));vst1q_f32(out+i,vmulq_f32(vcvtq_f32_s32(q),scale));}
 }
#endif
 for(;i<count;i++)out[i]=g_value(g,tensor,(int)i);
}
static float g_supply_correlation_pixel_v186(const float *left,const float *right,int d,int y,int x){
 int c,shift=d==16?15:d;float sum,partial[4]={0,0,0,0};
 for(c=0;c<8;c++){float r=x>=shift?right[(c*64+y)*80+x-shift]:0;if(d==16){float r1=x>=16?right[(c*64+y)*80+x-16]:0;r=r*.25f+r1*.75f;}partial[c&3]+=left[(c*64+y)*80+x]*r;}
 sum=partial[0]+partial[1];sum+=partial[2];sum+=partial[3];return sum*.125f;
}
static void g_supply_correlation_rows_v205(const float *left,const float *right,float *out,unsigned first,unsigned last){
 int d,y,x,c;unsigned row;
 for(row=first;row<last;row++){d=(int)(row/64u);y=(int)(row%64u);
  int start=((d==16?16:d)+3)&~3;x=0;
  for(;x<start;x++)out[(d*64+y)*80+x]=g_supply_correlation_pixel_v186(left,right,d,y,x);
#if !defined(_MSC_VER) && !defined(R14_HOST_REFERENCE)
  for(;x+4<=80;x+=4){
   float32x4_t part[4]={vdupq_n_f32(0),vdupq_n_f32(0),vdupq_n_f32(0),vdupq_n_f32(0)},sum;
   for(c=0;c<8;c++){
    float32x4_t l=vld1q_f32(left+(c*64+y)*80+x),r=vld1q_f32(right+(c*64+y)*80+x-(d==16?15:d));
    if(d==16){float32x4_t r1=vld1q_f32(right+(c*64+y)*80+x-16);r=vaddq_f32(vmulq_n_f32(r,.25f),vmulq_n_f32(r1,.75f));}
    part[c&3]=vaddq_f32(part[c&3],vmulq_f32(l,r));
   }
   sum=vaddq_f32(part[0],part[1]);sum=vaddq_f32(sum,part[2]);sum=vaddq_f32(sum,part[3]);vst1q_f32(out+(d*64+y)*80+x,vmulq_n_f32(sum,.125f));
  }
#else
  for(;x+4<=80;x+=4){
   float part[4][4]={{0}},sum;int lane;
   for(c=0;c<8;c++)for(lane=0;lane<4;lane++){float l=left[(c*64+y)*80+x+lane],r=right[(c*64+y)*80+x+lane-(d==16?15:d)];if(d==16){float r1=right[(c*64+y)*80+x+lane-16];r=r*.25f+r1*.75f;}part[c&3][lane]+=l*r;}
   for(lane=0;lane<4;lane++){sum=part[0][lane]+part[1][lane];sum+=part[2][lane];sum+=part[3][lane];out[(d*64+y)*80+x+lane]=sum*.125f;}
  }
#endif
  for(;x<80;x++)out[(d*64+y)*80+x]=g_supply_correlation_pixel_v186(left,right,d,y,x);
 }
}
static void g_supply_correlation_v186(const float *left,const float *right,float *out){
 g_supply_correlation_rows_v205(left,right,out,0,17u*64u);
}
static int g_supply_produce_v186(g_context *g,const rn_node *n,void *output,float *fl,float *fr){
 int k,offset=0;
 if(n->op==RN_OP_CONV){const rn_tensor_info *a=rn_tensors+n->input[0];g_input(g,n->input[0],(int16_t *)output,a->c*a->h*a->w,rn_parameters[n->parameter].input_frac);return RN_OK;}
 if(n->op==RN_OP_CORRELATION){g_supply_values_v186(g,n->input[0],fl,8u*CN);g_supply_values_v186(g,n->input[1],fr,8u*CN);g_supply_correlation_v186(fl,fr,(float *)output);return RN_OK;}
 if(n->op==RN_OP_CONCAT){for(k=0;k<n->inputs;k++){int t=n->input[k];const rn_tensor_info *a=rn_tensors+t;int count=a->c*a->h*a->w;g_supply_values_v186(g,t,(float *)output+offset,(unsigned)count);offset+=count;}return RN_OK;}
 if(n->op==RN_OP_DECODE){g_supply_values_v186(g,n->input[0],(float *)output,17u*CN);return RN_OK;}
 return RN_ARGUMENT;
}
