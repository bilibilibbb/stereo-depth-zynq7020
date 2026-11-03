/* Frozen program lowering: every arithmetic result rounds separately.
 * No associative rewrite, fused multiply/add, or replacement covariance. */
#include "field_cpu_v274.h"
#include "r14_filter_programs.h"
#include <math.h>
#include <string.h>
static uint64_t load_bits(const void *base,unsigned i){uint64_t b;memcpy(&b,(const unsigned char*)base+8u*i,8);return b;}
static double from_bits(uint64_t bits){double v;memcpy(&v,&bits,8);return v;}

static int run_gray_fields(rf_job *j){
 uint32_t i;
 for(i=0;i<j->count;i++){
  double a0 = ((double *)j->input[0].pointer)[i];
  double a1 = ((double *)j->input[1].pointer)[i];
  double a2 = ((double *)j->input[2].pointer)[i];
  double a3 = ((double *)j->input[3].pointer)[i];
  double a4 = ((double *)j->input[4].pointer)[i];
  double a5 = ((double *)j->input[5].pointer)[i];
  double a6 = ((double *)j->input[6].pointer)[i];
  double a7 = ((double *)j->input[7].pointer)[i];
  double a8 = ((double *)j->input[8].pointer)[i];
  double a9 = ((double *)j->input[9].pointer)[i];
  double a10 = ((double *)j->input[10].pointer)[i];
  double a11 = ((double *)j->input[11].pointer)[i];
  double a12 = ((double *)j->input[12].pointer)[i];
  double a13 = ((double *)j->input[13].pointer)[i];
  double a14 = ((double *)j->input[14].pointer)[i];
  double a15 = ((double *)j->input[15].pointer)[i];
  float a16 = ((float *)j->input[16].pointer)[i];
  float a17 = ((float *)j->input[17].pointer)[i];
  float a18 = ((float *)j->input[18].pointer)[i];
  float a19 = ((float *)j->input[19].pointer)[i];
  float a20 = ((float *)j->input[20].pointer)[i];
  float a21 = ((float *)j->input[21].pointer)[i];
  float a22 = ((float *)j->input[22].pointer)[i];
  float a23 = ((float *)j->input[23].pointer)[i];
  float a24 = ((float *)j->input[24].pointer)[i];
  float a25 = ((float *)j->input[25].pointer)[i];
  float a26 = ((float *)j->input[26].pointer)[i];
  float a27 = ((float *)j->input[27].pointer)[i];
  float a28 = ((float *)j->input[28].pointer)[i];
  float a29 = ((float *)j->input[29].pointer)[i];
  float a30 = ((float *)j->input[30].pointer)[i];
  float a31 = ((float *)j->input[31].pointer)[i];
  double a32 = from_bits(UINT64_C(0x0));
  double a33 = from_bits(UINT64_C(0x4030000000000000));
  double v0 = a32 + a0;
  if (!isfinite(v0)) return RC_ARGUMENT;
  double v1 = (double)a16;
  if (!isfinite(v1)) return RC_ARGUMENT;
  double v2 = a32 + v1;
  if (!isfinite(v2)) return RC_ARGUMENT;
  double v3 = v0 + a1;
  if (!isfinite(v3)) return RC_ARGUMENT;
  double v4 = (double)a17;
  if (!isfinite(v4)) return RC_ARGUMENT;
  double v5 = v2 + v4;
  if (!isfinite(v5)) return RC_ARGUMENT;
  double v6 = v3 + a2;
  if (!isfinite(v6)) return RC_ARGUMENT;
  double v7 = (double)a18;
  if (!isfinite(v7)) return RC_ARGUMENT;
  double v8 = v5 + v7;
  if (!isfinite(v8)) return RC_ARGUMENT;
  double v9 = v6 + a3;
  if (!isfinite(v9)) return RC_ARGUMENT;
  double v10 = (double)a19;
  if (!isfinite(v10)) return RC_ARGUMENT;
  double v11 = v8 + v10;
  if (!isfinite(v11)) return RC_ARGUMENT;
  double v12 = v9 + a4;
  if (!isfinite(v12)) return RC_ARGUMENT;
  double v13 = (double)a20;
  if (!isfinite(v13)) return RC_ARGUMENT;
  double v14 = v11 + v13;
  if (!isfinite(v14)) return RC_ARGUMENT;
  double v15 = v12 + a5;
  if (!isfinite(v15)) return RC_ARGUMENT;
  double v16 = (double)a21;
  if (!isfinite(v16)) return RC_ARGUMENT;
  double v17 = v14 + v16;
  if (!isfinite(v17)) return RC_ARGUMENT;
  double v18 = v15 + a6;
  if (!isfinite(v18)) return RC_ARGUMENT;
  double v19 = (double)a22;
  if (!isfinite(v19)) return RC_ARGUMENT;
  double v20 = v17 + v19;
  if (!isfinite(v20)) return RC_ARGUMENT;
  double v21 = v18 + a7;
  if (!isfinite(v21)) return RC_ARGUMENT;
  double v22 = (double)a23;
  if (!isfinite(v22)) return RC_ARGUMENT;
  double v23 = v20 + v22;
  if (!isfinite(v23)) return RC_ARGUMENT;
  double v24 = v21 + a8;
  if (!isfinite(v24)) return RC_ARGUMENT;
  double v25 = (double)a24;
  if (!isfinite(v25)) return RC_ARGUMENT;
  double v26 = v23 + v25;
  if (!isfinite(v26)) return RC_ARGUMENT;
  double v27 = v24 + a9;
  if (!isfinite(v27)) return RC_ARGUMENT;
  double v28 = (double)a25;
  if (!isfinite(v28)) return RC_ARGUMENT;
  double v29 = v26 + v28;
  if (!isfinite(v29)) return RC_ARGUMENT;
  double v30 = v27 + a10;
  if (!isfinite(v30)) return RC_ARGUMENT;
  double v31 = (double)a26;
  if (!isfinite(v31)) return RC_ARGUMENT;
  double v32 = v29 + v31;
  if (!isfinite(v32)) return RC_ARGUMENT;
  double v33 = v30 + a11;
  if (!isfinite(v33)) return RC_ARGUMENT;
  double v34 = (double)a27;
  if (!isfinite(v34)) return RC_ARGUMENT;
  double v35 = v32 + v34;
  if (!isfinite(v35)) return RC_ARGUMENT;
  double v36 = v33 + a12;
  if (!isfinite(v36)) return RC_ARGUMENT;
  double v37 = (double)a28;
  if (!isfinite(v37)) return RC_ARGUMENT;
  double v38 = v35 + v37;
  if (!isfinite(v38)) return RC_ARGUMENT;
  double v39 = v36 + a13;
  if (!isfinite(v39)) return RC_ARGUMENT;
  double v40 = (double)a29;
  if (!isfinite(v40)) return RC_ARGUMENT;
  double v41 = v38 + v40;
  if (!isfinite(v41)) return RC_ARGUMENT;
  double v42 = v39 + a14;
  if (!isfinite(v42)) return RC_ARGUMENT;
  double v43 = (double)a30;
  if (!isfinite(v43)) return RC_ARGUMENT;
  double v44 = v41 + v43;
  if (!isfinite(v44)) return RC_ARGUMENT;
  double v45 = v42 + a15;
  if (!isfinite(v45)) return RC_ARGUMENT;
  double v46 = (double)a31;
  if (!isfinite(v46)) return RC_ARGUMENT;
  double v47 = v44 + v46;
  if (!isfinite(v47)) return RC_ARGUMENT;
  double v48 = v45 / a33;
  if (!isfinite(v48)) return RC_ARGUMENT;
  double v49 = v47 / a33;
  if (!isfinite(v49)) return RC_ARGUMENT;
  double v50 = v48 * v49;
  if (!isfinite(v50)) return RC_ARGUMENT;
  double v51 = v48 * v48;
  if (!isfinite(v51)) return RC_ARGUMENT;
  ((double *)j->output[0].pointer)[i]=v49;
  ((double *)j->output[1].pointer)[i]=v48;
  ((double *)j->output[2].pointer)[i]=v50;
  ((double *)j->output[3].pointer)[i]=v51;
 }
 return RC_OK;
}
static int run_gray_coeff(rf_job *j){
 uint32_t i;
 for(i=0;i<j->count;i++){
  double a0 = ((double *)j->input[0].pointer)[i];
  double a1 = ((double *)j->input[1].pointer)[i];
  double a2 = ((double *)j->input[2].pointer)[i];
  double a3 = ((double *)j->input[3].pointer)[i];
  double a4 = from_bits(UINT64_C(0x3f8948b0fcd6e9e0));
  double a5 = from_bits(UINT64_C(0x3f1a36e2eb1c432d));
  double a6 = from_bits(UINT64_C(0x3ff0000000000000));
  double a7 = from_bits(UINT64_C(0x0));
  double v0 = a0 * a4;
  if (!isfinite(v0)) return RC_ARGUMENT;
  double v1 = a1 * a4;
  if (!isfinite(v1)) return RC_ARGUMENT;
  double v2 = a2 * a4;
  if (!isfinite(v2)) return RC_ARGUMENT;
  double v3 = v1 * v0;
  if (!isfinite(v3)) return RC_ARGUMENT;
  double v4 = v2 - v3;
  if (!isfinite(v4)) return RC_ARGUMENT;
  double v5 = a3 * a4;
  if (!isfinite(v5)) return RC_ARGUMENT;
  double v6 = v1 * v1;
  if (!isfinite(v6)) return RC_ARGUMENT;
  double v7 = v5 - v6;
  if (!isfinite(v7)) return RC_ARGUMENT;
  double v8 = v7 < 0.0 ? 0.0 : v7;
  if (!isfinite(v8)) return RC_ARGUMENT;
  double v9 = v8 + a5;
  if (!isfinite(v9)) return RC_ARGUMENT;
  double v10 = a6 / v9;
  if (!isfinite(v10)) return RC_ARGUMENT;
  double v11 = v4 * v10;
  if (!isfinite(v11)) return RC_ARGUMENT;
  double v12 = v11 * v1;
  if (!isfinite(v12)) return RC_ARGUMENT;
  double v13 = a7 + v12;
  if (!isfinite(v13)) return RC_ARGUMENT;
  double v14 = v0 - v13;
  if (!isfinite(v14)) return RC_ARGUMENT;
  double v15 = a5 * v10;
  if (!isfinite(v15)) return RC_ARGUMENT;
  ((double *)j->output[0].pointer)[i]=v11;
  ((double *)j->output[1].pointer)[i]=v14;
  ((double *)j->output[2].pointer)[i]=v15;
 }
 return RC_OK;
}
static int run_gray_tail(rf_job *j){
 uint32_t i;
 for(i=0;i<j->count;i++){
  double a0 = ((double *)j->input[0].pointer)[i];
  float a1 = ((float *)j->input[1].pointer)[i];
  double a2 = ((double *)j->input[2].pointer)[i];
  double a3 = ((double *)j->input[3].pointer)[i];
  double a4 = ((double *)j->input[4].pointer)[i];
  double a5 = ((double *)j->input[5].pointer)[i];
  double a6 = ((double *)j->input[6].pointer)[i];
  double a7 = ((double *)j->input[7].pointer)[i];
  double a8 = ((double *)j->input[8].pointer)[i];
  double a9 = ((double *)j->input[9].pointer)[i];
  double a10 = ((double *)j->input[10].pointer)[i];
  double a11 = ((double *)j->input[11].pointer)[i];
  double a12 = ((double *)j->input[12].pointer)[i];
  double a13 = ((double *)j->input[13].pointer)[i];
  double a14 = ((double *)j->input[14].pointer)[i];
  double a15 = ((double *)j->input[15].pointer)[i];
  double a16 = ((double *)j->input[16].pointer)[i];
  double a17 = ((double *)j->input[17].pointer)[i];
  double v0 = a4 * a6;
  if (!isfinite(v0)) return RC_ARGUMENT;
  double v1 = a2 * a7;
  if (!isfinite(v1)) return RC_ARGUMENT;
  double v2 = v0 + v1;
  if (!isfinite(v2)) return RC_ARGUMENT;
  double v3 = a4 * a8;
  if (!isfinite(v3)) return RC_ARGUMENT;
  double v4 = a2 * a9;
  if (!isfinite(v4)) return RC_ARGUMENT;
  double v5 = v3 + v4;
  if (!isfinite(v5)) return RC_ARGUMENT;
  double v6 = a5 * v2;
  if (!isfinite(v6)) return RC_ARGUMENT;
  double v7 = a3 * v5;
  if (!isfinite(v7)) return RC_ARGUMENT;
  double v8 = v6 + v7;
  if (!isfinite(v8)) return RC_ARGUMENT;
  double v9 = a4 * a10;
  if (!isfinite(v9)) return RC_ARGUMENT;
  double v10 = a2 * a11;
  if (!isfinite(v10)) return RC_ARGUMENT;
  double v11 = v9 + v10;
  if (!isfinite(v11)) return RC_ARGUMENT;
  double v12 = a4 * a12;
  if (!isfinite(v12)) return RC_ARGUMENT;
  double v13 = a2 * a13;
  if (!isfinite(v13)) return RC_ARGUMENT;
  double v14 = v12 + v13;
  if (!isfinite(v14)) return RC_ARGUMENT;
  double v15 = a5 * v11;
  if (!isfinite(v15)) return RC_ARGUMENT;
  double v16 = a3 * v14;
  if (!isfinite(v16)) return RC_ARGUMENT;
  double v17 = v15 + v16;
  if (!isfinite(v17)) return RC_ARGUMENT;
  double v18 = a4 * a14;
  if (!isfinite(v18)) return RC_ARGUMENT;
  double v19 = a2 * a15;
  if (!isfinite(v19)) return RC_ARGUMENT;
  double v20 = v18 + v19;
  if (!isfinite(v20)) return RC_ARGUMENT;
  double v21 = a4 * a16;
  if (!isfinite(v21)) return RC_ARGUMENT;
  double v22 = a2 * a17;
  if (!isfinite(v22)) return RC_ARGUMENT;
  double v23 = v21 + v22;
  if (!isfinite(v23)) return RC_ARGUMENT;
  double v24 = a5 * v20;
  if (!isfinite(v24)) return RC_ARGUMENT;
  double v25 = a3 * v23;
  if (!isfinite(v25)) return RC_ARGUMENT;
  double v26 = v24 + v25;
  if (!isfinite(v26)) return RC_ARGUMENT;
  double v27 = v8 * a0;
  if (!isfinite(v27)) return RC_ARGUMENT;
  double v28 = v27 + v17;
  if (!isfinite(v28)) return RC_ARGUMENT;
  double v29 = (double)a1;
  if (!isfinite(v29)) return RC_ARGUMENT;
  double v30 = v28 - v29;
  if (!isfinite(v30)) return RC_ARGUMENT;
  double v31 = v26 * v30;
  if (!isfinite(v31)) return RC_ARGUMENT;
  double v32 = v29 + v31;
  if (!isfinite(v32)) return RC_ARGUMENT;
  double v33 = v32 < 0.0 ? 0.0 : v32;
  if (!isfinite(v33)) return RC_ARGUMENT;
  double v34 = v33 > from_bits(UINT64_C(0x404fff8000000000)) ? from_bits(UINT64_C(0x404fff8000000000)) : v33;
  if (!isfinite(v34)) return RC_ARGUMENT;
  float v35 = (float)v34;
  if (!isfinite(v35)) return RC_ARGUMENT;
  ((float *)j->output[0].pointer)[i]=v35;
 }
 return RC_OK;
}
static int run_color_covariance(rf_job *j){
 uint32_t i;
 for(i=0;i<j->count;i++){
  double a0 = ((double *)j->input[0].pointer)[i];
  double a1 = ((double *)j->input[1].pointer)[i];
  double a2 = ((double *)j->input[2].pointer)[i];
  double a3 = ((double *)j->input[3].pointer)[i];
  double a4 = ((double *)j->input[4].pointer)[i];
  double a5 = ((double *)j->input[5].pointer)[i];
  double a6 = ((double *)j->input[6].pointer)[i];
  double a7 = ((double *)j->input[7].pointer)[i];
  double a8 = ((double *)j->input[8].pointer)[i];
  double a9 = ((double *)j->input[9].pointer)[i];
  double a10 = ((double *)j->input[10].pointer)[i];
  double a11 = ((double *)j->input[11].pointer)[i];
  double a12 = from_bits(UINT64_C(0x3f1a36e2eb1c432d));
  double a13 = from_bits(UINT64_C(0x3ff0000000000000));
  double v0 = a0 * a0;
  if (!isfinite(v0)) return RC_ARGUMENT;
  double v1 = a3 - v0;
  if (!isfinite(v1)) return RC_ARGUMENT;
  double v2 = v1 + a12;
  if (!isfinite(v2)) return RC_ARGUMENT;
  double v3 = a0 * a1;
  if (!isfinite(v3)) return RC_ARGUMENT;
  double v4 = a4 - v3;
  if (!isfinite(v4)) return RC_ARGUMENT;
  double v5 = a0 * a2;
  if (!isfinite(v5)) return RC_ARGUMENT;
  double v6 = a5 - v5;
  if (!isfinite(v6)) return RC_ARGUMENT;
  double v7 = a1 * a1;
  if (!isfinite(v7)) return RC_ARGUMENT;
  double v8 = a6 - v7;
  if (!isfinite(v8)) return RC_ARGUMENT;
  double v9 = v8 + a12;
  if (!isfinite(v9)) return RC_ARGUMENT;
  double v10 = a1 * a2;
  if (!isfinite(v10)) return RC_ARGUMENT;
  double v11 = a7 - v10;
  if (!isfinite(v11)) return RC_ARGUMENT;
  double v12 = a2 * a2;
  if (!isfinite(v12)) return RC_ARGUMENT;
  double v13 = a8 - v12;
  if (!isfinite(v13)) return RC_ARGUMENT;
  double v14 = v13 + a12;
  if (!isfinite(v14)) return RC_ARGUMENT;
  double v15 = v9 * v14;
  if (!isfinite(v15)) return RC_ARGUMENT;
  double v16 = v11 * v11;
  if (!isfinite(v16)) return RC_ARGUMENT;
  double v17 = v15 - v16;
  if (!isfinite(v17)) return RC_ARGUMENT;
  double v18 = v6 * v11;
  if (!isfinite(v18)) return RC_ARGUMENT;
  double v19 = v4 * v14;
  if (!isfinite(v19)) return RC_ARGUMENT;
  double v20 = v18 - v19;
  if (!isfinite(v20)) return RC_ARGUMENT;
  double v21 = v4 * v11;
  if (!isfinite(v21)) return RC_ARGUMENT;
  double v22 = v6 * v9;
  if (!isfinite(v22)) return RC_ARGUMENT;
  double v23 = v21 - v22;
  if (!isfinite(v23)) return RC_ARGUMENT;
  double v24 = v2 * v14;
  if (!isfinite(v24)) return RC_ARGUMENT;
  double v25 = v6 * v6;
  if (!isfinite(v25)) return RC_ARGUMENT;
  double v26 = v24 - v25;
  if (!isfinite(v26)) return RC_ARGUMENT;
  double v27 = v4 * v6;
  if (!isfinite(v27)) return RC_ARGUMENT;
  double v28 = v2 * v11;
  if (!isfinite(v28)) return RC_ARGUMENT;
  double v29 = v27 - v28;
  if (!isfinite(v29)) return RC_ARGUMENT;
  double v30 = v2 * v9;
  if (!isfinite(v30)) return RC_ARGUMENT;
  double v31 = v4 * v4;
  if (!isfinite(v31)) return RC_ARGUMENT;
  double v32 = v30 - v31;
  if (!isfinite(v32)) return RC_ARGUMENT;
  double v33 = v2 * v17;
  if (!isfinite(v33)) return RC_ARGUMENT;
  double v34 = v4 * v20;
  if (!isfinite(v34)) return RC_ARGUMENT;
  double v35 = v33 + v34;
  if (!isfinite(v35)) return RC_ARGUMENT;
  double v36 = v6 * v23;
  if (!isfinite(v36)) return RC_ARGUMENT;
  double v37 = v35 + v36;
  if (!isfinite(v37)) return RC_ARGUMENT;
  if (!isfinite(v37) || signbit(v37) || v37 == 0.0) return RC_ARGUMENT;
  double v38 = v37;
  if (!isfinite(v38)) return RC_ARGUMENT;
  double v39 = a13 / v38;
  if (!isfinite(v39)) return RC_ARGUMENT;
  double v40 = a9 - a0;
  if (!isfinite(v40)) return RC_ARGUMENT;
  double v41 = a10 - a1;
  if (!isfinite(v41)) return RC_ARGUMENT;
  double v42 = a11 - a2;
  if (!isfinite(v42)) return RC_ARGUMENT;
  double v43 = v17 * v40;
  if (!isfinite(v43)) return RC_ARGUMENT;
  double v44 = v20 * v41;
  if (!isfinite(v44)) return RC_ARGUMENT;
  double v45 = v43 + v44;
  if (!isfinite(v45)) return RC_ARGUMENT;
  double v46 = v23 * v42;
  if (!isfinite(v46)) return RC_ARGUMENT;
  double v47 = v45 + v46;
  if (!isfinite(v47)) return RC_ARGUMENT;
  double v48 = v47 * v39;
  if (!isfinite(v48)) return RC_ARGUMENT;
  double v49 = v20 * v40;
  if (!isfinite(v49)) return RC_ARGUMENT;
  double v50 = v26 * v41;
  if (!isfinite(v50)) return RC_ARGUMENT;
  double v51 = v49 + v50;
  if (!isfinite(v51)) return RC_ARGUMENT;
  double v52 = v29 * v42;
  if (!isfinite(v52)) return RC_ARGUMENT;
  double v53 = v51 + v52;
  if (!isfinite(v53)) return RC_ARGUMENT;
  double v54 = v53 * v39;
  if (!isfinite(v54)) return RC_ARGUMENT;
  double v55 = v23 * v40;
  if (!isfinite(v55)) return RC_ARGUMENT;
  double v56 = v29 * v41;
  if (!isfinite(v56)) return RC_ARGUMENT;
  double v57 = v55 + v56;
  if (!isfinite(v57)) return RC_ARGUMENT;
  double v58 = v32 * v42;
  if (!isfinite(v58)) return RC_ARGUMENT;
  double v59 = v57 + v58;
  if (!isfinite(v59)) return RC_ARGUMENT;
  double v60 = v59 * v39;
  if (!isfinite(v60)) return RC_ARGUMENT;
  ((double *)j->output[0].pointer)[i]=v48;
  ((double *)j->output[1].pointer)[i]=v54;
  ((double *)j->output[2].pointer)[i]=v60;
 }
 return RC_OK;
}
static int run_color_rhs(rf_job *j){
 uint32_t i;
 for(i=0;i<j->count;i++){
  double a0 = ((double *)j->input[0].pointer)[i];
  double a1 = ((double *)j->input[1].pointer)[i];
  double a2 = ((double *)j->input[2].pointer)[i];
  double a3 = ((double *)j->input[3].pointer)[i];
  double a4 = ((double *)j->input[4].pointer)[i];
  double a5 = ((double *)j->input[5].pointer)[i];
  double a6 = ((double *)j->input[6].pointer)[i];
  double a7 = ((double *)j->input[7].pointer)[i];
  double v0 = a0 * a4;
  if (!isfinite(v0)) return RC_ARGUMENT;
  double v1 = a1 * a5;
  if (!isfinite(v1)) return RC_ARGUMENT;
  double v2 = v0 + v1;
  if (!isfinite(v2)) return RC_ARGUMENT;
  double v3 = a2 * a6;
  if (!isfinite(v3)) return RC_ARGUMENT;
  double v4 = v2 + v3;
  if (!isfinite(v4)) return RC_ARGUMENT;
  double v5 = a3 * a7;
  if (!isfinite(v5)) return RC_ARGUMENT;
  double v6 = v4 + v5;
  if (!isfinite(v6)) return RC_ARGUMENT;
  float v7 = (float)v6;
  if (!isfinite(v7)) return RC_ARGUMENT;
  ((float *)j->output[0].pointer)[i]=v7;
 }
 return RC_OK;
}
/* IEEE bits -> signed Q40, nearest ties-even, same <=2^56 domain. */
int field_q40_v274(uint64_t bits,int64_t *result){
 unsigned e=(unsigned)((bits>>52)&2047u);uint64_t m=bits&UINT64_C(0xfffffffffffff),q;
 int shift;if(!result || e==2047u)return RC_ARGUMENT;
 if(e)m|=UINT64_C(1)<<52;
 shift=e?(int)e-1035:-1034;
 if(shift>=0){if(shift>11 || m>(UINT64_C(1)<<56)>>shift)return RC_ARGUMENT;q=m<<shift;}
 else{unsigned n=(unsigned)-shift;
  if(n>=64)q=0;
  else{uint64_t r=m&((UINT64_C(1)<<n)-1u),half=UINT64_C(1)<<(n-1);q=m>>n;if(r>half || (r==half && (q&1u)))q++;}
 }
 if(q>(UINT64_C(1)<<56))return RC_ARGUMENT;
 *result=(bits>>63)?-(int64_t)q:(int64_t)q;return RC_OK;
}
static int native_color(rf_job *j){
 /* Native FIELD also owns the shared Q40 BOX four-plane reduction.
  * Input CS is raw statistics. Use clipped9x9 sums before the affine RHS.
  * Regular BOX commands are still supplied by independent captured PL. */
 const void *s=j->buffers[0].pointer;const double *w=j->buffers[1].pointer;
 int64_t q[20480],columns[320],hsum[4];float *out=j->buffers[2].pointer;
 unsigned i=0,d=j->native_address[4]&31u,y,x,k,z;int rc;
 for(z=0;z<20480;z++){rc=field_q40_v274(load_bits(s,z),q+z);if(rc)return rc;}
 for(z=0;z<320;z++){columns[z]=q[z]*5;for(k=1;k<=4;k++)columns[z]+=q[k*320u+z];}
 for(y=0;y<64;y++){
  for(k=0;k<4;k++){hsum[k]=columns[k]*5;for(x=1;x<=4;x++)hsum[k]+=columns[x*4u+k];}
  for(x=0;x<80;x++){
   if(y>=4 && x>=d){
    unsigned base=(i/128u)*128u,lanes=j->count-base,lane=i-base;
    double v,t,a[4];if(lanes>128u)lanes=128u;
    for(k=0;k<4;k++)a[k]=(double)hsum[k]*0x1p-40;
    v=a[0]*w[base*4u+lane];t=a[1]*w[base*4u+lanes+lane];v=v+t;
    t=a[2]*w[base*4u+2u*lanes+lane];v=v+t;
    t=a[3]*w[base*4u+3u*lanes+lane];v=v+t;
    if(!isfinite(v)){return RC_ARGUMENT;}
    out[i]=(float)v;
    if(!isfinite(out[i])){return RC_ARGUMENT;}
    i++;
   }
   if(x<79){unsigned old=x<4?0:x-4u,next=x+5u<80?x+5u:79u;
    for(k=0;k<4;k++)hsum[k]=hsum[k]+columns[next*4u+k]-columns[old*4u+k];
   }
  }
  if(y<63){unsigned old=y<4?0:y-4u,next=y+5u<64?y+5u:63u;
   for(z=0;z<320;z++)columns[z]=columns[z]+q[next*320u+z]-q[old*320u+z];
  }
 }
 return i==j->count?RC_OK:RC_ARGUMENT;
}
static double interpolate(const int64_t *q,unsigned x,unsigned y){
 unsigned cx=x<2?0:(x-2u)/4u,cy=y<2?0:(y-2u)/4u;
 unsigned hx=cx<79?cx+1u:79,hy=cy<59?cy+1u:59;
 unsigned nx=x<2?0:(((x-2u)&3u)*2u+1u),ny=y<2?0:(((y-2u)&3u)*2u+1u);
 int64_t u=(int64_t)(8u-nx)*q[cy*80u+cx]+(int64_t)nx*q[cy*80u+hx];
 int64_t v=(int64_t)(8u-nx)*q[hy*80u+cx]+(int64_t)nx*q[hy*80u+hx];
 int64_t n=(int64_t)(8u-ny)*u+(int64_t)ny*v;
 uint64_t m=n<0?(uint64_t)-n:(uint64_t)n,r=m>>6;
 if((m&63u)>32u || ((m&63u)==32u && (r&1u)))r++;
 return (double)(n<0?-(int64_t)r:(int64_t)r)*0x1p-40;
}
static int gray_pixel(double guide,float median,double c0,double c1,double c2,float *result){

  double g0 = c0 * guide;
  if (!isfinite(g0)) return RC_ARGUMENT;
  double g1 = g0 + c1;
  if (!isfinite(g1)) return RC_ARGUMENT;
  double g2 = (double)median;
  if (!isfinite(g2)) return RC_ARGUMENT;
  double g3 = g1 - g2;
  if (!isfinite(g3)) return RC_ARGUMENT;
  double g4 = c2 * g3;
  if (!isfinite(g4)) return RC_ARGUMENT;
  double g5 = g2 + g4;
  if (!isfinite(g5)) return RC_ARGUMENT;
  double g6 = g5 < 0.0 ? 0.0 : g5;
  if (!isfinite(g6)) return RC_ARGUMENT;
  double g7 = g6 > from_bits(UINT64_C(0x404fff8000000000)) ? from_bits(UINT64_C(0x404fff8000000000)) : g6;
  if (!isfinite(g7)) return RC_ARGUMENT;
  float g8 = (float)g7;
  if (!isfinite(g8)) return RC_ARGUMENT;
 *result=g8;return RC_OK;
}
static int native_gray(rf_job *j){
 int64_t q[14400];const double *guide=j->buffers[0].pointer;const float *median=j->buffers[1].pointer;
 const void *coeff=j->buffers[2].pointer;float *out=j->buffers[3].pointer;unsigned i;int rc;
 for(i=0;i<14400;i++){rc=field_q40_v274(load_bits(coeff,i),q+i);if(rc)return rc;}
 for(i=0;i<76800;i++){
  unsigned x=i%320u,y=i/320u;
  rc=gray_pixel(guide[i],median[i],interpolate(q,x,y),interpolate(q+4800,x,y),interpolate(q+9600,x,y),out+i);
  if(rc)return rc;
 }
 return RC_OK;
}
int field_cpu_v274(rf_job *j,unsigned *kind){
 if(!j || !kind || !j->count)return RC_ARGUMENT;
 if(j->native_mode==1){*kind=5;return native_gray(j);}
 if(j->native_mode==2){*kind=6;return native_color(j);}

 if(j->program==&r14_program_gray_fields){*kind=0;return run_gray_fields(j);}
 if(j->program==&r14_program_gray_coeff){*kind=1;return run_gray_coeff(j);}
 if(j->program==&r14_program_gray_tail){*kind=2;return run_gray_tail(j);}
 if(j->program==&r14_program_color_covariance){*kind=3;return run_color_covariance(j);}
 if(j->program==&r14_program_color_rhs){*kind=4;return run_color_rhs(j);}
 return RC_ARGUMENT;
}
