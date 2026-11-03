/* Derived from frozen reference/v351a/retained_sgm_driver.c SHA256 98e63db5dbcf534b6f68399851141261ced6567e83d742bbaa77b0389719fe27.
 * Original start/cancel/recovery retained; add authenticated prefixes and
 * acquire only the unread suffix on successful whole-command completion. */
#include "fw_sgm_driver.h"
#include <string.h>
#include <limits.h>
#include "r14_profile.h"

static int rd(rs_device *d,uint32_t a,uint32_t *v){return d->io.read32(d->io.user,d->base+a,v)?RN_IO_ERROR:RN_OK;}
static int wr(rs_device *d,uint32_t a,uint32_t v){return d->io.write32(d->io.user,d->base+a,v)?RN_IO_ERROR:RN_OK;}
static int eq(rs_device *d,uint32_t a,uint32_t want){uint32_t v;if(rd(d,a,&v))return RN_IO_ERROR;return v==want?RN_OK:RN_STALE;}
static int ready(uint32_t s){return (s&(RN_BUSY|RN_QUIET|RN_READY|RN_NEEDS_RESET))==(RN_QUIET|RN_READY) && !(s>>16);}
static int span(uint32_t a,uint32_t n){return !(a&31u) && a>=0x19000000u && (uint64_t)a+n<=0x1a000000u;}
static int frame_valid(const rs_frame *f){
    uint32_t i,j,a[4],n[4]={RS_CENSUS_BYTES,RS_CENSUS_BYTES,RS_CURVE_BYTES,RS_NATIVE_BYTES};
    if(!f || !f->left || !f->right || !f->curve || !f->native || !f->timeout_cycles)return 0;
    if(((uintptr_t)f->left|(uintptr_t)f->right|(uintptr_t)f->curve|(uintptr_t)f->native)&31u)return 0;
    a[0]=f->left_address;a[1]=f->right_address;a[2]=f->curve_address;a[3]=f->native_address;
    for(i=0;i<4;i++){
        if(!span(a[i],n[i]))return 0;
        for(j=0;j<i;j++)if((uint64_t)a[i]+n[i]>a[j] && (uint64_t)a[j]+n[j]>a[i])return 0;
    }
    return 1;
}
static int fail(rs_device *d,int error){
    uint32_t s;d->ownership=2;
    if(!d->first_error)d->first_error=(uint32_t)error;
    memset(d->cached_valid,0,sizeof(d->cached_valid));
    if(!rd(d,RN_STATUS,&s)){d->last_status=s;if(s&RN_BUSY){R14P_ADD(R14P_SGM_CANCEL_ATTEMPTS,1);(void)wr(d,RN_CONTROL,2);}}
    R14P_ABORT(R14P_SGM_CACHE_PREP,R14P_SGM_INVALIDATE,error);
    return error;
}
static void release(rs_device *d,unsigned decoded_rows){
    uint32_t curve_first=decoded_rows*5760u,native_first=decoded_rows>4u?(decoded_rows-4u)*10240u:0u;
    R14P_BEGIN(R14P_SGM_INVALIDATE);
    if(d->owned_curve && curve_first<RS_CURVE_BYTES)d->io.invalidate(d->io.user,(uint8_t*)d->owned_curve+curve_first,RS_CURVE_BYTES-curve_first);
    if(d->owned_native && native_first<RS_NATIVE_BYTES)d->io.invalidate(d->io.user,(uint8_t*)d->owned_native+native_first,RS_NATIVE_BYTES-native_first);
    if(d->owned_curve)R14P_ADD(R14P_SGM_INVALIDATE_BYTES,RS_CURVE_BYTES-curve_first);
    if(d->owned_native)R14P_ADD(R14P_SGM_INVALIDATE_BYTES,RS_NATIVE_BYTES-native_first);
    d->io.barrier(d->io.user);d->ownership=0;d->owned_curve=0;d->owned_native=0;
    R14P_END(R14P_SGM_INVALIDATE,0);
}
int rs_open(rs_device *d){
    uint32_t s,tag;
    if(!d || !d->io.read32 || !d->io.write32 || !d->io.flush || !d->io.invalidate || !d->io.barrier || !d->io.now_us ||
       !d->expected_build || !d->timeout_us || d->timeout_us>10000000u)return RN_ARGUMENT;
    if(d->ownership)return RN_BUSY_ERROR;
    d->opened=0;
    if(eq(d,0,RS_MAGIC) || eq(d,4,RS_ABI) || eq(d,8,d->expected_build) || eq(d,12,RN_WEIGHT_ID) || eq(d,0x3c,RN_GRAPH_ID))return RN_IDENTITY;
    if(rd(d,RN_STATUS,&s) || rd(d,RN_ACCEPTED_TAG,&tag))return RN_IO_ERROR;
    if(!ready(s))return RN_BUSY_ERROR;
    d->next_tag=tag;d->first_error=0;d->last_status=s;d->completed=0;d->owned_curve=0;d->owned_native=0;
    memset(d->cached_valid,0,sizeof(d->cached_valid));d->opened=1;return RN_OK;
}
int rs_start(rs_device *d,const rs_frame *f){
    uint32_t s,i,w[6];
    if(!d || !d->opened || !frame_valid(f))return RN_ARGUMENT;
    if(d->ownership)return RN_BUSY_ERROR;
    if(d->next_tag==UINT32_MAX)return RN_IDENTITY;
    if(rd(d,RN_STATUS,&s))return fail(d,RN_IO_ERROR);
    if(!ready(s))return fail(d,RN_HARDWARE);
    w[0]=f->left_address;w[1]=f->right_address;w[2]=f->curve_address;w[3]=f->native_address;
    w[4]=++d->next_tag;w[5]=f->timeout_cycles;
    d->owned_curve=f->curve;d->owned_native=f->native;
    R14P_BEGIN(R14P_SGM_CACHE_PREP);
    d->io.flush(d->io.user,f->left,RS_CENSUS_BYTES);d->io.flush(d->io.user,f->right,RS_CENSUS_BYTES);
    d->io.flush(d->io.user,f->curve,RS_CURVE_BYTES);d->io.flush(d->io.user,f->native,RS_NATIVE_BYTES);d->io.barrier(d->io.user);
    R14P_ADD(R14P_SGM_FLUSH_BYTES,2u*RS_CENSUS_BYTES+RS_CURVE_BYTES+RS_NATIVE_BYTES);R14P_END(R14P_SGM_CACHE_PREP,0);
    R14P_BEGIN(R14P_SGM_DESCRIPTOR_WRITE);
    for(i=0;i<6;i++)if(!d->cached_valid[i] || d->cached[i]!=w[i]){
        if(wr(d,0x100u+4u*i,w[i]))return fail(d,RN_IO_ERROR);
        d->cached[i]=w[i];d->cached_valid[i]=1;
    }
    d->io.barrier(d->io.user);d->ownership=1;d->polls=0;d->started_us=d->io.now_us(d->io.user);
    R14P_END(R14P_SGM_DESCRIPTOR_WRITE,0);R14P_BEGIN(R14P_SGM_OWNED_INTERVAL);
    if(wr(d,RN_CONTROL,1))return fail(d,RN_IO_ERROR);
    return RN_OK;
}
static int poll_prefix(rs_device *d,unsigned decoded_rows){
    uint32_t s;
    if(!d || !d->opened || d->ownership!=1 || decoded_rows>64u)return RN_ARGUMENT;
    if(rd(d,RN_STATUS,&s))return fail(d,RN_IO_ERROR);
    d->last_status=s;
    if(s&RN_NEEDS_RESET)return fail(d,RN_RESET_REQUIRED);
    if(s>>16)return fail(d,RN_HARDWARE);
    if(s&RN_CANCELLED)return fail(d,RS_CANCELLED);
    if(!(s&RN_DONE)){
        if(d->io.now_us(d->io.user)-d->started_us>=d->timeout_us || ++d->polls>=1000000u)return fail(d,RN_TIMEOUT);
        return RS_PENDING;
    }
    if(!ready(s) || eq(d,RN_ACCEPTED_TAG,d->next_tag) || eq(d,RN_COMPLETED_TAG,d->next_tag) ||
       eq(d,RN_VALUES,76800u) || eq(d,0x40,5120u) || eq(d,0x48,76800u) ||
       eq(d,RN_READ_BEATS,76800u) || eq(d,0x44,122880u))return fail(d,RN_STALE);
    if(rd(d,RN_CYCLES,&d->cycles) || rd(d,RN_READ_BEATS,&d->read_beats) || rd(d,0x44,&d->write_beats))return fail(d,RN_IO_ERROR);
    R14P_END(R14P_SGM_OWNED_INTERVAL,0);
    R14P_ADD(R14P_SGM_COMPLETED,1);R14P_ADD(R14P_SGM_CYCLES,d->cycles);R14P_ADD(R14P_SGM_READ_BEATS,d->read_beats);R14P_ADD(R14P_SGM_WRITE_BEATS,d->write_beats);
    release(d,decoded_rows);d->completed++;return RN_OK;
}
int rs_poll(rs_device *d){return poll_prefix(d,0u);}
int fw_sgm_poll_prefix(rs_device *d,unsigned decoded_rows){return poll_prefix(d,decoded_rows);}
int fw_sgm_prefix_ready(rs_device *d,unsigned prior,unsigned *rows){
    uint32_t tag,native,curve,s;unsigned n,q;
    if(!d || !d->opened || d->ownership!=1 || prior>64u || !rows)return RN_ARGUMENT;
    if(eq(d,RN_ACCEPTED_TAG,d->next_tag))return fail(d,RN_STALE);
    if(rd(d,FW_SGM_PREFIX_TAG,&tag))return fail(d,RN_IO_ERROR);
    /* START may be accepted by MMIO one cycle before the engine captures
     * its descriptor. Never combine that new tag with previous counts. */
    if(tag!=d->next_tag)return RS_PENDING;
    if(rd(d,FW_SGM_NATIVE_COMMITTED,&native) || rd(d,FW_SGM_CURVE_COMMITTED,&curve) || rd(d,RN_STATUS,&s))return fail(d,RN_IO_ERROR);
    if(s&RN_NEEDS_RESET)return fail(d,RN_RESET_REQUIRED);
    if(s>>16)return fail(d,RN_HARDWARE);
    if(s&RN_CANCELLED)return fail(d,RS_CANCELLED);
    if(native>76800u || curve>5120u)return fail(d,RN_STALE);
    n=native/1280u+4u;q=curve/80u;if(q>n)q=n;
    if(q<prior)return fail(d,RN_STALE);
    *rows=q;return RN_OK;
}
int fw_sgm_prefix_acquire(rs_device *d,unsigned row){
    if(!d || d->ownership!=1 || row>=64u || !d->owned_curve || !d->owned_native)return RN_ARGUMENT;
    R14P_BEGIN(R14P_SGM_INVALIDATE);
    if(row>=4u)d->io.invalidate(d->io.user,(uint8_t*)d->owned_native+(row-4u)*10240u,10240u);
    d->io.invalidate(d->io.user,(uint8_t*)d->owned_curve+row*5760u,5760u);d->io.barrier(d->io.user);
    R14P_ADD(R14P_SGM_INVALIDATE_BYTES,5760u+(row>=4u?10240u:0u));R14P_END(R14P_SGM_INVALIDATE,0);return RN_OK;
}
int rs_submit(rs_device *d,const rs_frame *f){int e=rs_start(d,f);if(e)return e;do{e=rs_poll(d);}while(e==RS_PENDING);return e;}
int rs_cancel(rs_device *d){if(!d || !d->opened || d->ownership!=1)return RN_ARGUMENT;return fail(d,RS_CANCELLED);}
static int profiled_sgm_recover(rs_device *d){
    uint64_t start;uint32_t s,i=0;
    if(!d || !d->opened || d->ownership!=2)return RN_ARGUMENT;
    start=d->io.now_us(d->io.user);
    for(;;){
        if(rd(d,RN_STATUS,&s))return RN_IO_ERROR;
        d->last_status=s;if(s&RN_NEEDS_RESET)return RN_RESET_REQUIRED;
        if(!(s&RN_BUSY) && (s&RN_QUIET))break;
        if(d->io.now_us(d->io.user)-start>=d->timeout_us || ++i>=1000000u)return RN_TIMEOUT;
    }
    if(wr(d,RN_CONTROL,4))return RN_IO_ERROR;
    do{
        if(rd(d,RN_STATUS,&s))return RN_IO_ERROR;
        if(s&RN_NEEDS_RESET)return RN_RESET_REQUIRED;
        if(d->io.now_us(d->io.user)-start>=d->timeout_us || ++i>=1000000u)return RN_TIMEOUT;
    }while(!ready(s));
    release(d,0u);memset(d->cached_valid,0,sizeof(d->cached_valid));return RN_OK;
}
int rs_recover(rs_device *d){int rc;R14P_ADD(R14P_SGM_RECOVER_ATTEMPTS,1);rc=profiled_sgm_recover(d);if(rc)R14P_ADD(R14P_SGM_RECOVER_FAILED,1);return rc;}
