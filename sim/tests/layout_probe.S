/* TEST ONLY: inert linker-reservation probe, never board firmware. */
.syntax unified
.arm
.section .vectors,"ax",%progbits
.global _vector_table
_vector_table:
    b _vector_table
.section .bss.layout_probe,"aw",%nobits
.space 64
