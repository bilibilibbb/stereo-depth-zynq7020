#ifndef R14_HOST_MATH_H
#define R14_HOST_MATH_H
/* Host verification build only. ARM deployment does not include this file. */
#include <math.h>
int r14_host_math_init(void);
float r14_host_expf(float);
float r14_host_logf(float);
float r14_host_tanhf(float);
#define expf r14_host_expf
#define logf r14_host_logf
#define tanhf r14_host_tanhf
#endif
