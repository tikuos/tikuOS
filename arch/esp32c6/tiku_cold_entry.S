/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cold_entry.S - Flash-loaded C6 entry with a board WAKE-selected recovery path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
    .section .text.entry,"ax",@progbits
    .global c6_cold_entry
    .global __c6_recovery_entry
    .equ __c6_recovery_entry,C6_SDK_ENTRY
c6_cold_entry:
    .option push
    .option norelax
    li t0,0x6009000c
    lw t2,0(t0)
    ori t1,t2,640
    andi t1,t1,-257
    sw t1,0(t0)
    li t1,0x6009103c
    lw t1,0(t1)
    sw t2,0(t0)
    andi t1,t1,4
    bnez t1,c6_cold_recovery
    csrci mstatus,8
    csrw mie,zero
    li t0,0x1880
    csrs mstatus,t0
    li t0,0x40819f00
    li t1,0xc6c01d01
    sw t1,0(t0)
    li t1,1
    sw t1,4(t0)
    sw zero,8(t0)
    sw zero,12(t0)
    sw zero,16(t0)
    sw sp,20(t0)
    csrr t1,mtvec
    sw t1,28(t0)
    li t0,0x60096100
    lw t1,0(t0)
    ori t1,t1,1
    andi t1,t1,-3
    sw t1,0(t0)
    li t0,0x600960b0
    lw t1,0(t0)
    ori t1,t1,1
    andi t1,t1,-3
    sw t1,0(t0)
    li t0,0x40819f00
    li t1,0x600c2000
    lw t2,0(t1)
    sw t2,24(t0)
    andi t2,t2,-769
    sw t2,0(t1)
    la t1,c6_cold_trap
    csrw mtvec,t1
    li sp,0x40819f00
    addi sp,sp,-256
    mv a0,sp
    call c6_native_entry
1:  j 1b
c6_cold_recovery:
    li t0,C6_SDK_ENTRY
    jr t0
    .balign 4
c6_cold_trap:
    csrci mstatus,8
    li t0,0x40819f00
    csrr t1,mcause
    sw t1,8(t0)
    csrr t1,mepc
    sw t1,12(t0)
    csrr t1,mtval
    sw t1,16(t0)
2:  li t0,0x6009103c
    lw t1,0(t0)
    andi t1,t1,4
    beqz t1,2b
    li t0,0x40000090
    jr t0
    .option pop

#include "tiku_crt_early.S"
