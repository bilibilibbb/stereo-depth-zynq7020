#ifndef RETAINED_PERIPHERALS_H
#define RETAINED_PERIPHERALS_H
#include "retained_model.h"
#include "retained_neural_driver.h"
#include "retained_board_layout.h"
typedef struct {uint16_t reg,value;uint32_t delay_ms;} r14_camera_step;
typedef struct {
    rn_io io;uint32_t base,build,timeout_us,error,detail;int opened;
} r14_camera;
/* SCCB and physical reset only. Full-frame DDR capture uses rc_device. */
RN_API int r14_camera_open(r14_camera *,const r14_camera_step *,uint32_t,const r14_camera_step *,uint32_t);
typedef struct {
    rn_io io;uint32_t base,physical[2],front,timeout_us,error,detail;uint8_t *frames[2];int opened,quarantined;
} r14_display;
RN_API int r14_display_open(r14_display *);
RN_API int r14_display_present(r14_display *,uint32_t);
#endif
