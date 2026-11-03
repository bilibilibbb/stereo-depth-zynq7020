#ifndef FIELD_STREAM_V346_H
#define FIELD_STREAM_V346_H
#include "field_async_v282.h"
size_t fs_color_bytes_v346(void);
int fs_color_logits_begin_v348(void *,size_t,const float *,const double *,const float *,const double *,unsigned,float *,float *,const field_cache_v282 *);
int fs_color_begin_v346(void *,size_t,const double *,const float *,const double *,unsigned,float *,float *,const field_cache_v282 *);
int fs_color_producer_v346(void *);
int fs_color_consumer_v346(void *);
int fs_color_cancel_v346(void *);
int fs_color_take_v346(void *);
unsigned fs_color_rows_v346(const void *);
const double *fs_color_last_row_v346(const void *);
unsigned fs_color_output_v346(const void *);
size_t fs_gray_bytes_v346(void);
int fs_gray_begin_v346(void *,size_t,double *,int64_t *,const double *,const float *,float *,const field_cache_v282 *);
int fs_gray_producer_v346(void *);
int fs_gray_consumer_v346(void *);
int fs_gray_cancel_v346(void *);
int fs_gray_take_v346(void *);
unsigned fs_gray_prepared_v346(const void *);
unsigned fs_gray_output_v346(const void *);
size_t fs_color_scratch_offset_v350(void);
size_t fs_gray_scratch_offset_v350(void);
size_t fs_color_init_bytes_v350(void);
size_t fs_gray_init_bytes_v350(void);
unsigned fs_color_consumed_v349(const void *);
int fs_color_ready_v349(void *);
int fs_gray_ready_v349(void *);
#endif
