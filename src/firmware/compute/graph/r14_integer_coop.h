#ifndef R14_INTEGER_COOP_H
#define R14_INTEGER_COOP_H
#include "r14_nn_coop.h"
#include "r14_graph_coop.h"
typedef struct {
 uint32_t state,frame,tag,error;
 uint32_t descriptor[10];rc_buffer buffer[4];unsigned buffers;
 r14_integer_request request;
} r14_integer_job;
typedef struct {
 uint32_t cookie,cycle_limit;rc_device *device;
 uint8_t *memory[2];uint32_t address[2],bytes;
 r14_integer_job *active,*registered[2];uint64_t jobs,values;
} r14_integer_coop;
int r14_integer_coop_init(r14_integer_coop *,rc_device *,void *,void *,uint32_t,uint32_t);
int r14_integer_coop_begin(r14_integer_coop *,r14_integer_job *,uint32_t,const r14_integer_request *);
int r14_integer_coop_step(r14_integer_coop *,r14_integer_job *);
int r14_integer_coop_cancel(r14_integer_coop *,r14_integer_job *);
int r14_integer_coop_recover(r14_integer_coop *);
#endif
