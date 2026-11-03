#include "latest_pair.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t slots[3][SS_FROZEN_PACKET_BYTES],original[SS_PACKET_BYTES];
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t u64(const uint8_t *p){return (uint64_t)u32(p)<<32|u32(p+4);}
static void put64(uint8_t *p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(uint8_t)(v>>(56u-8u*i));}
static uint32_t crc(const void *p,size_t n){return ss_crc32_update(UINT32_MAX,p,n)^UINT32_MAX;}
static void publish(ss_latest_pair *s,uint64_t frame){
    uint8_t *p;assert(ss_latest_write(s,&p)==SS_OK);assert(s->pinned<0 || p!=s->slot[s->pinned]);
    memcpy(p,original,sizeof(original));put64(p+24,frame);
    assert(ss_latest_publish(s,p,sizeof(original),100000,120000)==SS_OK);
}
int main(int argc,char **argv){
    ss_latest_pair s={0};ss_identity request;FILE *f;const uint8_t *p;size_t n;uint32_t saved;unsigned i;
    assert(argc==3);f=fopen(argv[1],"rb");assert(f && fread(original,1,sizeof(original),f)==sizeof(original) && !fclose(f));
    request=(ss_identity){u64(original+16),1,1,1};
    assert(ss_latest_init(&s,slots[0],slots[1],slots[2],sizeof(slots[0]))==SS_OK);
    assert(ss_latest_freeze(&s,request,500000,501000)==SS_PENDING && s.freeze==SS_FREEZE_IDLE);
    assert(!s.request.shot && s.pinned<0);
    publish(&s,9);
    assert(ss_latest_packet(&s,&p,&n)==SS_PENDING && !p && !n && s.freeze==SS_FREEZE_IDLE);
    /* The slot manager only selects complete results. The board layer owns
     * the optional pending key and explicitly selects on first publication. */
    assert(ss_latest_freeze(&s,request,910000,910000)==SS_OK);
    assert(ss_latest_packet(&s,&p,&n)==SS_OK && p==slots[0] && n==SS_FROZEN_PACKET_BYTES);
    assert(u64(p+40)==910000 && u64(p+64)==900000 && u64(p+SS_PACKET_BYTES)==9);
    assert(u32(p+12)==crc(p+96,n-96) && !memcmp(p+96,original+96,SS_PAYLOAD_BYTES));
    saved=crc(p,n);
    for(i=10;i<16;i++){publish(&s,i);assert(crc(p,n)==saved);}
    assert(ss_latest_freeze(&s,request,950000,950000)==SS_PENDING);
    ss_latest_sent(&s);
    request.shot=2;assert(ss_latest_freeze(&s,request,1000000,1000000)==SS_OK);
    assert(ss_latest_packet(&s,&p,&n)==SS_OK && u64(p+SS_PACKET_BYTES)==15);
    assert(u64(p+40)>u64(p+64) && u64(p+SS_PACKET_BYTES+32)==1000000);
    ss_latest_sent(&s);
    request.shot=3;assert(ss_latest_freeze(&s,request,1100000,1100000)==SS_OK);
    assert(ss_latest_packet(&s,&p,&n)==SS_OK && u64(p+24)==3 && u64(p+SS_PACKET_BYTES)==15);
    assert(!memcmp(p+96,original+96,SS_PAYLOAD_BYTES) && u32(p+12)==crc(p+96,n-96));
    f=fopen(argv[2],"wb");assert(f && fwrite(p,1,n,f)==n && !fclose(f));
    puts("PASS: slot manager leaves pending-key ownership to board layer; immediate complete-slot ownership transfer, no payload copy, producer progress during a pin, repeat source with distinct requests and independent CRC/timestamps.");return 0;
}
