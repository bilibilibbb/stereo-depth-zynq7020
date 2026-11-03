#ifndef SNAPSHOT_GEM_NETIF_H
#define SNAPSHOT_GEM_NETIF_H
#include "fw_sparse.h"
#include "xemacps.h"
#include "lwip/netif.h"
enum {SS_GEM_RX_COUNT=64,SS_GEM_TX_COUNT=32,SS_GEM_FRAME_BYTES=1600};
typedef struct {
    uint32_t base,phy_address;
    uint8_t mac[6],ip[4],mask[4],gateway[4];
    fw_cache cache;
    const uint32_t *table;
    uint64_t (*now_us)(void *);void *user;
    int (*set_clock)(void *,unsigned);
} ss_gem_config;
typedef struct {
    XEmacPs device;struct netif netif;ss_gem_config config;
    uint32_t initialized,attempted,running,speed;
    uint32_t phy_state,mdio_active,phy_id[2],phy_status;
    uint64_t mdio_begin,next_phy,last_tcp,last_arp,last_now;
    uint64_t rx_packets,tx_packets,rx_dropped,tx_dropped,tx_busy,link_changes;
    int error;
    _Alignas(64) uint8_t rx[SS_GEM_RX_COUNT][SS_GEM_FRAME_BYTES];
    _Alignas(64) uint8_t tx[SS_GEM_TX_COUNT][SS_GEM_FRAME_BYTES];
} ss_gem;
/* CPU0 only, fresh loader-owned GEM; already configured PS pinmux/clocks.
 * Uses real SDK driver/BD rings plus lwIP core. Descriptor MB is permanently
 * noncacheable; payloads use the SAME L1/L2 cache lock as model/camera.
 * Init never waits for cable/autonegotiation and never owns model buffers. */
int ss_gem_init(ss_gem *,const ss_gem_config *);
/* One iteration: <=4 RX frames, <=8 TX retirements, <=1 MDIO transaction
 * advance, one overdue TCP/ARP tick each. All raw API runs here/on CPU0.
 * PHY status is asynchronous: no vendor unbounded MDIO/autoneg busy-loop. */
void ss_gem_service(void *);
#endif
