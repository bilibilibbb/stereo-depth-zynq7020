#ifndef FW_A9_PLATFORM_H
#define FW_A9_PLATFORM_H
#include <stdint.h>
/* Real A9/BSP implementation only. CPU0 and CPU1 share one aligned lock
 * for the bound PL310 cache routines; no USE_AMP L2 bypass is permitted. */
unsigned fw_a9_core_id(void);
void fw_a9_barrier(void);
void fw_a9_notify(void);
void fw_a9_wait(void);
void fw_a9_clean(uint32_t *,const void *,uint32_t);
void fw_a9_invalidate(uint32_t *,void *,uint32_t);
int fw_a9_section_range(const uint32_t *,uint32_t,uint32_t,unsigned,unsigned);
int fw_a9_platform_check(const uint32_t *,unsigned,uint32_t *);
#endif
