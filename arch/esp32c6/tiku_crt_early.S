/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.S - C6 RAM entry, context restoration and machine trap vectors.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
    .section .text.entry,"ax",@progbits
    .global c6_native_entry
    .type c6_native_entry,@function
c6_native_entry:
    csrrci t0,mstatus,8
    addi sp,sp,-80
    sw ra,0(sp)
    sw gp,4(sp)
    sw tp,8(sp)
    sw s0,12(sp)
    sw s1,16(sp)
    sw s2,20(sp)
    sw s3,24(sp)
    sw s4,28(sp)
    sw s5,32(sp)
    sw s6,36(sp)
    sw s7,40(sp)
    sw s8,44(sp)
    sw s9,48(sp)
    sw s10,52(sp)
    sw s11,56(sp)
    sw t0,60(sp)
    csrr t1,mtvec
    sw t1,64(sp)
    li t0,0x6001806c
    li t1,0xc6000001
    sw t1,0(t0)
    mv s11,sp
    mv s0,a0
    .option push
    .option norelax
    la gp,__global_pointer$
    la t0,__bss_start
    la t1,__bss_end
1:
    bgeu t0,t1,2f
    sw zero,0(t0)
    addi t0,t0,4
    j 1b
2:
    la t0,__native_irq_bottom
    la t1,__native_irq_top
    li t2,0xa5a5a5a5
3:
    bgeu t0,t1,4f
    sw t2,0(t0)
    addi t0,t0,4
    j 3b
4:
    la t0,__native_stack_bottom
    la t1,__native_stack_top
5:
    bgeu t0,t1,6f
    sw t2,0(t0)
    addi t0,t0,4
    j 5b
6:
    la t0,native_saved_context
    sw s11,0(t0)
    la sp,__native_stack_top
    la t0,c6_native_vectors
    ori t0,t0,1
    csrw mtvec,t0
    .option pop
    fence.i
    li t0,0x6001806c
    li t1,0xc6000002
    sw t1,0(t0)
    mv a0,s0
    call native_main
native_return:
    csrci mstatus,8
    mv sp,s11
    lw t0,64(sp)
    csrw mtvec,t0
    lw ra,0(sp)
    lw gp,4(sp)
    lw tp,8(sp)
    lw s0,12(sp)
    lw s1,16(sp)
    lw s2,20(sp)
    lw s3,24(sp)
    lw s4,28(sp)
    lw s5,32(sp)
    lw s6,36(sp)
    lw s7,40(sp)
    lw s8,44(sp)
    lw s9,48(sp)
    lw s10,52(sp)
    lw s11,56(sp)
    lw t0,60(sp)
    addi sp,sp,80
    csrw mstatus,t0
    ret

    .global native_abort
native_abort:
    csrci mstatus,8
    la t0,native_saved_context
    lw s11,0(t0)
    la sp,__native_stack_top
    call native_hardware_restore
    li a0,0
    j native_return

    .section .text.vectors,"ax",@progbits
    .balign 256
    .global c6_native_vectors
c6_native_vectors:
    .option push
    .option norvc
    .rept 32
    j native_trap
    .endr
    .option pop

    .section .text.trap,"ax",@progbits
    .balign 16
native_trap:
    csrci mstatus,8
    addi sp,sp,-144
    sw zero,0(sp)
    .irp r,1,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sw x\r,(4*\r)(sp)
    .endr
    addi t0,sp,144
    sw t0,8(sp)
    csrr t0,mepc
    sw t0,128(sp)
    csrr t0,mstatus
    sw t0,132(sp)
    csrr t0,mcause
    sw t0,136(sp)
    csrr t0,mtval
    sw t0,140(sp)
    mv a0,sp
    la gp,__global_pointer$
    la sp,__native_irq_top
    call native_trap_dispatch
    mv sp,a0
    lw t0,128(sp)
    csrw mepc,t0
    lw t0,132(sp)
    csrw mstatus,t0
    .irp r,1,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    lw x\r,(4*\r)(sp)
    .endr
    lw sp,8(sp)
    mret
