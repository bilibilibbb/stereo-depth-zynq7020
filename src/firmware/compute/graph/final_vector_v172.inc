/* Final dyadic I32 values are already the exact LUT grid indices. Avoid
 * per-pixel layout dispatch, scale conversion, grid reconstruction and
 * hit-counter stores. The original scalar path handles every other format. */
static void g_final_prediction_rows_v179(g_context *g,float *pred,int first,int last){
 const float *coarse=g_fp(g,RN_COARSE);const int32_t *q=g_i32(g,RN_FINAL);int y,x,k;
 if(!r14_final_values_v1 || g_layout[RN_FINAL].kind!=G_I32 || g_layout[RN_FINAL].fraction!=16 || g_layout[RN_FINAL].relu){g_final_prediction_scalar_rows_v179(g,pred,first,last);return;}
 for(y=first;y<last;y++)for(x=0;x<W;x+=4){
  int at=(y+16)*W+x,safe=1;float base=coarse[((y+16)/4)*80+x/4],gate[4],tanh_value[4];
  if(!(base>=0 && base<=63) || (base!=0 && base<0x1p-32f))safe=0;
  for(k=0;k<4;k++){
   float prior=g->native[at+k],valid=g->native[2*N+at+k];int a=q[at+k],b=q[N+at+k];
   if(a<R14_FINAL_GRID_MIN || a>R14_FINAL_GRID_MAX || b<R14_FINAL_GRID_MIN || b>R14_FINAL_GRID_MAX ||
      !(prior>=0 && prior<=1 && valid>=0 && valid<=1) || (prior!=0 && prior<0x1p-32f) || (valid!=0 && valid<0x1p-32f))safe=0;
  }
  if(!safe){
   for(k=0;k<4;k++){
    float fine0=g_value(g,RN_FINAL,at+k),fine1=g_value(g,RN_FINAL,N+at+k),v=g->native[2*N+at+k];
    float blend=r14_final_gate_v1(fine1)*v+(1-v),prior=g->native[at+k]*64;
    pred[y*W+x+k]=clamp(prior*(1-blend)+(base+4*r14_final_tanh_v1(fine0))*blend,0,63.99609375f);
   }
   continue;
  }
  for(k=0;k<4;k++){gate[k]=r14_final_values_v1[q[N+at+k]-R14_FINAL_GRID_MIN].gate;tanh_value[k]=r14_final_values_v1[q[at+k]-R14_FINAL_GRID_MIN].tanh_value;}
  {r14_vf172 one=v172_splat(1),valid=v172_load(g->native+2*N+at),prior=v172_mul(v172_load(g->native+at),v172_splat(64));
   r14_vf172 blend=v172_add(v172_mul(v172_load(gate),valid),v172_sub(one,valid));
   r14_vf172 value=v172_add(v172_mul(prior,v172_sub(one,blend)),v172_mul(v172_add(v172_splat(base),v172_mul(v172_splat(4),v172_load(tanh_value))),blend));
   v172_store(pred+y*W+x,v172_clamp(value,0,63.99609375f));
  }
  r14_final_hits_v1+=8;
 }
}

static void g_final_prediction(g_context *g,float *p){g_final_prediction_rows_v179(g,p,0,240);}
