#ifndef SNAPSHOT_VIDEO_LANE_H
#define SNAPSHOT_VIDEO_LANE_H
#include "camera_capture.h"

/* CPU0 owns the two alternating DMA slots. It arms the next capture before
 * acquiring/copying the previous pixels. The model takes one separate full
 * stereo copy; neither inference nor TCP can hold a camera DMA slot.
 * One finite packet may be TCP-loaned while the other is replaced. */
enum { SS_VIDEO_FREE,SS_VIDEO_BUILD,SS_VIDEO_READY,SS_VIDEO_LOAN };
enum { SS_VIDEO_IDLE,SS_VIDEO_WAIT,SS_VIDEO_ACQUIRE,SS_VIDEO_COPY,SS_VIDEO_RELEASE };
typedef struct {
    rc_device *device;
    ss_camera *camera;
    uint8_t *packet[2];size_t capacity;
    uint64_t boot,sequence,ready_us,last_us;
    uint32_t crc,raw_epoch,source_frame,last_source_frame;
    unsigned full_range,phase,row,state[2],raw_slot,source_seen,next_slot,model_copy;
    int writer,loan,error,active;
    uint64_t published,dropped,model_frames;
    uint8_t gray[SS_RAW_WIDTH*4u];
} ss_video;
int ss_video_init(ss_video *,rc_device *,uint64_t,unsigned,uint8_t *,uint8_t *,size_t);
/* No peer and no pending model means no new DMA. Accepted work still drains. */
int ss_video_step(ss_video *,int admit_capture);
int ss_video_acquire(ss_video *,const uint8_t **,size_t *);
void ss_video_release(ss_video *);
#endif
