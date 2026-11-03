#include "ps_io.h"
#include <stddef.h>

enum {SLCR_LOCK=0xf8000004u,SLCR_UNLOCK=0xf8000008u,IO_PLL=0xf8000108u,
      PLL_STATUS=0xf800010cu,APER_CLK=0xf800012cu,GEM_RX_CLK=0xf8000138u,
      GEM_TX_CLK=0xf8000140u,GEM_RESET=0xf8000214u,BOOT_MODE=0xf800025cu,
      MIO=0xf8000700u,GPIO_DATA=0xe000a060u,GPIO_DIR=0xe000a204u,GPIO_OEN=0xe000a208u};
static uint32_t rd(const ss_ps_io *p,uint32_t a){return p->read(p->user,a);}
static void wr(ss_ps_io *p,uint32_t a,uint32_t v){p->write(p->user,a,v);p->barrier(p->user);}
static int clock_valid(const ss_ps_io *p){
    uint32_t io=rd(p,IO_PLL);
    /* BOOT_MODE PLL_BYPASS is bit 4, not the boot-device field [3:0]. */
    if((io&0x13u) || ((io&8u) && (rd(p,BOOT_MODE)&16u)) ||
       !(rd(p,PLL_STATUS)&4u) || !(rd(p,APER_CLK)&64u) ||
       (rd(p,GEM_RX_CLK)&17u)!=1u || (rd(p,GEM_RESET)&0x51u) ||
       (rd(p,GEM_TX_CLK)&0x60u))return -2;
    return 0;
}
int ss_ps_dividers(uint32_t ref,uint32_t fdiv,unsigned speed,uint32_t *out){
    uint64_t source,target,product,diff;uint32_t a,b;
    if(!out || ref!=33333333u || fdiv<13u || fdiv>66u)return -1;
    target=speed==1000u?125000000u:speed==100u?25000000u:speed==10u?2500000u:0u;
    if(!target)return -1;
    source=(uint64_t)ref*fdiv;
    for(b=1;b<64u;b++)for(a=1;a<64u;a++){
        product=target*a*b;diff=product>source?product-source:source-product;
        if(diff<=fdiv){*out=(a<<8)|(b<<20);return 0;}
    }
    return -2;
}
int ss_ps_prepare(ss_ps_io *p){
    unsigned pin;uint32_t unused;
    if(!p || !p->read || !p->write || !p->barrier || p->prepared || p->reference_hz!=33333333u)return -1;
    /* RGMII takes the highest-priority MIO L0 route. RX receivers enabled. */
    for(pin=16;pin<=27;pin++)
        if(!(rd(p,MIO+4u*pin)&2u) || (pin>=22u && (rd(p,MIO+4u*pin)&0x2000u)))return -3;
    if((rd(p,MIO+52u*4u)&0xfeu)!=0x80u || (rd(p,MIO+53u*4u)&0x20feu)!=0x80u ||
       (rd(p,MIO+10u*4u)&0x20feu))return -3;
    if(clock_valid(p) || ss_ps_dividers(p->reference_hz,(rd(p,IO_PLL)>>12)&127u,1000u,&unused))return -2;
    wr(p,GPIO_DIR,rd(p,GPIO_DIR)&~(1u<<10));wr(p,GPIO_OEN,rd(p,GPIO_OEN)&~(1u<<10));
    if((rd(p,GPIO_DIR)|rd(p,GPIO_OEN))&(1u<<10))return -4;
    p->prepared=1;return 0;
}
int ss_ps_key1(const ss_ps_io *p){
    if(!p || !p->prepared)return -1;
    return (rd(p,GPIO_DATA)&(1u<<10))?0:1;
}
int ss_ps_gem_clock(void *user,unsigned speed){
    ss_ps_io *p=user;uint32_t div,old,next,observed;int rc;
    if(!p || !p->prepared || clock_valid(p))return -1;
    rc=ss_ps_dividers(p->reference_hz,(rd(p,IO_PLL)>>12)&127u,speed,&div);if(rc)return rc;
    old=rd(p,GEM_TX_CLK);next=(old&~0x03f03f01u)|div|1u;
    wr(p,SLCR_UNLOCK,0xdf0du);wr(p,GEM_TX_CLK,old&~1u);
    wr(p,GEM_TX_CLK,next&~1u);wr(p,GEM_TX_CLK,next);
    observed=rd(p,GEM_TX_CLK);wr(p,SLCR_LOCK,0x767bu);
    return observed==next?0:-4;
}
