/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_irq_probe.S - Register preservation check across live C5 interrupts.
 * a0 is the cycle duration; the result is zero on success or one on mismatch.
 * SPDX-License-Identifier: Apache-2.0
 */
    .section .text.tiku_c5_irq_probe,"ax",@progbits
    .global tiku_c5_irq_probe
    .type tiku_c5_irq_probe,@function
tiku_c5_irq_probe:
    addi sp, sp, -64
    sw ra, 0(sp)
    sw gp, 4(sp)
    sw tp, 8(sp)
    sw s0, 12(sp)
    sw s1, 16(sp)
    .irp reg,18,19,20,21,22,23,24,25,26,27
    sw x\reg, (20+4*(\reg-18))(sp)
    .endr
    mv x6, a0
    .irp reg,1,3,4,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    li x\reg, (\reg*0x1111)
    .endr
    csrr x5, mcycle
1:
    csrr x7, mcycle
    sub x7, x7, x5
    bltu x7, x6, 1b
    .irp reg,1,3,4,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    li x5, (\reg*0x1111)
    bne x\reg, x5, 2f
    .endr
    li a0, 0
    j 3f
2:
    li a0, 1
3:
    lw ra, 0(sp)
    lw gp, 4(sp)
    lw tp, 8(sp)
    lw s0, 12(sp)
    lw s1, 16(sp)
    .irp reg,18,19,20,21,22,23,24,25,26,27
    lw x\reg, (20+4*(\reg-18))(sp)
    .endr
    addi sp, sp, 64
    ret
    .size tiku_c5_irq_probe, .-tiku_c5_irq_probe
