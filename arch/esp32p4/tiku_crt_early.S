/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.S - P4 RAM entry and non-nested machine trap frame.
 *
 * CPU0 zeroes .bss, parks the other cores outside the diagnostic and runs
 * main; other harts wait in WFI.  The trap entry checks the interrupted SP,
 * saves the FPU and PIE state a context dirtied, then dispatches or faults.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_irq_arch.h"
#include "tiku_fault_arch.h"
#include "pie/tiku_pie.inc"

    .section .text.entry,"ax",@progbits
    .global tiku_esp32p4_reset_handler
    .type tiku_esp32p4_reset_handler,@function
tiku_esp32p4_reset_handler:
    csrci mstatus, 8
    csrr t0, mhartid
    bnez t0, .Lpark
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
#if defined(TIKU_P4_KERNEL_TEST) || defined(TIKU_P4_FIRMWARE)
    la sp, tiku_p4_isr_stack_end
#else
    la sp, __stack
#endif
    la t0, tiku_p4_trap_scratch
    sw zero, 16(t0)
    csrw mscratch, t0
    la t0, tiku_esp32p4_diagnostic_trap
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
    /* Vector state off; the FPU on, its registers untouched, rounding to
     * nearest.  Threads start the same way (tiku_thread_arch_frame_init). */
    li t0, 0x6600
    csrc mstatus, t0
    li t0, TIKU_P4_MSTATUS_FS_INITIAL
    csrs mstatus, t0
    csrw fcsr, zero
    /* The fcsr write marked the state dirty; back to initial. */
    li t0, TIKU_P4_MSTATUS_FS
    csrc mstatus, t0
    li t0, TIKU_P4_MSTATUS_FS_INITIAL
    csrs mstatus, t0
    /* The PIE on, its state initial.  Threads start the same way. */
    li t0, 1
    csrw TIKU_P4_PIE_STATE_CSR, t0
#if defined(TIKU_P4_KERNEL_TEST) || defined(TIKU_P4_FIRMWARE)
    call tiku_esp32p4_boot_park_cores
    call tiku_p4_cache_boot_init
    bnez a0, .Lpark
    la sp, __stack
#endif
    fence.i
#if defined(TIKU_MINIMAL)
    tail tiku_esp32p4_diagnostic
#else
    tail main
#endif
.Lpark:
    wfi
    j .Lpark
    .size tiku_esp32p4_reset_handler, .-tiku_esp32p4_reset_handler

    .section .text.trap,"ax",@progbits
    .balign 64
    .global tiku_esp32p4_diagnostic_trap
    .type tiku_esp32p4_diagnostic_trap,@function
tiku_esp32p4_diagnostic_trap:
    csrci mstatus, 8
    /* mscratch addresses saved t0/t1/t2/SP and phase: foreground, IRQ, fault.
     * The interrupted stack is not writable until its range is checked.
     */
    csrrw t0, mscratch, t0
    sw t1, 4(t0)
    sw t2, 8(t0)
    sw sp, 12(t0)
    csrr t1, mscratch
    sw t1, 0(t0)
    csrw mscratch, t0
    lw t1, 16(t0)
    li t2, 2
    bgeu t1, t2, .Lpark
    andi t2, sp, 15
    bnez t2, .Lbad_sp
    la t2, __bss_start
    addi t2, t2, TIKU_P4_FRAME_SIZE
    bltu sp, t2, .Lbad_sp
    la t2, __bss_end
    bgeu t2, sp, .Lsp_ok
    la t2, __stack_bottom
    addi t2, t2, TIKU_P4_FRAME_SIZE
    bltu sp, t2, .Lbad_sp
    la t2, __stack
    bltu t2, sp, .Lbad_sp
