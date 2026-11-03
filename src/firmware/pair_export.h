#ifndef SNAPSHOT_PAIR_EXPORT_H
#define SNAPSHOT_PAIR_EXPORT_H
#include <stddef.h>
#include <stdint.h>

enum { SS_WIDTH=320, SS_HEIGHT=240, SS_PIXELS=76800,
       SS_MODEL_PIXELS=81920, SS_HEADER_BYTES=96,
       SS_PAYLOAD_BYTES=SS_PIXELS*9, SS_PACKET_BYTES=SS_HEADER_BYTES+SS_PAYLOAD_BYTES };
enum { SS_OK=0, SS_PENDING=1, SS_ARGUMENT=-1, SS_DATA=-2, SS_STALE=-3, SS_CANCELLED=-4 };
typedef struct { uint64_t boot,shot; uint32_t epoch,attempt; } ss_identity;
typedef struct {
    ss_identity identity, rgb_identity, sgm_identity, model_identity;
    uint64_t trigger_us,locked_us,sgm_done_us,model_done_us;
    const uint8_t *rgb; size_t rgb_bytes;
    const uint64_t *sgm; size_t sgm_pixels;
    const float *model; size_t model_pixels;
    const uint8_t *roi[2]; size_t roi_pixels;
    uint16_t flags;
} ss_pair_source;
typedef struct {
    ss_pair_source source;
    uint8_t *packet;
    size_t capacity,crc_offset;
    uint32_t row,crc;
    int state;
} ss_pair_job;

/* Single-owner CPU adapter. RGB/results and both rectification masks must be complete,
 * cache-acquired and leased for this identity; this does not poll hardware.
 * Keep their read leases until step returns OK or an error. The job/packet
 * must be private and disjoint from sources. Reinit requires caller drain.
 * The output stays unpublished until both planes and CRC are complete. */
int ss_pair_begin(ss_pair_job *,const ss_pair_source *,void *,size_t);
/* At most rows (1..8) image rows OR rows*320*9 CRC bytes per call. */
int ss_pair_step(ss_pair_job *,unsigned rows);
int ss_pair_packet(const ss_pair_job *,const uint8_t **,size_t *);
void ss_pair_cancel(ss_pair_job *);
uint32_t ss_crc32_update(uint32_t,const uint8_t *,size_t);
#endif
