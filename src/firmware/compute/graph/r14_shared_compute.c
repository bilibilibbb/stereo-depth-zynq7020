#include "r14_shared_compute.h"
#include <string.h>
#include <limits.h>
#include "nn_validation.inc"

enum {BUSY=1,QUIET=2,READY=4,DONE=8,CANCELLED=16,RESET=32,
      CLEAR_PENDING=64,COMMAND_PENDING=128,CONFIG_PENDING=256};
static const uint32_t magic[4]={0x524e3134u,0x52493134u,0x52463134u,0x52423134u};
static const unsigned words[4]={81,10,4,7},tag_word[4]={7,9,2,1};
static int rd(rc_device *d,unsigned p,uint32_t r,uint32_t *v){return d->io.read32(d->io.user,d->base+p*1024u+r,v)?RC_IO_ERROR:RC_OK;}
static int wr(rc_device *d,unsigned p,uint32_t r,uint32_t v){return d->io.write32(d->io.user,d->base+p*1024u+r,v)?RC_IO_ERROR:RC_OK;}
static int eq(rc_device *d,unsigned p,uint32_t r,uint32_t want){uint32_t v;int e=rd(d,p,r,&v);return e?e:v==want?RC_OK:RC_IDENTITY;}
static int identity(rc_device *d,unsigned p){
    int e;
    if((e=eq(d,p,0,magic[p])) || (e=eq(d,p,4,p==RC_NN?0x10003u:p==RC_FIELD?0x10004u:p==RC_BOX?0x10002u:0x10002u)) ||
       (e=eq(d,p,8,d->expected_build)) || (e=eq(d,p,12,0xed2076d1u)) ||
       (e=eq(d,p,0x3c,0xd14cebbdu)) || (e=eq(d,p,0x40,0x43503433u)))return e;
    return RC_OK;
}
static int span(const rc_buffer *b){
    return b->pointer && b->bytes && b->write<=1 && !(b->address&7u) &&
        b->address>=0x19000000u && (uint64_t)b->address+b->bytes<=0x1a000000u &&
        (uintptr_t)b->pointer<=UINTPTR_MAX-b->bytes-63u;
}
static int interval(uint64_t a,uint32_t na,uint64_t b,uint32_t nb){
    uint64_t ae=(a+na+63u)&~UINT64_C(63),be=(b+nb+63u)&~UINT64_C(63);
    a&=~UINT64_C(63);b&=~UINT64_C(63);return a<be && b<ae;
}
static int conflict(const rc_buffer *a,const rc_buffer *b){
    return (a->write || b->write) &&
        (interval(a->address,a->bytes,b->address,b->bytes) ||
         interval((uintptr_t)a->pointer,a->bytes,(uintptr_t)b->pointer,b->bytes));
}
static int fail(rc_device *d,unsigned p,int error){
    rc_engine *e=&d->engine[p];uint32_t s;
    e->state=RC_QUARANTINE;if(!e->first_error)e->first_error=(uint32_t)error;
    memset(e->descriptor_valid,0,sizeof(e->descriptor_valid));
    if(!rd(d,p,0x14,&s) && (s&(BUSY|COMMAND_PENDING|CONFIG_PENDING)))(void)wr(d,p,0x10,2);
    return error;
}
static void release(rc_device *d,unsigned p,unsigned exact_success){
    rc_engine *e=&d->engine[p];unsigned i;
    for(i=0;i<e->buffer_count;i++)if(e->buffers[i].write){
        if(p==RC_NN && e->output_cache_bytes && exact_success)
            d->io.invalidate(d->io.user,e->output_cache_pointer,e->output_cache_bytes);
        else {
            /* Aligned narrow preparation left untouched channel lines dirty.
             * Preserve them before full error invalidation. The written lines
             * were invalidated before submit, and no CPU writer is permitted
             * while the complete buffer lease is held. Only invoke this after
             * the hardware has actually drained and CLEAR has completed. */
            if(p==RC_NN && e->output_cache_bytes)
                d->io.flush(d->io.user,e->buffers[i].pointer,e->buffers[i].bytes);
            d->io.invalidate(d->io.user,e->buffers[i].pointer,e->buffers[i].bytes);
        }
    }
    d->io.barrier(d->io.user);e->buffer_count=0;e->output_cache_pointer=0;e->output_cache_bytes=0;e->state=RC_IDLE;
}
static int elapsed(rc_device *d,rc_engine *e){return d->io.now_us(d->io.user)-e->started_us>=d->timeout_us;}
static int buffers_match(const rc_buffer *b,unsigned n,unsigned i,uint32_t addr,uint32_t bytes,unsigned write){
    return i<n && span(&b[i]) && b[i].address==addr && b[i].bytes==bytes && b[i].write==write;
}
static int validate(rc_device *d,unsigned p,const uint32_t *w,const rc_buffer *b,unsigned n,uint32_t *values){
    uint64_t count;uint32_t width,height,channels,op,a_bytes,d_bytes,i,j=0;
    if(p==RC_NN){
        rn_descriptor descriptor;memcpy(descriptor.word,w,sizeof(descriptor.word));
        if(!valid_descriptor(&descriptor) || !w[8])return 0;
        *values=w[12]*w[13]*w[14];
        if(!buffers_match(b,n,0,w[1],w[2],0))return 0;
        if(!w[0]){if(!buffers_match(b,n,1,w[5],w[6],0))return 0;j=1;}
        return n==j+2 && buffers_match(b,n,j+1,w[3],w[4],1);
    }
    if(p==RC_INTEGER && w[0]==3u){
        *values=148480u;
        return n==4 && w[5]==81920u && w[6]==81920u && w[7]==296960u && w[8] &&
            buffers_match(b,n,0,w[2],81920u,0) && buffers_match(b,n,1,w[3],81920u,0) &&
            buffers_match(b,n,2,w[1],81920u,0) && buffers_match(b,n,3,w[4],296960u,1);
    }
    if(p==RC_INTEGER){
        op=w[0]&3u;width=w[1]&1023u;height=(w[1]>>10)&1023u;channels=(w[1]>>20)&255u;
        if((w[0]&~0x7fff1f03u) || (w[1]&0xf0000000u) || (op!=1 && op!=2) || width<4 || width>320 ||
           (width&3u) || !height || height>256 || !channels || channels>32 || !(w[0]&1024u) || !w[8] ||
           ((w[0]>>16)&31u)<9 || ((w[0]>>16)&31u)>16 || ((w[0]>>26)&31u)<11 || ((w[0]>>26)&31u)>14)return 0;
        if(op==1 && (((w[0]>>21)&31u)<9 || ((w[0]>>21)&31u)>16))return 0;
        if(op==2 && (width>160 || height>128 || ((w[0]>>16)&31u)>12))return 0;
        count=(uint64_t)width*height*channels;*values=(uint32_t)(count*(op==2?4u:1u));
        a_bytes=(uint32_t)count*((w[0]&256u)?4u:2u);d_bytes=*values*4u;
        if(w[5]!=a_bytes || w[7]!=d_bytes || !buffers_match(b,n,0,w[2],w[5],0))return 0;
        if(op==1){if(w[6]!=(uint32_t)count*((w[0]&512u)?4u:2u) || !buffers_match(b,n,1,w[3],w[6],0))return 0;j=1;}
        else if(w[3] || w[6])return 0;
        return n==j+2 && buffers_match(b,n,j+1,w[4],w[7],1);
    }
    if(p==RC_BOX){
        op=w[0]&3u;width=(w[0]>>8)&127u;height=(w[0]>>16)&127u;
        if((w[0]&~0x007f7f03u) || !width || width>80 || !height || height>64 || !w[6])return 0;
        *values=height*(width+(op==3));
        return n==2 && w[4]==width*height*8u && w[5]==*values*8u &&
            buffers_match(b,n,0,w[2],w[4],0) && buffers_match(b,n,1,w[3],w[5],1);
    }
    count=w[0];width=w[1]&63u;height=(w[1]>>8)&15u;channels=(w[1]>>16)&511u;
    if(!channels){
        uint32_t flag=d->native_config[4],resident=flag>>31;
        uint32_t count_expected=resident?60u*(80u-(flag&31u)):76800u;
        uint32_t sizes[4]={614400u,307200u,115200u,307200u};
        if(d->field_mode!=1 || count!=count_expected || width!=(resident?8u:18u) || height!=1u ||
           (w[1]&~0x01ff0f3fu) || !w[3] || n!=(resident?3u:4u))return 0;
        for(i=0;i<5;i++)if(!d->native_valid[i])return 0;
        if(resident){
            if((flag&0x7fffffe0u) || (flag&31u)>16u || d->native_config[3])return 0;
            sizes[0]=163840u;sizes[1]=(uint32_t)count*32u;sizes[2]=(uint32_t)count*4u;
        }else if(!flag)return 0;
        for(i=0;i<n;i++)if(!buffers_match(b,n,i,d->native_config[i],sizes[i],i==n-1u) ||
            b[i].pointer!=d->native_buffer[i].pointer || d->native_buffer[i].bytes!=sizes[i])return 0;
        *values=count_expected;return 1;
    }
    if(d->field_mode!=0)return 0;
    if(!count || count>81920 || (w[1]&~0x01ff0f3fu) || !width || width>40 || !height || height>8 || !channels || channels>64 || !w[3])return 0;
    for(i=0;i<channels;i++)if(!d->program_valid[i])return 0;
    for(i=0;i<width;i++){
        rc_buffer *m=&d->input_buffer[i];
        if(!d->input_valid[i])return 0;
        if(!d->input_config[i].scalar){
            uint32_t bytes=(uint32_t)count*(d->input_config[i].wide?8u:4u);
            if(m->bytes<bytes || !buffers_match(b,n,j,m->address,bytes,0) || b[j].pointer!=m->pointer)return 0;
            j++;
        }
    }
    for(i=0;i<height;i++){
        rc_buffer *m=&d->output_buffer[i];uint32_t bytes=(uint32_t)count*(d->output_config[i].wide?8u:4u);
        if(!d->output_valid[i] || m->bytes<bytes || !buffers_match(b,n,j,m->address,bytes,1) || b[j].pointer!=m->pointer)return 0;
        j++;
    }
    *values=(uint32_t)count*height;return n==j;
}
int rc_open(rc_device *d){
    unsigned p;uint32_t s;int e;
    if(!d || !d->io.read32 || !d->io.write32 || !d->io.flush || !d->io.invalidate || !d->io.barrier || !d->io.now_us ||
       !d->expected_build || !d->timeout_us || d->timeout_us>10000000u || (d->base&4095u))return RC_ARGUMENT;
    for(p=0;p<4;p++)if(d->engine[p].state!=RC_IDLE)return RC_AGAIN;
    d->opened=0;
    for(p=0;p<4;p++){
        if((e=identity(d,p)))return e;
        if((e=rd(d,p,0x14,&s)))return e;
        if((s&(BUSY|QUIET|READY|RESET|CLEAR_PENDING|COMMAND_PENDING|CONFIG_PENDING))!=(QUIET|READY) || (s>>16))return RC_AGAIN;
    }
    memset(d->engine,0,sizeof(d->engine));memset(d->input_valid,0,sizeof(d->input_valid));
    memset(d->output_valid,0,sizeof(d->output_valid));memset(d->program_valid,0,sizeof(d->program_valid));
    d->field_mode=0;memset(d->native_valid,0,sizeof(d->native_valid));
    for(p=0;p<4;p++)if((e=rd(d,p,0x18,&d->engine[p].next_tag)))return e;
    d->opened=1;return RC_OK;
}
int rc_begin(rc_device *d,unsigned p,const uint32_t *w,unsigned word_count,const rc_buffer *b,unsigned n){
    rc_engine *e;unsigned i,k,j;uint32_t s,v,tag;int error;
    if(!d || !d->opened || p>3 || !w || !b || word_count!=words[p] || n>RC_MAX_BUFFERS)return RC_ARGUMENT;
    e=&d->engine[p];if(e->state!=RC_IDLE)return RC_AGAIN;
    if(!validate(d,p,w,b,n,&v))return RC_ARGUMENT;
    for(i=0;i<n;i++){
        for(j=0;j<i;j++)if(conflict(&b[i],&b[j]))return RC_LEASE_CONFLICT;
        for(k=0;k<4;k++)if(d->engine[k].state!=RC_IDLE)
            for(j=0;j<d->engine[k].buffer_count;j++)if(conflict(&b[i],&d->engine[k].buffers[j]))return RC_LEASE_CONFLICT;
    }
    if((error=identity(d,p)) || (error=rd(d,p,0x14,&s)))return error;
    if(s&RESET)return RC_RESET_REQUIRED;
    if(s>>16)return RC_HARDWARE;
    if((s&(BUSY|QUIET|READY|CLEAR_PENDING|COMMAND_PENDING|CONFIG_PENDING))!=(QUIET|READY))return RC_AGAIN;
    if((error=rd(d,p,0x30,&e->accepted_before)) || (error=rd(d,p,0x34,&e->completed_before)))return error;
    tag=(e->next_tag+1u)&(p<2?UINT32_MAX:65535u);if(!tag)tag=1;
    e->next_tag=tag;e->pending_tag=tag;e->expected_values=v;e->buffer_count=n;e->first_error=0;
    memcpy(e->buffers,b,n*sizeof(*b));e->state=RC_JOB;
    /* Keep the entire physical/host output lease. Dense hardware writes a
     * contiguous natural-CHW channel interval; no cache line may straddle its
     * boundary. Depthwise or unaligned/partial-line cases use the old range. */
    e->output_cache_pointer=0;e->output_cache_bytes=0;
    if(p==RC_NN && !w[RN_D_CMD_DEPTHWISE]){
        uint32_t page=w[RN_D_OUTPUT_WIDTH]*w[RN_D_OUTPUT_HEIGHT]*2u;
        uint32_t offset=w[RN_D_CHANNEL_START]*page;
        uint32_t footprint=w[RN_D_OUTPUT_CHANNELS]*page;
        uintptr_t point=(uintptr_t)b[n-1u].pointer+offset;
        uint32_t address=b[n-1u].address+offset;
        if(!(point&31u) && !(address&31u) && !(footprint&31u) && footprint &&
           offset<=b[n-1u].bytes && footprint<=b[n-1u].bytes-offset){
            e->output_cache_pointer=(void*)point;e->output_cache_bytes=footprint;
        }
    }
    for(i=0;i<n;i++){
        if(p==RC_NN && b[i].write && e->output_cache_bytes)
            d->io.flush(d->io.user,e->output_cache_pointer,e->output_cache_bytes);
        else d->io.flush(d->io.user,b[i].pointer,b[i].bytes);
    }
    d->io.barrier(d->io.user);
    for(i=0;i<word_count;i++){
        v=i==tag_word[p]?tag:w[i];
        if(!e->descriptor_valid[i] || e->descriptor[i]!=v){
            if(wr(d,p,0x100u+i*4u,v))return fail(d,p,RC_IO_ERROR);
            e->descriptor[i]=v;e->descriptor_valid[i]=1;
        }
    }
    d->io.barrier(d->io.user);e->started_us=d->io.now_us(d->io.user);
    if(wr(d,p,0x10,1))return fail(d,p,RC_IO_ERROR);
    return RC_OK;
}
int rc_poll(rc_device *d,unsigned p){
    rc_engine *e;uint32_t s,a,c,v;int error;
    if(!d || !d->opened || p>3 || d->engine[p].state!=RC_JOB)return RC_ARGUMENT;
    e=&d->engine[p];if(rd(d,p,0x14,&s))return fail(d,p,RC_IO_ERROR);e->status=s;
    if(s&RESET)return fail(d,p,RC_RESET_REQUIRED);
    if(s>>16)return fail(d,p,RC_HARDWARE);
    if(s&CANCELLED)return fail(d,p,RC_CANCELLED);
    if(elapsed(d,e))return fail(d,p,RC_TIMEOUT);
    if(!(s&DONE))return RC_PENDING;
    if((s&(BUSY|QUIET|CLEAR_PENDING|COMMAND_PENDING|CONFIG_PENDING))!=QUIET)return RC_PENDING;
    if((error=identity(d,p)))return fail(d,p,error);
    if(rd(d,p,0x18,&a) || rd(d,p,0x1c,&c) || rd(d,p,0x20,&v))return fail(d,p,RC_IO_ERROR);
    if(a!=e->pending_tag || c!=e->pending_tag || v!=e->expected_values)return fail(d,p,RC_STALE);
    if(rd(d,p,0x30,&a) || rd(d,p,0x34,&c))return fail(d,p,RC_IO_ERROR);
    if(a-e->accepted_before!=1u || c-e->completed_before!=1u)return fail(d,p,RC_STALE);
    if(rd(d,p,0x24,&e->cycles) || rd(d,p,0x28,&e->read_beats))return fail(d,p,RC_IO_ERROR);
    e->completed++;release(d,p,1);return RC_OK;
}
int rc_cancel(rc_device *d,unsigned p){
    if(!d || !d->opened || p>3 || (d->engine[p].state!=RC_JOB && d->engine[p].state!=RC_CONFIG))return RC_ARGUMENT;
    return fail(d,p,RC_CANCELLED);
}
int rc_recover_begin(rc_device *d,unsigned p){
    rc_engine *e;uint32_t s,o;int error;
    if(!d || !d->opened || p>3 || d->engine[p].state!=RC_QUARANTINE)return RC_ARGUMENT;
    e=&d->engine[p];if((error=identity(d,p)) || (error=rd(d,p,0x14,&s)))return error;
    if(s&RESET)return RC_RESET_REQUIRED;
    if((s&(BUSY|QUIET|COMMAND_PENDING|CONFIG_PENDING|CLEAR_PENDING))!=QUIET)return RC_PENDING;
    if(p>=2){
        if(rd(d,p,0x48,&o))return RC_IO_ERROR;
        if(o&8u)return RC_RESET_REQUIRED;
        if((o&3u)!=(p-1u) && ((o&3u)!=0 || !(o&4u)))return RC_PENDING;
    }
    if(wr(d,p,0x10,4))return RC_IO_ERROR;
    e->state=RC_CLEAR;e->started_us=d->io.now_us(d->io.user);return RC_OK;
}
int rc_recover_poll(rc_device *d,unsigned p){
    rc_engine *e;uint32_t s;int error;
    if(!d || !d->opened || p>3 || d->engine[p].state!=RC_CLEAR)return RC_ARGUMENT;
    e=&d->engine[p];if(rd(d,p,0x14,&s))return fail(d,p,RC_IO_ERROR);
    if(s&RESET)return fail(d,p,RC_RESET_REQUIRED);
    if(elapsed(d,e))return fail(d,p,RC_TIMEOUT);
    if((s&(BUSY|QUIET|READY|CLEAR_PENDING|COMMAND_PENDING|CONFIG_PENDING))!=(QUIET|READY) || (s>>16))return RC_PENDING;
    if((error=identity(d,p)))return fail(d,p,error);
    release(d,p,0);memset(e->descriptor_valid,0,sizeof(e->descriptor_valid));
    if(p==RC_FIELD){memset(d->input_valid,0,sizeof(d->input_valid));memset(d->output_valid,0,sizeof(d->output_valid));memset(d->program_valid,0,sizeof(d->program_valid));memset(d->native_valid,0,sizeof(d->native_valid));}
    return RC_OK;
}
int rc_config_begin(rc_device *d,const rc_config *cfg,const rc_buffer *buffer){
    rc_engine *e;uint32_t s,metadata;int error;
    if(!d || !d->opened || !cfg || cfg->kind>3 || cfg->bank>4 || cfg->slot>15 || cfg->scalar>1 || cfg->wide>1)return RC_ARGUMENT;
    e=&d->engine[RC_FIELD];if(e->state!=RC_IDLE)return RC_AGAIN;
    if(cfg->kind==0 && ((cfg->data>>32) || cfg->index>=64))return RC_ARGUMENT;
    if(cfg->kind==3 && (cfg->index>=5 || cfg->data>>32 || cfg->bank || cfg->slot || cfg->scalar || !cfg->wide))return RC_ARGUMENT;
    if((cfg->kind==1 || cfg->kind==2) && cfg->bank && cfg->slot>=8)return RC_ARGUMENT;
    if(cfg->kind==1 && (cfg->index>=40 || (cfg->scalar && ((cfg->data>>52)&2047u)==2047u)))return RC_ARGUMENT;
    if(cfg->kind==2 && (cfg->index>=8 || cfg->scalar || cfg->data>>32))return RC_ARGUMENT;
    if(cfg->kind==3){
        uint32_t flag=d->native_config[4],resident=flag>>31;
        uint32_t sizes[4]={614400u,307200u,115200u,307200u};
        if(cfg->index==4){
            if(buffer || !cfg->data || ((cfg->data&0x80000000u) &&
               ((cfg->data&0x7fffffe0u) || (cfg->data&31u)>16u)))return RC_ARGUMENT;
        }else{
            if(!d->native_valid[4])return RC_ARGUMENT;
            if(resident){sizes[0]=163840u;sizes[1]=60u*(80u-(flag&31u))*32u;sizes[2]=60u*(80u-(flag&31u))*4u;}
            if(resident && cfg->index==3){if(buffer || cfg->data)return RC_ARGUMENT;}
            else if(!buffer || !span(buffer) || buffer->address!=cfg->data ||
                buffer->bytes!=sizes[cfg->index] || buffer->write!=(cfg->index==(resident?2u:3u)))return RC_ARGUMENT;
        }
    }else if(cfg->kind==2 || (cfg->kind==1 && !cfg->scalar)){
        if(!buffer || !span(buffer) || buffer->address!=cfg->data || buffer->write!=(cfg->kind==2))return RC_ARGUMENT;
    }else if(buffer)return RC_ARGUMENT;
    if((error=identity(d,RC_FIELD)) || (error=rd(d,RC_FIELD,0x14,&s)))return error;
    if(s&RESET)return RC_RESET_REQUIRED;
    if(s>>16)return RC_HARDWARE;
    if((s&(BUSY|QUIET|READY|CLEAR_PENDING|COMMAND_PENDING|CONFIG_PENDING))!=(QUIET|READY))return RC_AGAIN;
    if(rd(d,RC_FIELD,0x44,&d->config_before))return RC_IO_ERROR;
    d->pending_config=*cfg;if(buffer)d->pending_config_buffer=*buffer;else memset(&d->pending_config_buffer,0,sizeof(rc_buffer));
    metadata=cfg->kind|((uint32_t)cfg->index<<2)|((uint32_t)cfg->bank<<10)|((uint32_t)cfg->slot<<13)|((uint32_t)cfg->scalar<<17)|((uint32_t)cfg->wide<<18);
    e->state=RC_CONFIG;e->first_error=0;
    if(wr(d,RC_FIELD,0x80,metadata) || wr(d,RC_FIELD,0x84,(uint32_t)cfg->data) || wr(d,RC_FIELD,0x88,(uint32_t)(cfg->data>>32)))return fail(d,RC_FIELD,RC_IO_ERROR);
    d->io.barrier(d->io.user);e->started_us=d->io.now_us(d->io.user);
    if(wr(d,RC_FIELD,0x10,8))return fail(d,RC_FIELD,RC_IO_ERROR);
    return RC_OK;
}
int rc_config_poll(rc_device *d){
    rc_engine *e;rc_config *c;uint32_t s,count;int error;
    if(!d || !d->opened || d->engine[RC_FIELD].state!=RC_CONFIG)return RC_ARGUMENT;
    e=&d->engine[RC_FIELD];if(rd(d,RC_FIELD,0x14,&s) || rd(d,RC_FIELD,0x44,&count))return fail(d,RC_FIELD,RC_IO_ERROR);
    if(s&RESET)return fail(d,RC_FIELD,RC_RESET_REQUIRED);
    if(s>>16)return fail(d,RC_FIELD,RC_HARDWARE);
    if(s&CANCELLED)return fail(d,RC_FIELD,RC_CANCELLED);
    if(elapsed(d,e))return fail(d,RC_FIELD,RC_TIMEOUT);
    if((s&CONFIG_PENDING) || count==d->config_before)return RC_PENDING;
    if(count-d->config_before!=1u)return fail(d,RC_FIELD,RC_STALE);
    if((error=identity(d,RC_FIELD)))return fail(d,RC_FIELD,error);
    c=&d->pending_config;
    if(d->field_mode!=(c->kind==3)){
        memset(d->input_valid,0,sizeof(d->input_valid));memset(d->output_valid,0,sizeof(d->output_valid));
        memset(d->program_valid,0,sizeof(d->program_valid));memset(d->native_valid,0,sizeof(d->native_valid));
        d->field_mode=(uint8_t)(c->kind==3);
    }
    if(c->kind==3){if(c->index==4)memset(d->native_valid,0,sizeof(d->native_valid));d->native_config[c->index]=(uint32_t)c->data;d->native_valid[c->index]=1;
        if(c->index<4)d->native_buffer[c->index]=d->pending_config_buffer;}
    else if(c->kind==0)d->program_valid[c->index]=1;
    else if(c->kind==1){d->input_config[c->index]=*c;d->input_buffer[c->index]=d->pending_config_buffer;d->input_valid[c->index]=1;}
    else {d->output_config[c->index]=*c;d->output_buffer[c->index]=d->pending_config_buffer;d->output_valid[c->index]=1;}
    e->state=RC_IDLE;return RC_OK;
}
