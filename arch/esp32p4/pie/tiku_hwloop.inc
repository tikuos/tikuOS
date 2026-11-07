/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_hwloop.inc - P4 xespv2p1 register-count hardware-loop setup.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Requires pie_xnum from tiku_pie.inc. The end labels the last loop
 * instruction, at an even forward displacement of at most 8190 bytes.
 * The caller enables CSR 0x7F1, supplies a count in 1..4095 on v1.x and
 * disables linker relaxation across the loop with .option norelax. */
.macro tiku_p4_lp_setup slot, count, end
    .if (\slot != 0) && (\slot != 1)
        .error "hardware-loop slot must be 0 or 1"
    .endif
    pie_xnum .Lhwlp_reg\@, \count
    .word 0x0000402b | (\slot << 7) | (.Lhwlp_reg\@ << 15) | ((\end - .) << 19)
.endm
