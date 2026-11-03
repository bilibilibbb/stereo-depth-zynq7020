#ifndef SNAPSHOT_XLWIPCONFIG_H
#define SNAPSHOT_XLWIPCONFIG_H
/* The imported SDK Ethernet protocol header asks only about AXI 1588
 * padding. This application uses PS GEM and ordinary Ethernet framing. */
#define XLWIP_CONFIG_AXI_ETHERNET_ENABLE_1588 0
#endif
