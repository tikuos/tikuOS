/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_arch.S - CPU0 startup and integer/FPU worker context.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_runtime_arch.h"
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
    li t0, 0x6000
    csrs mstatus, t0
    csrw fcsr, zero
    fmv.w.x f0, zero
    fmv.w.x f1, zero
    fmv.w.x f2, zero
    fmv.w.x f3, zero
    fmv.w.x f4, zero
    fmv.w.x f5, zero
    fmv.w.x f6, zero
    fmv.w.x f7, zero
    fmv.w.x f8, zero
    fmv.w.x f9, zero
    fmv.w.x f10, zero
    fmv.w.x f11, zero
    fmv.w.x f12, zero
    fmv.w.x f13, zero
    fmv.w.x f14, zero
    fmv.w.x f15, zero
    fmv.w.x f16, zero
    fmv.w.x f17, zero
    fmv.w.x f18, zero
    fmv.w.x f19, zero
    fmv.w.x f20, zero
    fmv.w.x f21, zero
    fmv.w.x f22, zero
    fmv.w.x f23, zero
    fmv.w.x f24, zero
    fmv.w.x f25, zero
    fmv.w.x f26, zero
    fmv.w.x f27, zero
    fmv.w.x f28, zero
    fmv.w.x f29, zero
    fmv.w.x f30, zero
    fmv.w.x f31, zero
    fence.i
    call main
.Lpark:
    wfi
    j .Lpark
    .section .text.trap,"ax",@progbits
    .balign 64