.Lsp_ok:
    csrr t2, mcause
    bgez t2, .Lexception
    bnez t1, .Lexception
    li t1, 1
    sw t1, 16(t0)
    addi sp, sp, -TIKU_P4_FRAME_SIZE
    sw zero, 0(sp)
    .irp reg,1,3,4,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sw x\reg, (4*\reg)(sp)
    .endr
    lw t1, 0(t0)
    sw t1, 20(sp)
    lw t1, 4(t0)
    sw t1, 24(sp)
    lw t1, 8(t0)
    sw t1, 28(sp)
    addi t0, sp, TIKU_P4_FRAME_SIZE
    sw t0, 8(sp)
    csrr t0, mepc
    sw t0, TIKU_P4_FRAME_PC(sp)
    csrr t0, mstatus
    sw t0, TIKU_P4_FRAME_STATUS(sp)
    csrr t0, mcause
    sw t0, TIKU_P4_FRAME_CAUSE(sp)
    /* A context that wrote the FPU (FS dirty) keeps its registers and
     * fcsr in the frame; one that never did skips the 33 stores. */
    sw zero, TIKU_P4_FRAME_FPU(sp)
    csrr t0, mstatus
    li t1, TIKU_P4_MSTATUS_FS
    and t0, t0, t1
    bne t0, t1, 4f
    .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fsw f\reg, (TIKU_P4_FRAME_F0+4*\reg)(sp)
    .endr
    csrr t0, fcsr
    sw t0, TIKU_P4_FRAME_FCSR(sp)
    li t0, 1
    sw t0, TIKU_P4_FRAME_FPU(sp)
4:
    /* The PIE's state CSR is kept for the write-back; a context that wrote
     * the vector unit (state dirty) keeps q0-q7, the accumulators, ua_state,
     * xacc, sar, sar_bytes and fft_bit_width in the frame too.  The 128-bit
     * stores need a base register from x8-x15 or x24-x31: t3, saved above. */
    sw zero, TIKU_P4_FRAME_PIE(sp)
    csrr t0, TIKU_P4_PIE_STATE_CSR
    sw t0, TIKU_P4_FRAME_PIE_STATE(sp)
    andi t0, t0, 3
    addi t0, t0, -3
    bnez t0, 7f
    addi t3, sp, TIKU_P4_FRAME_PIE_REGS
    esp_vst_128_ip q0, t3, 16
    esp_vst_128_ip q1, t3, 16
    esp_vst_128_ip q2, t3, 16
    esp_vst_128_ip q3, t3, 16
    esp_vst_128_ip q4, t3, 16
    esp_vst_128_ip q5, t3, 16
    esp_vst_128_ip q6, t3, 16
    esp_vst_128_ip q7, t3, 16
    esp_st_qacc_l_l_128_ip t3, 16
    esp_st_qacc_l_h_128_ip t3, 16
    esp_st_qacc_h_l_128_ip t3, 16
    esp_st_qacc_h_h_128_ip t3, 16
    esp_st_ua_state_ip t3, 16
    esp_st_u_xacc_ip t3, 0
    esp_movx_r_sar_bytes a1
    slli a2, a1, 4
    esp_movx_r_fft_bit_width a1
    or a2, a2, a1
    sb a2, 5(t3)
    esp_movx_r_sar a1
    sb a1, 6(t3)
    li t0, 1
    sw t0, TIKU_P4_FRAME_PIE(sp)
7:
    mv a0, sp
    .option push
    .option norelax
    la gp, __global_pointer$
    la sp, tiku_p4_isr_stack_end
    .option pop
    call tiku_p4_trap_dispatch
    mv sp, a0
    /* The PIE registers come back for a context that had them saved; its
     * state CSR comes back for every context, so one that never wrote the
     * vector unit stays initial and the next entry skips the save. */
    lw t0, TIKU_P4_FRAME_PIE(sp)
    beqz t0, 8f
    addi t3, sp, TIKU_P4_FRAME_PIE_REGS
    esp_vld_128_ip q0, t3, 16
    esp_vld_128_ip q1, t3, 16
    esp_vld_128_ip q2, t3, 16
    esp_vld_128_ip q3, t3, 16
    esp_vld_128_ip q4, t3, 16
    esp_vld_128_ip q5, t3, 16
    esp_vld_128_ip q6, t3, 16
    esp_vld_128_ip q7, t3, 16
    esp_ld_qacc_l_l_128_ip t3, 16
    esp_ld_qacc_l_h_128_ip t3, 16
    esp_ld_qacc_h_l_128_ip t3, 16
    esp_ld_qacc_h_h_128_ip t3, 16
    esp_ld_ua_state_ip t3, 16
    esp_ld_xacc_ip t3, 0
    lbu a1, 5(t3)
    esp_movx_w_fft_bit_width a1
    srli a1, a1, 4
    esp_movx_w_sar_bytes a1
    lbu a1, 6(t3)
    esp_movx_w_sar a1
