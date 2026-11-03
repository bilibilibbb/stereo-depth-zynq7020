#include "boot_config.h"
#include "pair_export.h"
#include <string.h>
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put32(uint8_t *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t crc(const uint8_t *p,size_t n){return ss_crc32_update(UINT32_MAX,p,n)^UINT32_MAX;}
static int pl(uint32_t p){return p>=0x40000000u && p<=0xbfff0000u && !(p&65535u);}
static int host_ip(uint32_t ip,uint32_t mask){return ip && (ip>>24)!=127u && (ip>>28)<14u && (ip&~mask) && (ip&~mask)!=~mask;}
int ss_boot_decode(ss_boot_config *c,const uint8_t *p,size_t n,uint32_t recipe){
    ss_boot_config t;uint32_t ip,mask,gw,inv;unsigned i,mac=0;
    if(!c || !p || n!=SS_BOOT_CONFIG_BYTES || !recipe || memcmp(p,"S114",4) || le32(p+4)!=1u || le32(p+8)!=n ||
       le32(p+156)!=crc(p,156) || le32(p+84)!=11459u)return -1;
    for(i=92;i<156;i++)if(p[i])return -1;
    if(p[70] || p[71])return -1;
    memset(&t,0,sizeof(t));
    t.seed=le32(p+12);t.epoch=le32(p+16);t.platform=le32(p+20);
    t.nn_base=le32(p+24);t.box_base=le32(p+28);t.sgm_base=le32(p+32);t.sgm_build=le32(p+36);
    t.mode_tag=le32(p+40);t.recipe_crc=le32(p+44);t.capture_cookie=le32(p+48);
    t.driver_timeout_us=le32(p+52);t.sgm_timeout_cycles=le32(p+56);t.debounce_us=le32(p+60);
    memcpy(t.mac,p+64,6);memcpy(t.ip,p+72,4);memcpy(t.mask,p+76,4);memcpy(t.gateway,p+80,4);t.calibration_crc=le32(p+88);
    if(!t.seed || !t.epoch || !t.platform || !t.sgm_build || !t.mode_tag || t.recipe_crc!=recipe || !t.capture_cookie ||
       !t.driver_timeout_us || t.driver_timeout_us>10000000u || !t.sgm_timeout_cycles || !t.debounce_us || t.debounce_us>1000000u ||
       !pl(t.nn_base) || t.box_base!=t.nn_base+0xc00u || !pl(t.sgm_base) || t.nn_base==t.sgm_base)return -2;
    /* Reserved existing camera control/capture windows cannot alias engines. */
    if(t.nn_base==0x43c10000u || t.nn_base==0x43c40000u || t.box_base==0x43c10000u || t.box_base==0x43c40000u ||
       t.sgm_base==0x43c10000u || t.sgm_base==0x43c40000u)return -2;
    for(i=0;i<6;i++)mac|=t.mac[i];
    ip=be32(t.ip);mask=be32(t.mask);gw=be32(t.gateway);inv=~mask;
    if(!mac || (t.mac[0]&1u) || !mask || inv<3u || (inv&(inv+1u)) || !host_ip(ip,mask) ||
       (gw && (!host_ip(gw,mask) || (gw&mask)!=(ip&mask) || gw==ip)))return -3;
    *c=t;return 0;
}
int ss_boot_sequence_next(uint8_t out[16],const uint8_t in[16],uint32_t seed,uint64_t *boot){
    uint8_t next[16];uint32_t old;
    if(!out || !in || !boot || !seed || memcmp(in,"SSEQ",4) || le32(in+4)!=seed || le32(in+12)!=crc(in,12))return -1;
    old=le32(in+8);if(old==UINT32_MAX)return -2;
    memcpy(next,in,16);put32(next+8,old+1u);put32(next+12,crc(next,12));
    memcpy(out,next,16);*boot=((uint64_t)seed<<32)|(old+1u);return 0;
}