.Ltrap:
    csrci mstatus, 8
    addi sp, sp, -S31_FRAME_SIZE
    sw x1, 4(sp)
    sw x3, 12(sp)
    sw x4, 16(sp)
    sw x5, 20(sp)
    sw x6, 24(sp)
    sw x7, 28(sp)
    sw x8, 32(sp)
    sw x9, 36(sp)
    sw x10, 40(sp)
    sw x11, 44(sp)
    sw x12, 48(sp)
    sw x13, 52(sp)
    sw x14, 56(sp)
    sw x15, 60(sp)
    sw x16, 64(sp)
    sw x17, 68(sp)
    sw x18, 72(sp)
    sw x19, 76(sp)
    sw x20, 80(sp)
    sw x21, 84(sp)
    sw x22, 88(sp)
    sw x23, 92(sp)
    sw x24, 96(sp)
    sw x25, 100(sp)
    sw x26, 104(sp)
    sw x27, 108(sp)
    sw x28, 112(sp)
    sw x29, 116(sp)
    sw x30, 120(sp)
    sw x31, 124(sp)
    addi t0, sp, S31_FRAME_SIZE
    sw t0, 8(sp)
    csrr t0, mepc
    sw t0, S31_FRAME_PC(sp)
    csrr t0, mstatus
    sw t0, S31_FRAME_STATUS(sp)
    csrr t0, mcause
    sw t0, S31_FRAME_CAUSE(sp)
    csrr t0, fcsr
    sw t0, S31_FRAME_FCSR(sp)
    fsw f0, (S31_FRAME_F0 + 0)(sp)
    fsw f1, (S31_FRAME_F0 + 4)(sp)
    fsw f2, (S31_FRAME_F0 + 8)(sp)
    fsw f3, (S31_FRAME_F0 + 12)(sp)
    fsw f4, (S31_FRAME_F0 + 16)(sp)
    fsw f5, (S31_FRAME_F0 + 20)(sp)
    fsw f6, (S31_FRAME_F0 + 24)(sp)
    fsw f7, (S31_FRAME_F0 + 28)(sp)
    fsw f8, (S31_FRAME_F0 + 32)(sp)
    fsw f9, (S31_FRAME_F0 + 36)(sp)
    fsw f10, (S31_FRAME_F0 + 40)(sp)
    fsw f11, (S31_FRAME_F0 + 44)(sp)
    fsw f12, (S31_FRAME_F0 + 48)(sp)
    fsw f13, (S31_FRAME_F0 + 52)(sp)
    fsw f14, (S31_FRAME_F0 + 56)(sp)
    fsw f15, (S31_FRAME_F0 + 60)(sp)
    fsw f16, (S31_FRAME_F0 + 64)(sp)
    fsw f17, (S31_FRAME_F0 + 68)(sp)
    fsw f18, (S31_FRAME_F0 + 72)(sp)
    fsw f19, (S31_FRAME_F0 + 76)(sp)
    fsw f20, (S31_FRAME_F0 + 80)(sp)
    fsw f21, (S31_FRAME_F0 + 84)(sp)
    fsw f22, (S31_FRAME_F0 + 88)(sp)
    fsw f23, (S31_FRAME_F0 + 92)(sp)
    fsw f24, (S31_FRAME_F0 + 96)(sp)
    fsw f25, (S31_FRAME_F0 + 100)(sp)
    fsw f26, (S31_FRAME_F0 + 104)(sp)
    fsw f27, (S31_FRAME_F0 + 108)(sp)
    fsw f28, (S31_FRAME_F0 + 112)(sp)
    fsw f29, (S31_FRAME_F0 + 116)(sp)
    fsw f30, (S31_FRAME_F0 + 120)(sp)
    fsw f31, (S31_FRAME_F0 + 124)(sp)
    mv a0, sp
    call tiku_s31_trap
    mv sp, a0
    flw f0, (S31_FRAME_F0 + 0)(sp)
    flw f1, (S31_FRAME_F0 + 4)(sp)
    flw f2, (S31_FRAME_F0 + 8)(sp)
    flw f3, (S31_FRAME_F0 + 12)(sp)
    flw f4, (S31_FRAME_F0 + 16)(sp)
    flw f5, (S31_FRAME_F0 + 20)(sp)
    flw f6, (S31_FRAME_F0 + 24)(sp)
    flw f7, (S31_FRAME_F0 + 28)(sp)
    flw f8, (S31_FRAME_F0 + 32)(sp)
    flw f9, (S31_FRAME_F0 + 36)(sp)
    flw f10, (S31_FRAME_F0 + 40)(sp)
    flw f11, (S31_FRAME_F0 + 44)(sp)
    flw f12, (S31_FRAME_F0 + 48)(sp)
    flw f13, (S31_FRAME_F0 + 52)(sp)
    flw f14, (S31_FRAME_F0 + 56)(sp)
    flw f15, (S31_FRAME_F0 + 60)(sp)
    flw f16, (S31_FRAME_F0 + 64)(sp)
    flw f17, (S31_FRAME_F0 + 68)(sp)
    flw f18, (S31_FRAME_F0 + 72)(sp)
    flw f19, (S31_FRAME_F0 + 76)(sp)
    flw f20, (S31_FRAME_F0 + 80)(sp)
    flw f21, (S31_FRAME_F0 + 84)(sp)
    flw f22, (S31_FRAME_F0 + 88)(sp)
    flw f23, (S31_FRAME_F0 + 92)(sp)
    flw f24, (S31_FRAME_F0 + 96)(sp)
    flw f25, (S31_FRAME_F0 + 100)(sp)
    flw f26, (S31_FRAME_F0 + 104)(sp)
    flw f27, (S31_FRAME_F0 + 108)(sp)
    flw f28, (S31_FRAME_F0 + 112)(sp)
    flw f29, (S31_FRAME_F0 + 116)(sp)
    flw f30, (S31_FRAME_F0 + 120)(sp)
    flw f31, (S31_FRAME_F0 + 124)(sp)
    lw t0, S31_FRAME_FCSR(sp)
    csrw fcsr, t0
    lw t0, S31_FRAME_PC(sp)
    csrw mepc, t0
    lw t0, S31_FRAME_CAUSE(sp)
    csrw mcause, t0
    lw t0, S31_FRAME_STATUS(sp)
    csrw mstatus, t0
    lw x1, 4(sp)
    lw x3, 12(sp)
    lw x4, 16(sp)
    lw x5, 20(sp)
    lw x6, 24(sp)
    lw x7, 28(sp)
    lw x8, 32(sp)
    lw x9, 36(sp)
    lw x10, 40(sp)
    lw x11, 44(sp)
    lw x12, 48(sp)
    lw x13, 52(sp)
    lw x14, 56(sp)
    lw x15, 60(sp)
    lw x16, 64(sp)
    lw x17, 68(sp)
    lw x18, 72(sp)
    lw x19, 76(sp)
    lw x20, 80(sp)
    lw x21, 84(sp)
    lw x22, 88(sp)
    lw x23, 92(sp)
    lw x24, 96(sp)
    lw x25, 100(sp)
    lw x26, 104(sp)
    lw x27, 108(sp)
    lw x28, 112(sp)
    lw x29, 116(sp)
    lw x30, 120(sp)
    lw x31, 124(sp)
    lw sp, 8(sp)
    mret

    .section .text.probe,"ax",@progbits
    .global tiku_s31_fpu_probe
tiku_s31_fpu_probe:
    addi sp, sp, -16
    fsw fs0, 0(sp)
    csrr t0, fcsr
    sw t0, 4(sp)
    li t0, 0x3f123456
    fmv.w.x ft0, t0
    li t0, 0x40123456
    fmv.w.x fs0, t0
    li t0, 0x21
    csrw fcsr, t0
    ecall
    li a0, 1
    fmv.x.w t0, ft0
    li t1, 0x3f123456
    bne t0, t1, .Ldone
    fmv.x.w t0, fs0
    li t1, 0x40123456
    bne t0, t1, .Ldone
    csrr t0, fcsr
    li t1, 0x21
    bne t0, t1, .Ldone
    li a0, 0
.Ldone:
    flw fs0, 0(sp)
    lw t0, 4(sp)
    csrw fcsr, t0
    addi sp, sp, 16
    ret
