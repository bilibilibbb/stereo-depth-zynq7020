#include "latest_pair.h"
#include <string.h>

static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t u64(const uint8_t *p){return (uint64_t)u32(p)<<32|u32(p+4);}
static void put32(uint8_t *p,uint32_t x){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(x>>(24u-8u*i));}
static void put64(uint8_t *p,uint64_t x){put32(p,(uint32_t)(x>>32));put32(p+4,(uint32_t)x);}
static int span(const void *p,size_t n){return p && n-1<=UINTPTR_MAX-(uintptr_t)p;}
static int overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;}
int ss_latest_init(ss_latest_pair *s,uint8_t *a,uint8_t *b,uint8_t *tx,size_t capacity){
    const void *v[4]={s,a,b,tx};size_t n[4]={sizeof(*s),SS_FROZEN_PACKET_BYTES,SS_FROZEN_PACKET_BYTES,SS_FROZEN_PACKET_BYTES};
    unsigned i,j;
    if(capacity<SS_FROZEN_PACKET_BYTES)return SS_ARGUMENT;
    for(i=0;i<4;i++){if(!span(v[i],n[i]))return SS_ARGUMENT;for(j=0;j<i;j++)if(overlap(v[i],n[i],v[j],n[j]))return SS_ARGUMENT;}
    memset(s,0,sizeof(*s));s->slot[0]=a;s->slot[1]=b;s->slot[2]=tx;
    s->latest=s->writing=s->pinned=-1;return SS_OK;
}
int ss_latest_write(ss_latest_pair *s,uint8_t **packet){
    int next;if(!s || !packet)return SS_ARGUMENT;
    *packet=NULL;if(s->writing>=0)return SS_PENDING;
    for(next=0;next<3;next++)if(next!=s->latest && next!=s->pinned)break;
    if(next==3)return SS_PENDING;
    s->writing=next;*packet=s->slot[next];return SS_OK;
}
int ss_latest_publish(ss_latest_pair *s,const uint8_t *packet,size_t bytes,uint64_t armed,uint64_t acquired){
    ss_identity source;
    if(!s || s->writing<0 || packet!=s->slot[s->writing] || bytes!=SS_PACKET_BYTES ||
       memcmp(packet,"SDP1",4) || packet[4]!=1 || packet[5]!=2 || packet[6] || packet[7]!=96 ||
       u32(packet+8)!=SS_PAYLOAD_BYTES || packet[80] || packet[81]!=4 ||
       !u64(packet+16) || !u64(packet+24) || armed>acquired || acquired>u64(packet+48) ||
       u64(packet+48)>u64(packet+56) || u64(packet+56)>u64(packet+64))return SS_ARGUMENT;
    source=(ss_identity){u64(packet+16),u64(packet+24),u32(packet+32),u32(packet+36)};
    if(s->latest>=0 && (s->source[s->latest].boot!=source.boot || s->source[s->latest].shot>=source.shot))return SS_STALE;
    s->source[s->writing]=source;s->payload_crc[s->writing]=u32(packet+12);
    s->raw_arm_us[s->writing]=armed;s->raw_ready_us[s->writing]=acquired;
    s->latest=s->writing;s->writing=-1;s->published++;return SS_OK;
}
void ss_latest_discard(ss_latest_pair *s){if(s)s->writing=-1;}
static int select_latest(ss_latest_pair *s,uint64_t now){
    int index=s->latest;uint8_t *p=s->slot[index],*meta=p+SS_PACKET_BYTES;
    ss_identity source=s->source[index];uint32_t checksum;
    if(s->request.boot!=source.boot || now<u64(p+64))return SS_STALE;
    s->pinned=index;s->selected_us=now;
    put64(meta,source.shot);put32(meta+8,source.epoch);put32(meta+12,source.attempt);
    put64(meta+16,s->raw_arm_us[index]);put64(meta+24,s->raw_ready_us[index]);put64(meta+32,now);
    checksum=ss_crc32_update(s->payload_crc[index]^UINT32_MAX,meta,SS_FREEZE_META_BYTES)^UINT32_MAX;
    put32(p+8,SS_PAYLOAD_BYTES+SS_FREEZE_META_BYTES);put32(p+12,checksum);
    put64(p+16,s->request.boot);put64(p+24,s->request.shot);
    put32(p+32,s->request.epoch);put32(p+36,s->request.attempt);put64(p+40,s->pressed_us);p[81]=5;
    s->freeze=SS_FREEZE_READY;return SS_OK;
}
int ss_latest_freeze(ss_latest_pair *s,ss_identity request,uint64_t pressed,uint64_t now){
    if(!s || !request.boot || !request.shot || !request.epoch || !request.attempt || pressed>now)return SS_ARGUMENT;
    if(s->freeze!=SS_FREEZE_IDLE)return SS_PENDING;
    if(s->latest<0)return SS_PENDING;
    s->request=request;s->pressed_us=pressed;
    return select_latest(s,now);
}
int ss_latest_packet(const ss_latest_pair *s,const uint8_t **p,size_t *n){
    if(!s || !p || !n)return SS_ARGUMENT;
    *p=NULL;*n=0;if(s->freeze!=SS_FREEZE_READY)return SS_PENDING;
    *p=s->slot[s->pinned];*n=SS_FROZEN_PACKET_BYTES;return SS_OK;
}
void ss_latest_sent(ss_latest_pair *s){if(s && s->freeze==SS_FREEZE_READY){s->pinned=-1;s->freeze=SS_FREEZE_IDLE;}}
