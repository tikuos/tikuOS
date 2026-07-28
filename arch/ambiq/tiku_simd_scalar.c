/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_simd_scalar.c - a scalar twin of every hal/tiku_simd.c kernel, in the
 *                      same firmware image as the Helium originals.
 *
 * WHY THIS FILE EXISTS.  hal/tiku_simd.c holds both backends but selects one at
 * COMPILE time, so on this Cortex-M55 the scalar paths are compiled out.
 * Measuring Helium against scalar would then need two firmware images -- and
 * comparing across builds on this part is exactly what experiment 1 warned
 * about: ~3 % run-to-run variance between boots (F11), and a loop-alignment
 * hazard that moved a busy-current figure by 956 uA on the other platform
 * purely because an unrelated build flag shifted the code.
 *
 * So instead this translation unit forces TIKU_SIMD_MVE to 0, renames the
 * eleven public symbols, and includes the original source.  The result is a
 * scalar twin that is:
 *
 *   - the SAME SOURCE, so the two paths cannot drift apart, and their
 *     bit-for-bit agreement is a property of the build rather than a hope;
 *   - in the SAME IMAGE, so both are measured at one build's alignment, in one
 *     boot, against one basis.
 *
 * There is no #include of a .c anywhere else in this tree, and it is not a
 * pattern to copy casually -- it is justified here by the measurement
 * requirement above, and confined to a file that exists only for measurement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Force the scalar backend regardless of what the target can do. */
#define TIKU_SIMD_MVE 0

/* Rename every public symbol (the internal helpers are static already). */
#define tiku_simd_backend       tiku_simd_scalar_backend
#define tiku_simd_fill_u8       tiku_simd_scalar_fill_u8
#define tiku_simd_copy_u8       tiku_simd_scalar_copy_u8
#define tiku_simd_add_sat_u8    tiku_simd_scalar_add_sat_u8
#define tiku_simd_multiply_u8   tiku_simd_scalar_multiply_u8
#define tiku_simd_scale_u8      tiku_simd_scalar_scale_u8
#define tiku_simd_affine_u8     tiku_simd_scalar_affine_u8
#define tiku_simd_saxpy_u8      tiku_simd_scalar_saxpy_u8
#define tiku_simd_sum_u8        tiku_simd_scalar_sum_u8
#define tiku_simd_dot_u8        tiku_simd_scalar_dot_u8
#define tiku_simd_lut256_u8     tiku_simd_scalar_lut256_u8

#include "../../hal/tiku_simd.c"
