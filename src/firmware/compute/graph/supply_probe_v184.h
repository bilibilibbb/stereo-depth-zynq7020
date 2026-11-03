#ifndef SUPPLY_PROBE_V184_H
#define SUPPLY_PROBE_V184_H
#include <stdint.h>
#include <stddef.h>
enum {SUPPLY_CASES_V184=7,SUPPLY_MAGIC_V184=0x53553184u,SUPPLY_CAPACITY_V184=0x00800000u};
typedef struct {uint32_t node,kind,source_offset[3],source_bytes[3],out_offset,out_bytes,reserved[2];} supply_case_v184;
typedef struct {uint32_t magic,count,bytes,version;supply_case_v184 cases[SUPPLY_CASES_V184];} supply_blob_v184;
typedef struct {uint64_t begin_tick,end_tick;uint32_t node,clock_error;} supply_sample_v184;
size_t supply_work_bytes_v184(void);
int supply_prepare_v184(const void *,size_t,unsigned,void *,size_t,void *,size_t,float *,void *,size_t);
int supply_execute_v184(void *,uint64_t (*)(void),supply_sample_v184 *);
int supply_extract_v184(void *,const void *,void *,size_t);
int supply_execute_v186(void *,uint64_t (*)(void),supply_sample_v184 *);
int supply_quant_directed_v186(void);
#endif
