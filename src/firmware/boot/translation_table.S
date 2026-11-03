/* One immutable short-descriptor table shared by the two A9 cores.
 * Attributes match the bound SDK xil_mmu.h, not the measurement queue map.
 * 1GiB DDR: normal WB shareable, except GEM descriptors and fixed wire mailboxes.
 * Device mappings remain Device/Strongly-ordered; reserved ranges fault. */
.section .mmu_tbl,"a",%progbits
.balign 16384
.global MMUTable
.type MMUTable,%object
MMUTable:
.set ss_section,0
.rept 4096
    .if (ss_section == 0x070) || (ss_section == 0x071)
        .word (ss_section << 20) + 0x11de2
    .elseif ss_section < 0x400
        .word (ss_section << 20) + 0x15de6
    .elseif ss_section < 0xc00
        .word (ss_section << 20) + 0x00c02
    .elseif (ss_section >= 0xe00) && (ss_section < 0xe03)
        .word (ss_section << 20) + 0x00c06
    .elseif (ss_section >= 0xf80) && (ss_section < 0xf90)
        .word (ss_section << 20) + 0x00c06
    .elseif ss_section == 0xfff
        /* BootROM CPU1 mailbox in high OCM; no cached alias. */
        .word (ss_section << 20) + 0x11de2
    .else
        .word 0
    .endif
    .set ss_section,ss_section+1
.endr
.size MMUTable,.-MMUTable
