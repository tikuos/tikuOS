/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_crt0.S - FLPR (VPR RISC-V) startup for every nRF54L part.
 *
 * The M33 loader memcpy's the whole image into the SRAM carve before setting
 * CPURUN, so there is no .data copy here: stack, global pointer, .bss, main().
 * INITPC points at the carve base and .init is placed first, so _start leads.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Interrupts stay disabled: the coprocessor runs fully polled, and mtvec
 * serves only the trap stub below.
 */

    .section .init, "ax"
    .globl _start
    .type _start, @function
_start:
    /* Global pointer: must load without relaxation (the relaxed form
     * would itself use gp before it is set). */
    .option push
    .option norelax
    la   gp, __global_pointer$
    .option pop

    la   sp, __stack_top

    /* Trap vector, direct mode: an unset mtvec sends a trap to whatever
     * address it reset to.  The stub below reloads sp, since a trap may
     * arrive with sp corrupt, calls tiku_flpr_fault() and never returns. */
    la   t0, flpr_trap
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
    call tiku_flpr_main

    /* main never returns; if it does, park quietly. */
3:
    wfi
    j    3b
    .size _start, .-_start

    .align 2
    .globl flpr_trap
    .type flpr_trap, @function
flpr_trap:
    la   sp, __stack_top
    call tiku_flpr_fault
1:
    wfi
    j    1b
    .size flpr_trap, .-flpr_trap

