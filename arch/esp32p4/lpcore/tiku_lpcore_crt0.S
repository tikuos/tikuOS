/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lpcore_crt0.S - P4 LP core vectors and reset entry.
 *
 * The HP loader copies the whole image into LP SRAM before the launch, so
 * there is no .data copy here: global pointer, stack, mtvec, .bss, main().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Interrupts stay disabled: the payload runs polled.  mtvec holds the table
 * base with mode 0, so every trap enters slot 0; the other slots only make
 * the table the 128 bytes the reset entry follows.  Each slot is a four-byte
 * jump, so the table is assembled without compressed instructions.
 */

    .section .vectors, "ax"
    .globl tiku_lpcore_vectors
    .type tiku_lpcore_vectors, @function
    .option push
    .option norvc
tiku_lpcore_vectors:
    .rept 32
    j    lpcore_trap
    .endr
    .option pop
    .size tiku_lpcore_vectors, .-tiku_lpcore_vectors

    .section .init, "ax"
    .globl _start
    .type _start, @function
_start:
    /* Global pointer: must load without relaxation (the relaxed form would
     * itself use gp before it is set). */
    .option push
    .option norelax
    la   gp, __global_pointer$
    .option pop

    la   sp, __stack_top
    la   t0, tiku_lpcore_vectors
    csrw mtvec, t0

    /* Zero .bss (word-aligned by the linker script). */
    la   t0, __bss_start
    la   t1, __bss_end
1:
    bgeu t0, t1, 2f
    sw   zero, 0(t0)
    addi t0, t0, 4
    j    1b
2:
    call tiku_lpcore_main

    /* main never returns; if it does, spin. */
3:
    j    3b
    .size _start, .-_start

    .align 2
    .type lpcore_trap, @function
lpcore_trap:
    /* A trap may arrive with sp corrupt. */
    la   sp, __stack_top
    call tiku_lpcore_fault
1:
    j    1b
    .size lpcore_trap, .-lpcore_trap
