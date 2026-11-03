#ifndef SNAPSHOT_VIDEO_CAPTURE_H
#define SNAPSHOT_VIDEO_CAPTURE_H
#include "retained_camera_driver.h"
/* Video owns both raw slots, CPU-read-only after the initial clean.
 * Poll validates descriptors only; caller must invalidate consumed pixels
 * after arming the other slot. Original rc_release still retires each slot. */
int ss_video_capture_arm(rc_device *,uint32_t);
int ss_video_capture_poll(rc_device *,uint32_t);
#endif
