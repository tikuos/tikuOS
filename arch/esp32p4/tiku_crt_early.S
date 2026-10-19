/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.S - P4 RAM entry and non-nested machine trap frame.
 *
 * CPU0 zeroes .bss and, outside the diagnostic, parks the other cores and
 * sets the L2 layout before main; other harts wait in WFI.  The trap entry
 * checks the interrupted SP, then dispatches interrupts or records faults.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_irq_arch.h"
#include "tiku_fault_arch.h"

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
    li t0, 0x6600
    csrc mstatus, t0
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
    mv a0, sp
    .option push
    .option norelax
    la gp, __global_pointer$
    la sp, tiku_p4_isr_stack_end
    .option pop
    call tiku_p4_trap_dispatch
    mv sp, a0
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
    .global tiku_p4_fault_stack
tiku_p4_fault_stack:
    .skip 2048
    .global tiku_p4_fault_stack_end
tiku_p4_fault_stack_end:
