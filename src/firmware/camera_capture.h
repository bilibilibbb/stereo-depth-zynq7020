#ifndef SNAPSHOT_CAMERA_CAPTURE_H
#define SNAPSHOT_CAMERA_CAPTURE_H
#include "snapshot_flow.h"
#include "retained_camera_driver.h"

enum { SS_RAW_WIDTH=640,SS_RAW_HEIGHT=480,SS_RAW_RGB_BYTES=921600,
       SS_CALIBRATION_BYTES=128+SS_PIXELS*16+64,
       SS_PREVIEW_MAX_BYTES=96+SS_RAW_RGB_BYTES };
typedef struct {
    const uint8_t *blob;
    const float *map[2],*matrix_q;
    uint32_t platform,mode_tag,full_range,max_skew_ticks,header_crc,magic;
} ss_calibration;
/* Bind the original R14CAL.BIN contract. Validates its calibration flag,
 * geometry, camera mode, map/Q/header CRC and finite usable map area.
 * No identity-map fallback. Keep this blob immutable for the camera lifetime. */
int ss_calibration_bind(ss_calibration *,const void *,size_t,uint32_t);
typedef struct {
    rc_device *device;
    const ss_calibration *calibration;
    uint64_t boot;
    uint8_t *raw_rgb,*rectified[2],*roi[2],*preview_packet;
    size_t raw_rgb_bytes,rectified_bytes,roi_bytes,preview_capacity;
    uint8_t *right_raw_rgb; /* Separate DDR scratch; never shared with CPU0. */
    void (*notify)(void *); /* Optional wake after publishing work. */
    void *notify_user;
} ss_camera_config;
typedef struct {
    ss_identity identity;
    uint64_t raw_arm_us,raw_ready_us,locked_us;
    uint32_t capture_epoch,source_id[2],sof_ticks[2],skew_ticks,calibration_crc;
} ss_camera_receipt;
enum ss_camera_phase { SS_CAM_IDLE,SS_CAM_WAIT,SS_CAM_CONVERT,SS_CAM_PREPROCESS,
                       SS_CAM_PREVIEW_COPY,SS_CAM_RELEASE,SS_CAM_QUARANTINE,SS_CAM_EXTERNAL_COPY };
enum ss_camera_work_phase { SS_CAM_WORK_ACQUIRE,SS_CAM_WORK_CONVERT,
                            SS_CAM_WORK_RECTIFY,SS_CAM_WORK_DONE };
typedef struct {
    _Alignas(64) uint32_t phase,ready_rows;
    unsigned row;
    int error;
    _Alignas(64) uint8_t gray_rows[SS_RAW_WIDTH*4u];
} ss_camera_work;
typedef struct {
    ss_camera_config config;
    ss_camera_receipt receipt;
    ss_identity request;
    uint64_t trigger_us,last_us,raw_ready_us,preview_sequence;
    unsigned initialized,phase,raw_shot,side,row;
    unsigned request_owned,request_pending,request_ready,request_cancelled;
    unsigned promote_pending;
    unsigned preview_ready,preview_loan;
    unsigned external_preview,imported_raw;
    uint8_t *external_raw[2]; /* One model-owned stereo copy, independent of DMA slots. */
    uint32_t preview_crc;
    int error,driver_error,request_error;
    /* OPEN plus one entered bit per worker: closing admission is distinct
     * from retirement. Neither source DMA nor scratch can be reused early. */
    _Alignas(64) uint32_t work_gate;
    ss_camera_work work[2];
} ss_camera;

/* rc_open and real sensor setup must already have succeeded. Uses actual
 * retained capture slot0; slot1 remains reserved but is not armed here.
 * All supplied writable spans are private/disjoint. The device cache hooks
 * must use the same A9/PL310 lock as the compute workers. */
int ss_camera_init(ss_camera *,const ss_camera_config *);
/* CPU0 switches capture ownership to the video producer before either lane
 * starts. A model request pins this separate copy through both workers. */
int ss_camera_enable_video(ss_camera *,uint8_t *,uint8_t *,size_t);
int ss_camera_copy_begin(ss_camera *,unsigned,uint64_t);
int ss_camera_copy_finish(ss_camera *);
void ss_camera_copy_abort(ss_camera *,int);
/* CPU0 only, once per main loop. One driver poll/release OR <=4 conversion,
 * rectification or preview rows. Real rc_poll validates both eye descriptors,
 * SOF skew, freshness and drained complete DMA before any pixels are used. */
int ss_camera_step(ss_camera *);
/* CPU0 is serviced by ss_camera_step; the real CPU1 loop calls this with 1.
 * At most four rows, except the one-time CPU1 raw-DMA cache acquisition.
 * Configuration must be published before the secondary starts calling.
 * OK means progress, PENDING means no work. Each side has one producer. */
int ss_camera_worker_step(ss_camera *,unsigned);
/* Direct adapters for ss_flow capture callbacks. OK begin is one logical
 * snapshot lease even if an earlier preview capture is still completing. */
int ss_camera_begin(void *,ss_identity,uint64_t);
/* Continuous producer: reuse the newest retained raw pair if available, or
 * the currently armed pair. Its acquisition may precede this admission.
 * This never consumes a preview packet or queues older camera frames. */
int ss_camera_begin_latest(void *,ss_identity,uint64_t);
int ss_camera_poll(void *,ss_identity,ss_capture_pair *);
void ss_camera_cancel(void *,ss_identity);
int ss_camera_release(void *,ss_identity);
/* At most one preview packet. A loan pins that copy while capture/compute
 * use other buffers. Drop/release only after the network source loan ends. */
int ss_camera_preview_acquire(ss_camera *,const uint8_t **,size_t *);
void ss_camera_preview_release(ss_camera *);
#endif
