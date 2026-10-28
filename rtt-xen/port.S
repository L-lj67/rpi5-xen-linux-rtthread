/* SPDX-License-Identifier: Apache-2.0 */
/* Single EL1h CPU; full integer and FP/SIMD contexts. */
.section .text.boot,"ax"
.global _image_start
_image_start:
    b reset
    .long 0
    .quad 0x80000
    .quad _image_end - _image_start
    .quad 0xa
    .quad 0, 0, 0
    .long 0x644d5241
    .long 0
reset:
    msr daifset, #15
    mov x20, x0
    ldr x1, =_bss_start
    ldr x2, =_bss_end
1:  cmp x1, x2
    b.hs 2f
    str xzr, [x1], #8
    b 1b
2:  ldr x1, =_boot_stack_top
    mov sp, x1
    ldr x1, =boot_dtb
    str x20, [x1]
    mrs x1, cpacr_el1
    orr x1, x1, #(3 << 20)
    msr cpacr_el1, x1
    isb
    ldr x1, =vectors
    msr vbar_el1, x1
    isb
    /* Identity map MMIO as Device, guest RAM as normal cacheable memory. */
    ldr x1, =boot_l1
    ldr x4, =boot_l2
    mov x5, #0
    ldr x6, =0x0060000000000401
6:  lsl x2, x5, #21
    orr x2, x2, x6
    str x2, [x4, x5, lsl #3]
    add x5, x5, #1
    cmp x5, #512
    b.lo 6b
    orr x2, x4, #3
    str x2, [x1]
    ldr x2, =0x38000705
    str x2, [x4, #(448 * 8)]
    ldr x2, =0x40000705
    str x2, [x1, #8]
    msr ttbr0_el1, x1
    ldr x2, =0xff04
    msr mair_el1, x2
    ldr x2, =0x200803519
    msr tcr_el1, x2
    dsb sy
    tlbi vmalle1
    dsb sy
    isb
    mrs x2, sctlr_el1
    ldr x3, =0x1005
    orr x2, x2, x3
    msr sctlr_el1, x2
    isb
    bl rtthread_startup
3:  wfi
    b 3b

.macro SAVE_FRAME
    sub sp, sp, #800
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    stp x30, xzr, [sp, #240]
    stp q0, q1, [sp, #272]
    stp q2, q3, [sp, #304]
    stp q4, q5, [sp, #336]
    stp q6, q7, [sp, #368]
    stp q8, q9, [sp, #400]
    stp q10, q11, [sp, #432]
    stp q12, q13, [sp, #464]
    stp q14, q15, [sp, #496]
    stp q16, q17, [sp, #528]
    stp q18, q19, [sp, #560]
    stp q20, q21, [sp, #592]
    stp q22, q23, [sp, #624]
    stp q24, q25, [sp, #656]
    stp q26, q27, [sp, #688]
    stp q28, q29, [sp, #720]
    stp q30, q31, [sp, #752]
    mrs x0, fpcr
    mrs x1, fpsr
    str x0, [sp, #784]
    str x1, [sp, #792]
.endm
.macro RESTORE_FRAME
    ldp x0, x1, [sp, #256]
    msr elr_el1, x0
    msr spsr_el1, x1
    ldp q0, q1, [sp, #272]
    ldp q2, q3, [sp, #304]
    ldp q4, q5, [sp, #336]
    ldp q6, q7, [sp, #368]
    ldp q8, q9, [sp, #400]
    ldp q10, q11, [sp, #432]
    ldp q12, q13, [sp, #464]
    ldp q14, q15, [sp, #496]
    ldp q16, q17, [sp, #528]
    ldp q18, q19, [sp, #560]
    ldp q20, q21, [sp, #592]
    ldp q22, q23, [sp, #624]
    ldp q24, q25, [sp, #656]
    ldp q26, q27, [sp, #688]
    ldp q28, q29, [sp, #720]
    ldp q30, q31, [sp, #752]
    ldr x0, [sp, #784]
    ldr x1, [sp, #792]
    msr fpcr, x0
    msr fpsr, x1
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #800
    eret
.endm

.section .text,"ax"
.global rt_hw_context_switch
rt_hw_context_switch:
    SAVE_FRAME
    mrs x2, nzcv
    mrs x3, daif
    orr x2, x2, x3
    mov x4, #5
    orr x2, x2, x4
    stp x30, x2, [sp, #256]
    ldp x0, x1, [sp, #0]
    mov x2, sp
    str x2, [x0]
    ldr x2, [x1]
    mov sp, x2
    clrex
    RESTORE_FRAME

.global rt_hw_context_switch_to
rt_hw_context_switch_to:
    ldr x1, [x0]
    mov sp, x1
    clrex
    RESTORE_FRAME

.section .text.vectors,"ax"
.balign 2048
vectors:
.rept 4
    b exception_entry
    .balign 128
    b irq_entry
    .balign 128
    b exception_entry
    .balign 128
    b exception_entry
    .balign 128
.endr
irq_entry:
    SAVE_FRAME
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #256]
    bl rt_interrupt_enter
    bl guest_irq_handler
    bl rt_interrupt_leave
    ldr x0, =irq_switch_pending
    ldr x1, [x0]
    cbz x1, 4f
    str xzr, [x0]
    ldr x0, =irq_switch_from
    ldr x0, [x0]
    mov x1, sp
    str x1, [x0]
    ldr x0, =irq_switch_to
    ldr x0, [x0]
    ldr x1, [x0]
    mov sp, x1
4:  clrex
    RESTORE_FRAME
exception_entry:
    SAVE_FRAME
    mrs x0, esr_el1
    mrs x1, elr_el1
    mrs x2, far_el1
    bl guest_exception
5:  wfi
    b 5b

.section .bss,"aw",%nobits
.balign 4096
boot_l1:
    .skip 4096
.global boot_l2
boot_l2:
    .skip 4096
