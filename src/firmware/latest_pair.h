#ifndef SNAPSHOT_LATEST_PAIR_H
#define SNAPSHOT_LATEST_PAIR_H
#include "pair_export.h"

enum { SS_FREEZE_META_BYTES=40,SS_FROZEN_PACKET_BYTES=SS_PACKET_BYTES+SS_FREEZE_META_BYTES };
enum { SS_FREEZE_IDLE,SS_FREEZE_READY };
typedef struct {
    uint8_t *slot[3];
    uint64_t raw_arm_us[3],raw_ready_us[3];
    ss_identity source[3];
    uint32_t payload_crc[3];
    ss_identity request;
    uint64_t pressed_us,selected_us,published;
    int latest,writing,pinned,freeze;
} ss_latest_pair;
/* CPU0-only ownership. Three bounded result slots: sending may pin one
 * while the other two alternate latest/producer. No whole-frame copy on
 * the key path, inference duplication or queue of old results. */
int ss_latest_init(ss_latest_pair *,uint8_t *,uint8_t *,uint8_t *,size_t);
int ss_latest_write(ss_latest_pair *,uint8_t **);
int ss_latest_publish(ss_latest_pair *,const uint8_t *,size_t,uint64_t,uint64_t);
void ss_latest_discard(ss_latest_pair *);
/* No complete result: return SS_PENDING without retaining a request.
 * Selection pins the current complete slot until the sender releases it. */
int ss_latest_freeze(ss_latest_pair *,ss_identity,uint64_t,uint64_t);
int ss_latest_packet(const ss_latest_pair *,const uint8_t **,size_t *);
/* Only after the actual sender has ended every loan, including cancel. */
void ss_latest_sent(ss_latest_pair *);
#endif
