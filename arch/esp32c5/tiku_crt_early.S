/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_crt_early.S - C5 RAM entry and non-nested machine trap frame.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_irq_arch.h"

    .section .text.entry,"ax",@progbits
    .global tiku_esp32c5_reset_handler
    .type tiku_esp32c5_reset_handler,@function
tiku_esp32c5_reset_handler:
    csrci mstatus, 8
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    la sp, __stack
    la t0, tiku_esp32c5_diagnostic_trap
    ori t0, t0, 3
    csrw mtvec, t0
    la t0, __bss_start
    la t1, __bss_end
1:
    bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:
    fence.i
#if defined(TIKU_MINIMAL)
    tail tiku_esp32c5_diagnostic
#else
    tail main
#endif
    .size tiku_esp32c5_reset_handler, .-tiku_esp32c5_reset_handler

    .section .text.trap,"ax",@progbits
    .balign 64
    .global tiku_esp32c5_diagnostic_trap
    .type tiku_esp32c5_diagnostic_trap,@function
tiku_esp32c5_diagnostic_trap:
    csrci mstatus, 8
    addi sp, sp, -TIKU_C5_FRAME_SIZE
    sw zero, 0(sp)
    .irp reg,1,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sw x\reg, (4*\reg)(sp)
    .endr
    addi t0, sp, TIKU_C5_FRAME_SIZE
    sw t0, 8(sp)
    csrr t0, mepc
    sw t0, TIKU_C5_FRAME_PC(sp)
    csrr t0, mstatus
    sw t0, TIKU_C5_FRAME_STATUS(sp)
    csrr t0, mcause
    sw t0, TIKU_C5_FRAME_CAUSE(sp)
    mv a0, sp
    .option push
    .option norelax
    la gp, __global_pointer$
    la sp, tiku_c5_isr_stack_end
    .option pop
    call tiku_c5_trap_dispatch
    mv sp, a0
    lw t0, TIKU_C5_FRAME_STATUS(sp)
    csrw mstatus, t0
    lw t0, TIKU_C5_FRAME_CAUSE(sp)
    csrw mcause, t0
    lw t0, TIKU_C5_FRAME_PC(sp)
    csrw mepc, t0
    .irp reg,1,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    lw x\reg, (4*\reg)(sp)
    .endr
    lw sp, 8(sp)
    mret
    .size tiku_esp32c5_diagnostic_trap, .-tiku_esp32c5_diagnostic_trap

    .section .bss.isr_stack,"aw",@nobits
    .balign 16
tiku_c5_isr_stack:
    .skip 2048
tiku_c5_isr_stack_end:
