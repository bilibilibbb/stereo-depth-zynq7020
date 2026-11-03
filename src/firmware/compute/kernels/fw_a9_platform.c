#include "fw_a9_platform.h"
#include "fw_color_bsp_rename.h"
#include "xil_cache.h"
#include "xil_mmu.h"
#include "xparameters_ps.h"
#include "fw_sparse.h"
#if !defined(__arm__) || defined(R14_HOST_REFERENCE)
#error "The A9 platform unit requires actual ARM instructions and the bound BSP"
#endif
void fw_a9_barrier(void){__asm__ volatile("dsb sy":::"memory");}
void fw_a9_notify(void){__asm__ volatile("dsb sy\nsev":::"memory");}
unsigned fw_a9_core_id(void){uint32_t x;__asm__ volatile("mrc p15,0,%0,c0,c0,5":"=r"(x));return x&3u;}
static uint32_t irq_lock(void){uint32_t saved;__asm__ volatile("mrs %0,cpsr\ncpsid if":"=r"(saved)::"memory");return saved;}
static void irq_restore(uint32_t saved){__asm__ volatile("msr cpsr_c,%0"::"r"(saved):"memory");}
static void cache_enter(uint32_t *lock){while(__atomic_exchange_n(lock,1u,__ATOMIC_ACQUIRE))__asm__ volatile("yield":::"memory");}
static void cache_leave(uint32_t *lock){__atomic_store_n(lock,0u,__ATOMIC_RELEASE);}
void fw_a9_clean(uint32_t *lock,const void *data,uint32_t bytes){
 uint32_t saved=irq_lock();cache_enter(lock);
 /* Private SDK unit is compiled without USE_AMP: both cores maintain L2.
  * Serialize PL310 debug/sync register sequences, including partial lines. */
 Xil_DCacheFlushRange((INTPTR)data,bytes);fw_a9_barrier();cache_leave(lock);irq_restore(saved);
}
void fw_a9_invalidate(uint32_t *lock,void *data,uint32_t bytes){
 uint32_t saved=irq_lock();cache_enter(lock);
 Xil_DCacheInvalidateRange((INTPTR)data,bytes);fw_a9_barrier();cache_leave(lock);irq_restore(saved);
}
int fw_a9_section_range(const uint32_t *table,uint32_t begin,uint32_t bytes,unsigned device,unsigned cached){
 uint32_t first=begin>>20u,last;unsigned i;
 if(!bytes || bytes-1u>UINT32_MAX-begin)return 0;
 last=(begin+bytes-1u)>>20u;
 for(i=first;i<=last;i++){
  uint32_t d=table[i],attr=(d&0xfffffu)&~16u;
  if((d&0xfff00000u)!=(i<<20u))return 0;
  if(device){if(attr!=DEVICE_MEMORY && attr!=STRONG_ORDERED && attr!=(DEVICE_MEMORY|SHAREABLE) && attr!=(STRONG_ORDERED|SHAREABLE))return 0;}
  else if(attr!=NORM_WB_CACHE && (cached || attr!=NORM_NONCACHE))return 0;
 }
 return 1;
}
int fw_a9_platform_check(const uint32_t *table,unsigned core,uint32_t *fpscr){
 uint32_t sctlr,actlr,ttbr,ttbcr,dacr;
 if(!table || ((uintptr_t)table&16383u) || fw_a9_core_id()!=core || core>1u)return FW_ARGUMENT;
 __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
 __asm__ volatile("mrc p15,0,%0,c1,c0,1":"=r"(actlr));
 __asm__ volatile("mrc p15,0,%0,c2,c0,0":"=r"(ttbr));
 __asm__ volatile("mrc p15,0,%0,c2,c0,2":"=r"(ttbcr));
 __asm__ volatile("mrc p15,0,%0,c3,c0,0":"=r"(dacr));
 __asm__ volatile("vmrs %0,fpscr":"=r"(*fpscr));
 /* Shared producer buffers can be maintained by the opposite core before
  * DMA. Both SMP[6] and cache-maintenance broadcast FW[0] are required.
  * ARM DDI0388I section4.3.10/table4-36; inspect only, never alter boot state. */
 if((sctlr&0x1005u)!=0x1005u || (sctlr&0x12000000u) || (actlr&65u)!=65u ||
    ttbcr || (ttbr&0xffffc000u)!=(uint32_t)(uintptr_t)table || !(dacr&1u) ||
    (*fpscr&0x03c00000u) || !fw_a9_section_range(table,XPS_SCU_PERIPH_BASE,0x3000u,1u,0u))return FW_ARGUMENT;
 if(!(*(volatile uint32_t *)(uintptr_t)XPS_SCU_PERIPH_BASE&1u) ||
    !(*(volatile uint32_t *)(uintptr_t)(XPS_L2CC_BASEADDR+0x100u)&1u))return FW_ARGUMENT;
 return FW_OK;
}
void fw_a9_wait(void){__asm__ volatile("wfe":::"memory");}
