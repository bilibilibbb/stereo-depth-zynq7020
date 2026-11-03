#ifndef FW_MEMORY_LAYOUT_H
#define FW_MEMORY_LAYOUT_H
#include <stdint.h>
/* Pointer-neutral offsets into a separately registered, frame-bound DDR
 * region. These are physical C payload layouts, not an assertion that an
 * array is on-chip. Initialization by CPU0 precedes the stated writer's
 * lease. Numeric layouts exclude metadata/diagnostics except entries with
 * element_bits0, which explicitly denote coherent/uncached control lines
 * (private color FIELD accounting), never numeric PL scratch. All three actors
 * use the same region identity; the adapter supplies its physical base. */
enum {FW_OWNER_CPU0=1u,FW_OWNER_CPU1=2u,FW_OWNER_PL=4u};
typedef struct {
 uint32_t id,offset,bytes,element_bits,row_elements,rows,writer,readers;
} fw_memory_span;
_Static_assert(sizeof(fw_memory_span)==32,"pointer-neutral memory span");
#endif
