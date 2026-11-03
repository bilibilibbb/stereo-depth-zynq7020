#ifndef COMMAND_TRACE_V347_H
#define COMMAND_TRACE_V347_H
#ifdef R14_HOST_REFERENCE
void trace_cpu_phase_v347(const char *,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,int);
#else
static inline void trace_cpu_phase_v347(const char *n,unsigned f,unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,unsigned g,unsigned h,int r){(void)n;(void)f;(void)a;(void)b;(void)c;(void)d;(void)e;(void)g;(void)h;(void)r;}
#endif
#endif
