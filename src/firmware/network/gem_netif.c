#include "gem_netif.h"
#include "memory_layout.h"
#include "fw_a9_platform.h"
#include "lwip/init.h"
#include "lwip/etharp.h"
#include "lwip/priv/tcp_priv.h"
#include "netif/ethernet.h"
#include <string.h>

/* Polled port over the unchanged installed XEmacPs v3.8 driver. Each
 * Ethernet frame owns one dedicated copy/BD, so lwIP pbufs can be released
 * independently. No packet pointer crosses into CPU1 or an interrupt. */
static uint32_t rd(ss_gem *g,uint32_t r){return XEmacPs_ReadReg(g->config.base,r);}
static void wr(ss_gem *g,uint32_t r,uint32_t v){XEmacPs_WriteReg(g->config.base,r,v);}
static void barrier(ss_gem *g){g->config.cache.barrier(g->config.cache.user);}
static void fail(ss_gem *g,int code){if(!g->error)g->error=code;netif_set_link_down(&g->netif);}
static unsigned index_of(const XEmacPs_BdRing *r,const XEmacPs_Bd *b){return (unsigned)(((UINTPTR)b-r->BaseBdAddr)/r->Separation);}
static void retire_tx(ss_gem *g){
    XEmacPs_BdRing *r=&XEmacPs_GetTxRing(&g->device);XEmacPs_Bd *b,*p;unsigned i,n,ready=0;
    /* SDK v3.8 FromHwTx advances past an unused BD without breaking. Its
     * limit counts completions, not inspected BDs; passing 8 when only one
     * is done can wrap and count that same completion repeatedly. Here one
     * BD is one frame: first find a contiguous completed prefix, then ask
     * the unchanged driver for exactly that prefix. No DMA-owned reuse. */
    barrier(g);p=r->HwHead;
    while(ready<8u && ready<r->HwCnt){
        uint32_t status=*(volatile const uint32_t *)((uintptr_t)p+XEMACPS_BD_STAT_OFFSET);
        if(!(status&XEMACPS_TXBUF_USED_MASK))break;
        if(!(status&XEMACPS_TXBUF_LAST_MASK)){fail(g,-19);return;}
        ready++;p=XEmacPs_BdRingNext(r,p);
    }
    if(!ready)return;
    n=XEmacPs_BdRingFromHwTx(r,ready,&b);if(n!=ready){fail(g,-20);return;}p=b;
    for(i=0;i<n;i++,p=XEmacPs_BdRingNext(r,p)){
        if(XEmacPs_BdIsTxRetry(p) || XEmacPs_BdIsTxUrun(p) || XEmacPs_BdIsTxExh(p))g->tx_dropped++;
    }
    if(n && XEmacPs_BdRingFree(r,n,b)!=XST_SUCCESS)fail(g,-20);
}
static err_t output(struct netif *netif,struct pbuf *packet){
    ss_gem *g=netif->state;XEmacPs_BdRing *r=&XEmacPs_GetTxRing(&g->device);XEmacPs_Bd *b;unsigned i;uint32_t wrap;
    if(g->error || !g->running || !netif_is_link_up(netif))return ERR_IF;
    if(!packet || packet->tot_len<14u || packet->tot_len>1514u)return ERR_BUF;
    /* Reclaim is bounded in service(), not recursively inside tcp_output. */
    if(XEmacPs_BdRingAlloc(r,1,&b)!=XST_SUCCESS){g->tx_busy++;return ERR_MEM;}
    i=index_of(r,b);if(i>=SS_GEM_TX_COUNT){fail(g,-21);return ERR_IF;}
    if(pbuf_copy_partial(packet,g->tx[i],packet->tot_len,0)!=packet->tot_len){
        (void)XEmacPs_BdRingUnAlloc(r,1,b);return ERR_BUF;
    }
    g->config.cache.clean(g->config.cache.user,g->tx[i],SS_GEM_FRAME_BYTES);
    wrap=XEmacPs_BdGetStatus(b)&XEMACPS_TXBUF_WRAP_MASK;
    XEmacPs_BdSetAddressTx(b,(UINTPTR)g->tx[i]);
    /* SDK BdSetStatus ORs bits; it does NOT replace a retired length/error. */
    XEmacPs_BdWrite(b,XEMACPS_BD_STAT_OFFSET,wrap|XEMACPS_TXBUF_USED_MASK|XEMACPS_TXBUF_LAST_MASK|packet->tot_len);
    if(XEmacPs_BdRingToHw(r,1,b)!=XST_SUCCESS){fail(g,-22);return ERR_IF;}
    barrier(g);XEmacPs_BdClearTxUsed(b);barrier(g);XEmacPs_Transmit(&g->device);
    g->tx_packets++;return ERR_OK;
}
static err_t setup(struct netif *n){
    ss_gem *g=n->state;n->name[0]='s';n->name[1]='g';n->hwaddr_len=6;
    memcpy(n->hwaddr,g->config.mac,6);n->mtu=1500;
    n->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP|NETIF_FLAG_ETHERNET;
    n->output=etharp_output;n->linkoutput=output;return ERR_OK;
}
static int rings(ss_gem *g){
    XEmacPs_BdRing *rx=&XEmacPs_GetRxRing(&g->device),*tx=&XEmacPs_GetTxRing(&g->device);
    XEmacPs_Bd template_bd,bv[1],*b,*p;unsigned i;
    if(XEmacPs_BdRingCreate(rx,SS_GEM_BEGIN,SS_GEM_BEGIN,XEMACPS_BD_ALIGNMENT,SS_GEM_RX_COUNT)!=XST_SUCCESS ||
       XEmacPs_BdRingCreate(tx,SS_GEM_BEGIN+0x10000u,SS_GEM_BEGIN+0x10000u,XEMACPS_BD_ALIGNMENT,SS_GEM_TX_COUNT)!=XST_SUCCESS)return -23;
    XEmacPs_BdClear(&template_bd);
    if(XEmacPs_BdRingClone(rx,&template_bd,XEMACPS_RECV)!=XST_SUCCESS)return -24;
    XEmacPs_BdClear(bv);XEmacPs_BdSetTxUsed(bv);
    if(XEmacPs_BdRingClone(tx,bv,XEMACPS_SEND)!=XST_SUCCESS ||
       XEmacPs_BdRingAlloc(rx,SS_GEM_RX_COUNT,&b)!=XST_SUCCESS)return -25;
    p=b;
    for(i=0;i<SS_GEM_RX_COUNT;i++,p=XEmacPs_BdRingNext(rx,p)){
        g->config.cache.clean(g->config.cache.user,g->rx[i],SS_GEM_FRAME_BYTES);
        XEmacPs_BdSetAddressRx(p,(UINTPTR)g->rx[i]);
    }
    if(XEmacPs_BdRingToHw(rx,SS_GEM_RX_COUNT,b)!=XST_SUCCESS)return -26;
    barrier(g);
    XEmacPs_SetQueuePtr(&g->device,rx->BaseBdAddr,0,XEMACPS_RECV);
    XEmacPs_SetQueuePtr(&g->device,tx->BaseBdAddr,0,XEMACPS_SEND);return 0;
}
static void receive(ss_gem *g){
    XEmacPs_BdRing *r=&XEmacPs_GetRxRing(&g->device);XEmacPs_Bd *b,*p,*again;unsigned i,k,n,length;
    barrier(g);n=XEmacPs_BdRingFromHwRx(r,4,&b);p=b;
    for(i=0;i<n;i++,p=XEmacPs_BdRingNext(r,p)){
        struct pbuf *packet=NULL;k=index_of(r,p);length=XEmacPs_GetRxFrameSize(&g->device,p);
        if(k>=SS_GEM_RX_COUNT){fail(g,-27);return;}
        if(XEmacPs_BdIsRxSOF(p) && XEmacPs_BdIsRxEOF(p) && length>=14u && length<=1514u){
            g->config.cache.invalidate(g->config.cache.user,g->rx[k],SS_GEM_FRAME_BYTES);barrier(g);
            packet=pbuf_alloc(PBUF_RAW,(u16_t)length,PBUF_POOL);
            if(packet && pbuf_take(packet,g->rx[k],(u16_t)length)!=ERR_OK){pbuf_free(packet);packet=NULL;}
        }
        if(packet){
            g->rx_packets++;
            if(g->netif.input(packet,&g->netif)!=ERR_OK)pbuf_free(packet);
        }else g->rx_dropped++;
    }
    if(!n)return;
    if(XEmacPs_BdRingFree(r,n,b)!=XST_SUCCESS || XEmacPs_BdRingAlloc(r,n,&again)!=XST_SUCCESS || again!=b){fail(g,-28);return;}
    /* Hardware still sees NEW until each completed buffer is re-published. */
    if(XEmacPs_BdRingToHw(r,n,b)!=XST_SUCCESS){fail(g,-29);return;}
    p=b;
    for(i=0;i<n;i++,p=XEmacPs_BdRingNext(r,p)){
        XEmacPs_BdWrite(p,XEMACPS_BD_STAT_OFFSET,0);barrier(g);XEmacPs_BdClearRxNew(p);
    }
    barrier(g);
}
static int mdio(ss_gem *g,unsigned reg,int write,uint16_t value,uint64_t now){
    uint32_t word;
    if(g->mdio_active || !(rd(g,XEMACPS_NWSR_OFFSET)&XEMACPS_NWSR_MDIOIDLE_MASK))return -30;
    word=XEMACPS_PHYMNTNC_OP_MASK|(write?XEMACPS_PHYMNTNC_OP_W_MASK:XEMACPS_PHYMNTNC_OP_R_MASK)|
        (g->config.phy_address<<XEMACPS_PHYMNTNC_PHAD_SHFT_MSK)|(reg<<XEMACPS_PHYMNTNC_PREG_SHFT_MSK)|value;
    wr(g,XEMACPS_PHYMNTNC_OFFSET,word);barrier(g);g->mdio_active=1;g->mdio_begin=now;return 0;
}
static void phy(ss_gem *g,uint64_t now){
    uint16_t v;unsigned speed;int rc;
    if(!g->mdio_active){
        if(now<g->next_phy)return;
        switch(g->phy_state){
        case 0:rc=mdio(g,2,0,0,now);break;
        case 1:rc=mdio(g,3,0,0,now);break;
        case 2:rc=mdio(g,4,1,0x0141u,now);break; /* 10/100 full duplex */
        case 3:rc=mdio(g,9,1,0x0200u,now);break; /* 1000 full duplex */
        case 4:rc=mdio(g,0,0,0,now);break;
        case 5:rc=mdio(g,0,1,(uint16_t)((g->phy_status&~0x0c00u)|0x1200u),now);break;
        case 6:case 7:rc=mdio(g,1,0,0,now);break; /* twice for latched link */
        case 8:rc=mdio(g,17,0,0,now);break;
        default:fail(g,-31);return;
        }
        if(rc)fail(g,rc);
        return;
    }
    if(!(rd(g,XEMACPS_NWSR_OFFSET)&XEMACPS_NWSR_MDIOIDLE_MASK)){
        if(now-g->mdio_begin>10000u)fail(g,-32);
        return;
    }
    v=(uint16_t)rd(g,XEMACPS_PHYMNTNC_OFFSET);g->mdio_active=0;
    if(g->phy_state==0){g->phy_id[0]=v;if(v!=0x001cu){fail(g,-33);return;}}
    if(g->phy_state==1){g->phy_id[1]=v;if(!v || v==65535u){fail(g,-34);return;}}
    if(g->phy_state==4)g->phy_status=v;
    if(g->phy_state==7 && (v&0x24u)!=0x24u){
        if(netif_is_link_up(&g->netif)){netif_set_link_down(&g->netif);g->link_changes++;}
        g->phy_state=6;g->next_phy=now+250000u;return;
    }
    if(g->phy_state==8){
        speed=(v>>14)==2u?1000u:(v>>14)==1u?100u:10u;
        if((v&0x2400u)!=0x2400u || (v>>14)==3u){netif_set_link_down(&g->netif);g->phy_state=6;g->next_phy=now+250000u;return;}
        if(speed!=g->speed){
            /* No clock change while a TX copy remains DMA-owned. Keep all
             * storage pinned; this may wait for actual TX completion. */
            if(XEmacPs_GetTxRing(&g->device).HwCnt){g->phy_state=6;g->next_phy=now+250000u;return;}
            if(g->config.set_clock(g->config.user,speed)){fail(g,-35);return;}
            XEmacPs_SetOperatingSpeed(&g->device,(u16)speed);g->speed=speed;
        }
        if(!g->running){XEmacPs_Start(&g->device);XEmacPs_IntDisable(&g->device,XEMACPS_IXR_ALL_MASK);g->running=1;}
        if(!netif_is_link_up(&g->netif)){netif_set_link_up(&g->netif);g->link_changes++;}
        if(XEmacPs_GetTxRing(&g->device).HwCnt)XEmacPs_Transmit(&g->device);
        g->phy_state=6;g->next_phy=now+1000000u;return;
    }
    g->phy_state++;
}
int ss_gem_init(ss_gem *g,const ss_gem_config *c){
    XEmacPs_Config cfg;ss_gem_config copy;ip4_addr_t ip,mask,gateway;uint32_t dma;unsigned i;uint8_t bits=0;int rc;
    if(!g || !c || g->attempted || c->base!=0xe000b000u || c->phy_address!=1u ||
       !c->now_us || !c->set_clock || !c->cache.clean || !c->cache.invalidate || !c->cache.barrier ||
       !c->table || fw_a9_core_id()!=0u || ((uintptr_t)g&63u) || (c->mac[0]&1u) ||
       (uintptr_t)g<SS_IMAGE_BEGIN || (uintptr_t)g>SS_IMAGE_END-sizeof(*g) ||
       !fw_a9_section_range(c->table,(uint32_t)(uintptr_t)g,sizeof(*g),0,1) ||
       !fw_a9_section_range(c->table,c->base,4096u,1,0) ||
       c->table[SS_GEM_BEGIN>>20]!=0x07011de2u)return -1;
    for(i=0;i<6;i++)bits|=c->mac[i];
    if(!bits)return -1;
    /* Do not seize a previously running MAC on a warm/unknown image. */
    if(XEmacPs_ReadReg(c->base,XEMACPS_NWCTRL_OFFSET)&(XEMACPS_NWCTRL_RXEN_MASK|XEMACPS_NWCTRL_TXEN_MASK))return -2;
    copy=*c;memset(g,0,sizeof(*g));g->config=copy;g->attempted=1;c=&g->config;
    cfg=(XEmacPs_Config){0,c->base,0};
    if(XEmacPs_CfgInitialize(&g->device,&cfg,c->base)!=XST_SUCCESS || g->device.Version!=2u)return -3;
    XEmacPs_IntDisable(&g->device,XEMACPS_IXR_ALL_MASK);
    if(XEmacPs_ClearOptions(&g->device,XEMACPS_RX_CHKSUM_ENABLE_OPTION|XEMACPS_TX_CHKSUM_ENABLE_OPTION|XEMACPS_FLOW_CONTROL_OPTION)!=XST_SUCCESS ||
       XEmacPs_SetMacAddress(&g->device,g->config.mac,1)!=XST_SUCCESS)return -4;
    XEmacPs_SetMdioDivisor(&g->device,MDC_DIV_224);
    dma=rd(g,XEMACPS_DMACR_OFFSET);dma=(dma&~XEMACPS_DMACR_RXBUF_MASK)|((SS_GEM_FRAME_BYTES/64u)<<XEMACPS_DMACR_RXBUF_SHIFT);
    wr(g,XEMACPS_DMACR_OFFSET,dma);
    rc=rings(g);if(rc)return rc;
    lwip_init();
    IP4_ADDR(&ip,c->ip[0],c->ip[1],c->ip[2],c->ip[3]);IP4_ADDR(&mask,c->mask[0],c->mask[1],c->mask[2],c->mask[3]);
    IP4_ADDR(&gateway,c->gateway[0],c->gateway[1],c->gateway[2],c->gateway[3]);
    if(!netif_add(&g->netif,&ip,&mask,&gateway,g,setup,ethernet_input))return -5;
    netif_set_default(&g->netif);netif_set_up(&g->netif);
    g->last_now=g->last_tcp=g->last_arp=c->now_us(c->user);g->initialized=1;return 0;
}
void ss_gem_service(void *user){
    ss_gem *g=user;uint64_t now;uint32_t rx_status,tx_status;
    if(!g || !g->initialized || g->error)return;
    now=g->config.now_us(g->config.user);if(now<g->last_now){fail(g,-36);return;}g->last_now=now;
    retire_tx(g);if(g->error)return;
    if(g->running){
        receive(g);if(g->error)return;
        rx_status=rd(g,XEMACPS_RXSR_OFFSET);tx_status=rd(g,XEMACPS_TXSR_OFFSET);
        if((rx_status&XEMACPS_RXSR_HRESPNOK_MASK) || (tx_status&XEMACPS_TXSR_HRESPNOK_MASK)){fail(g,-37);return;}
        wr(g,XEMACPS_RXSR_OFFSET,rx_status);wr(g,XEMACPS_TXSR_OFFSET,tx_status);
        /* The DMA can stop at a USED descriptor while a later packet is
         * published. A pending, DMA-owned head must not wait for the
         * one-second PHY fallback to trigger transmission again. This
         * only starts the existing ring: no reset, descriptor rewrite or
         * premature reclamation, and no kick while TX is already active. */
        if(!(tx_status&XEMACPS_TXSR_TXGO_MASK) && XEmacPs_GetTxRing(&g->device).HwCnt){
            XEmacPs_Bd *head=XEmacPs_GetTxRing(&g->device).HwHead;
            barrier(g);
            if(!(*(volatile const uint32_t *)((uintptr_t)head+XEMACPS_BD_STAT_OFFSET)&XEMACPS_TXBUF_USED_MASK))
                XEmacPs_Transmit(&g->device);
        }
    }
    phy(g,now);
    if(now-g->last_tcp>=250000u){g->last_tcp+=250000u;tcp_tmr();}
    if(now-g->last_arp>=1000000u){g->last_arp+=1000000u;etharp_tmr();}
}
