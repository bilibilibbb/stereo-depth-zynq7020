#ifndef FIELD_CPU_V274_H
#define FIELD_CPU_V274_H
#include "r14_field_coop.h"
typedef struct {uint64_t ticks[7];uint32_t jobs[7],values[7],checked_bytes,mismatch,first_offset,reserved;} field_stats_v274;
int field_cpu_v274(rf_job *,unsigned *);
void filter_cpu_stats_v274(const void *,field_stats_v274 *);
int field_q40_v274(uint64_t,int64_t *);
#endif