8:
    lw t0, TIKU_P4_FRAME_PIE_STATE(sp)
    csrw TIKU_P4_PIE_STATE_CSR, t0
    /* A context with no saved FPU state never set fcsr: the interrupt may
     * have, so it goes back to its reset value.  Either write marks the state
     * dirty; the mstatus restore below puts the context's own FS back. */
    lw t0, TIKU_P4_FRAME_FPU(sp)
    beqz t0, 5f
    lw t0, TIKU_P4_FRAME_FCSR(sp)
    csrw fcsr, t0
    .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    flw f\reg, (TIKU_P4_FRAME_F0+4*\reg)(sp)
    .endr
    j 6f
5:
    csrw fcsr, zero
6:
    lw t0, TIKU_P4_FRAME_STATUS(sp)
    csrw mstatus, t0
    lw t0, TIKU_P4_FRAME_CAUSE(sp)
    csrw mcause, t0
    lw t0, TIKU_P4_FRAME_PC(sp)
    csrw mepc, t0
    csrr t0, mscratch
    sw zero, 16(t0)
    .irp reg,1,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    lw x\reg, (4*\reg)(sp)
    .endr
    lw sp, 8(sp)
    mret
.Lbad_sp:
    li a7, TIKU_P4_FAULT_BAD_SP
    j .Lfault
.Lexception:
    /* A store access fault (code 7) inside the durable span is an MPU
     * violation: latched in tiku_p4_mpu_latch (flags, count, address), the
     * store skipped by its length, the context resumed.  Only t0-t2 are
     * free here; the scratch area holds their values. */
    slli t2, t2, 20
    srli t2, t2, 20
    addi t2, t2, -7
    bnez t2, .Lcapture
    csrr t2, mtval
    la t1, tiku_p4_mpu_latch
    lw t0, 0(t1)
    bltu t2, t0, .Lcapture
    lw t0, 4(t1)
    bgeu t2, t0, .Lcapture
    sw t2, 16(t1)
    lw t0, 8(t1)
    ori t0, t0, 1
    sw t0, 8(t1)
    lw t0, 12(t1)
    addi t0, t0, 1
    sw t0, 12(t1)
    csrr t0, mepc
    lhu t2, 0(t0)
    andi t2, t2, 3
    addi t0, t0, 2
    li t1, 3
    bne t2, t1, .Lskipped
    addi t0, t0, 2
.Lskipped:
    csrw mepc, t0
    csrr t0, mscratch
    lw t1, 4(t0)
    lw t2, 8(t0)
    lw sp, 12(t0)
    lw t0, 0(t0)
    mret
.Lcapture:
    csrr t0, mscratch
    lw t1, 16(t0)
    li a7, 0
.Lfault:
    beqz t1, 3f
    ori a7, a7, TIKU_P4_FAULT_IN_IRQ
3:
    li t1, 2
    sw t1, 16(t0)
    csrr a0, mcause
    csrr a1, mepc
    csrr a2, mtval
    lw a3, 12(t0)
    csrr a4, mstatus
    csrr a5, mhartid
    mv a6, ra
    .option push
    .option norelax
    la gp, __global_pointer$
    la sp, tiku_p4_fault_stack_end
    .option pop
    call tiku_p4_fault_capture
    j .Lpark
    .size tiku_esp32p4_diagnostic_trap, .-tiku_esp32p4_diagnostic_trap

    .section .bss.isr_stack,"aw",@nobits
    .balign 16
    .global tiku_p4_isr_guard
tiku_p4_isr_guard:
    .skip 64
    .global tiku_p4_isr_stack
tiku_p4_isr_stack:
    .skip 2048
    .global tiku_p4_isr_stack_end
tiku_p4_isr_stack_end:

    .section .bss.fault_stack,"aw",@nobits
    .balign 16
    .global tiku_p4_trap_scratch
tiku_p4_trap_scratch:
    .skip 32
    /* Durable-span store faults: span start, end, flags, count, address. */
    .global tiku_p4_mpu_latch
tiku_p4_mpu_latch:
    .skip 20
    .balign 16
    .global tiku_p4_fault_stack
tiku_p4_fault_stack:
    .skip 2048
    .global tiku_p4_fault_stack_end
tiku_p4_fault_stack_end:
