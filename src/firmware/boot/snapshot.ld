/* One production image, private per-core stacks, one shared data/BSS.
 * CPU0 SDK CRT alone clears BSS/initializes libc and the global timer.
 * CPU1 branches straight from private CPU boot to ss_secondary_main. */
ENTRY(_vector_table)
MEMORY {
    image (rwx) : ORIGIN = 0x02000000, LENGTH = 0x02000000
    heap (rw) : ORIGIN = 0x04000000, LENGTH = 0x00800000
    stacks (rw) : ORIGIN = 0x04800000, LENGTH = 0x00500000
}
SECTIONS {
    .text : {
        KEEP(*(.vectors)) KEEP(*(.boot))
        KEEP(*(.text.ss_cpu1_boot)) KEEP(*(.text.ss_cpu1_vectors))
        *(.text .text.* .gnu.linkonce.t.* .glue_7 .glue_7t .vfp11_veneer)
    } > image
    .init : {KEEP(*(.init))} > image
    .fini : {KEEP(*(.fini))} > image
    .rodata : {*(.rodata .rodata.* .rodata1 .gnu.linkonce.r.*)} > image
    .ARM.extab : {*(.ARM.extab* .gnu.linkonce.armextab.*)} > image
    .ARM.exidx : {__exidx_start = .; *(.ARM.exidx* .gnu.linkonce.armexidx.*) __exidx_end = .;} > image
    .mmu_tbl ALIGN(16384) : {KEEP(*(.mmu_tbl))} > image
    .data : {*(.data .data.* .data1 .gnu.linkonce.d.* .sdata .sdata.* .sdata2 .sdata2.* .got .got.*)} > image
    .ctors : {KEEP(*crtbegin.o(.ctors)) KEEP(*(EXCLUDE_FILE(*crtend.o) .ctors)) KEEP(*(SORT(.ctors.*))) KEEP(*(.ctors))} > image
    .dtors : {KEEP(*crtbegin.o(.dtors)) KEEP(*(EXCLUDE_FILE(*crtend.o) .dtors)) KEEP(*(SORT(.dtors.*))) KEEP(*(.dtors))} > image
    .preinit_array : {__preinit_array_start = .; KEEP(*(.preinit_array*)) __preinit_array_end = .;} > image
    .init_array : {__init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;} > image
    .fini_array : {__fini_array_start = .; KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array)) __fini_array_end = .;} > image
    .sbss (NOLOAD) : {__sbss_start = .; *(.sbss .sbss.* .gnu.linkonce.sb.*) . = ALIGN(4); __sbss_end = .;} > image
    .bss (NOLOAD) : {__bss_start = .; *(.bss .bss.* .sbss2 .sbss2.* COMMON) . = ALIGN(64); __bss_end = .;} > image
    .heap (NOLOAD) : { _heap = .; _heap_start = .; . += 0x00800000; _heap_end = .; HeapBase = _heap; HeapLimit = _heap_end; } > heap
    .stacks (NOLOAD) : {
        . = ALIGN(64); _stack_end = .; . += 0x200000; _stack = .; __stack = .;
        ss_cpu1_stack_end = .; . += 0x200000; ss_cpu1_stack = .;
        . += 0x1000; __irq_stack = .; . += 0x2000; __supervisor_stack = .;
        . += 0x1000; __abort_stack = .; . += 0x1000; __fiq_stack = .; . += 0x1000; __undef_stack = .;
        . += 0x1000; ss_cpu1_irq_stack = .; . += 0x2000; ss_cpu1_supervisor_stack = .;
        . += 0x1000; ss_cpu1_abort_stack = .; . += 0x1000; ss_cpu1_fiq_stack = .; . += 0x1000; ss_cpu1_undef_stack = .;
    } > stacks
    _end = __bss_end; end = _end;
    .ARM.attributes 0 : {*(.ARM.attributes)}
}
ASSERT(SIZEOF(.mmu_tbl)==16384,"exactly one shared MMU table required")
ASSERT(__bss_end<=0x04000000,"image overlaps heap")
ASSERT(ss_cpu1_undef_stack<=0x04d00000,"stack reservation exceeded")
