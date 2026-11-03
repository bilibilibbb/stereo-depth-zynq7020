/* ROM wake entry for the second core of this one shared image.
 * Vendor secondary boot initializes PRIVATE CPU state only. The compiler
 * defines USE_AMP=1 for that ONE assembly object, never for runtime caches.
 * No CPU1 CRT, global constructors, BSS clearing or shared timer reset. */
.syntax unified
.arm
.section .text.ss_cpu1_boot,"ax",%progbits
.balign 32
.global ss_cpu1_boot
.type ss_cpu1_boot,%function
ss_cpu1_boot:
    cpsid if
    mrc p15,0,r0,c0,c0,5
    and r0,r0,#3
    cmp r0,#1
    bne ss_cpu1_park
    /* The caller established the fresh ROM-wait precondition. Refuse an
     * unexpected enabled cache/MMU rather than destroying live state. */
    mrc p15,0,r0,c1,c0,0
    tst r0,#1
    bne ss_cpu1_park
    tst r0,#4
    bne ss_cpu1_park
    mov r0,#0
    mcr p15,0,r0,c2,c0,2
    dsb sy
    isb
    b ss_cpu1_vendor_boot
.size ss_cpu1_boot,.-ss_cpu1_boot

.section .text.ss_cpu1_vectors,"ax",%progbits
.balign 32
.global ss_cpu1_vectors
.type ss_cpu1_vectors,%function
ss_cpu1_vectors:
    b ss_cpu1_boot
    b ss_cpu1_park
    b ss_cpu1_park
    b ss_cpu1_park
    b ss_cpu1_park
    b ss_cpu1_park
    b ss_cpu1_park
    b ss_cpu1_park
.size ss_cpu1_vectors,.-ss_cpu1_vectors

.section .text.ss_cpu1_park,"ax",%progbits
.type ss_cpu1_park,%function
ss_cpu1_park:
    cpsid if
    wfe
    b ss_cpu1_park
.size ss_cpu1_park,.-ss_cpu1_park
