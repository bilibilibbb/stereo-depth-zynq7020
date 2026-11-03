#ifndef SNAPSHOT_BOOT_CONFIG_H
#define SNAPSHOT_BOOT_CONFIG_H
#include <stdint.h>
#include <stddef.h>
enum {SS_BOOT_CONFIG_BYTES=160,SS_BOOT_SEQUENCE_BYTES=16};
typedef struct {
    uint32_t seed,epoch,platform,nn_base,box_base,sgm_base,sgm_build;
    uint32_t mode_tag,recipe_crc,capture_cookie,driver_timeout_us,sgm_timeout_cycles,debounce_us;
    uint8_t mac[6],ip[4],mask[4],gateway[4];
    uint32_t calibration_crc;
} ss_boot_config;
int ss_boot_decode(ss_boot_config *,const uint8_t *,size_t,uint32_t);
int ss_boot_sequence_next(uint8_t [16],const uint8_t [16],uint32_t,uint64_t *);
/* CPU0 before CPU1 release or any capture/network DMA. FatFs SD0 loads the
 * real calibration and persists/readbacks a new boot sequence. No fallback
 * identity map, invented PL build/base or silently reset boot identity. */
int ss_boot_files(ss_boot_config *,uint32_t,uint64_t *);
#endif
