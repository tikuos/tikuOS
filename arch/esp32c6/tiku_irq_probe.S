/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_irq_probe.S - Register preservation and illegal-instruction probes for C6.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
    .section .text.irq_probe,"ax",@progbits
    .global native_irq_frame_probe
native_irq_frame_probe:
    addi sp,sp,-16
    sw s0,0(sp)
    sw s1,4(sp)
    li s0,0x1234abcd
    li s1,0x76543210
    li a2,0x10293847
    li a3,0x56473829
    li t5,0x99887766
    li t6,0x11223344
    csrr a1,0x7e2
1:
    csrr t0,0x7e2
    sub t0,t0,a1
    bltu t0,a0,1b
    li a0,-1
    li t0,0x1234abcd
    bne s0,t0,2f
    li t0,0x76543210
    bne s1,t0,2f
    li t0,0x10293847
    bne a2,t0,2f
    li t0,0x56473829
    bne a3,t0,2f
    li t0,0x99887766
    bne t5,t0,2f
    li t0,0x11223344
    bne t6,t0,2f
    li a0,0
2:
    lw s0,0(sp)
    lw s1,4(sp)
    addi sp,sp,16
    ret
    .global native_illegal_probe
native_illegal_probe:
    .global native_probe_illegal
native_probe_illegal:
    .4byte 0xffffffff
    .global native_probe_after_illegal
native_probe_after_illegal:
    ret
