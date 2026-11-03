#ifndef SNAPSHOT_PS_IO_H
#define SNAPSHOT_PS_IO_H
#include <stdint.h>
/* The CPU0 application supplies actual volatile MMIO and a DSB. Tests can
 * exercise the exact pin/clock rejection logic without touching a board. */
typedef struct {
    uint32_t (*read)(void *,uint32_t);
    void (*write)(void *,uint32_t,uint32_t);
    void (*barrier)(void *);
    void *user;
    uint32_t reference_hz,prepared;
} ss_ps_io;
/* Validate FSBL-created J23 RGMII0/MDIO clocks and PS_KEY1 GPIO. No remux,
 * PLL reprogramming, PHY reset, whole-board reset or GEM takeover. */
int ss_ps_prepare(ss_ps_io *);
/* Return 1 when MIO10 is low. Debouncing/long-press policy is in the app. */
int ss_ps_key1(const ss_ps_io *);
/* Actual GEM reference-divider update only after its TX ownership drained. */
int ss_ps_gem_clock(void *,unsigned);
/* Pure integer divider selection. Only source-frequency integer rounding
 * (at most one reference Hz) is tolerated; no guessed near-enough clock. */
int ss_ps_dividers(uint32_t,uint32_t,unsigned,uint32_t *);
#endif
