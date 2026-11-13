/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * diagnostic_start.S - SRAM entry and non-nested integer trap context.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
    .section .text.entry,"ax",@progbits
    .global tiku_s31_diagnostic_entry
tiku_s31_diagnostic_entry:
    csrci mstatus, 8
    csrr t0, mhartid
    bnez t0, .Lpark
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    la sp, __stack
    la t0, .Ltrap
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
    call tiku_s31_diagnostic
.Lpark:
    wfi
    j .Lpark

    .section .text.trap,"ax",@progbits
    .balign 64
.Ltrap:
    csrci mstatus, 8
    addi sp, sp, -144
    sw ra, 4(sp)
    sw gp, 12(sp)
    sw tp, 16(sp)
    sw t0, 20(sp)
    sw t1, 24(sp)
    sw t2, 28(sp)
    sw s0, 32(sp)
    sw s1, 36(sp)
    sw a0, 40(sp)
    sw a1, 44(sp)
    sw a2, 48(sp)
    sw a3, 52(sp)
    sw a4, 56(sp)
    sw a5, 60(sp)
    sw a6, 64(sp)
    sw a7, 68(sp)
    sw s2, 72(sp)
    sw s3, 76(sp)
    sw s4, 80(sp)
    sw s5, 84(sp)
    sw s6, 88(sp)
    sw s7, 92(sp)
    sw s8, 96(sp)
    sw s9, 100(sp)
    sw s10, 104(sp)
    sw s11, 108(sp)
    sw t3, 112(sp)
    sw t4, 116(sp)
    sw t5, 120(sp)
    sw t6, 124(sp)
    csrr a0, mcause
    csrr a1, mepc
    csrr a2, mtval
    sw a0, 128(sp)
    csrr t0, mstatus
    sw t0, 132(sp)
    call tiku_s31_diagnostic_trap
    csrw mepc, a0
    lw t0, 128(sp)
    csrw mcause, t0
    lw t0, 132(sp)
    csrw mstatus, t0
    lw ra, 4(sp)
    lw gp, 12(sp)
    lw tp, 16(sp)
    lw t0, 20(sp)
    lw t1, 24(sp)
    lw t2, 28(sp)
    lw s0, 32(sp)
    lw s1, 36(sp)
    lw a0, 40(sp)
    lw a1, 44(sp)
    lw a2, 48(sp)
    lw a3, 52(sp)
    lw a4, 56(sp)
    lw a5, 60(sp)
    lw a6, 64(sp)
    lw a7, 68(sp)
    lw s2, 72(sp)
    lw s3, 76(sp)
    lw s4, 80(sp)
    lw s5, 84(sp)
    lw s6, 88(sp)
    lw s7, 92(sp)
    lw s8, 96(sp)
    lw s9, 100(sp)
    lw s10, 104(sp)
    lw s11, 108(sp)
    lw t3, 112(sp)
    lw t4, 116(sp)
    lw t5, 120(sp)
    lw t6, 124(sp)
    addi sp, sp, 144
    mret

    .section .text.probe,"ax",@progbits
    .global tiku_s31_context_probe
tiku_s31_context_probe:
    addi sp, sp, -16
    sw s0, 0(sp)
    li a0, 0x12345678
    li t0, 0x87654321
    li t6, 0x76543210
    li s0, 0x55aa55aa
    ecall
    li t1, 0x12345678
    bne a0, t1, .Lprobe_fail
    li t1, 0x87654321
    bne t0, t1, .Lprobe_fail
    li t1, 0x76543210
    bne t6, t1, .Lprobe_fail
    li t1, 0x55aa55aa
    bne s0, t1, .Lprobe_fail
    li a0, 0
    j .Lprobe_done
.Lprobe_fail:
    li a0, 1
.Lprobe_done:
    lw s0, 0(sp)
    addi sp, sp, 16
    ret
