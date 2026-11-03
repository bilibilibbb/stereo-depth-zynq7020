#ifndef SNAPSHOT_LWIP_ARCH_CC_H
#define SNAPSHOT_LWIP_ARCH_CC_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_PLATFORM_DIAG(x) do {printf x;} while(0)
#define LWIP_PLATFORM_ASSERT(x) do {fprintf(stderr,"lwIP assertion: %s (%s:%d)\n",(x),__FILE__,__LINE__);abort();} while(0)
#define LWIP_RAND() ((uint32_t)rand())
#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#endif
