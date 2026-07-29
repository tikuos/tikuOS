/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_simd_scalar.c - scalar twin of the Helium kernels, in the same image.
 *
 * Forces the SIMD backend off, renames the public symbols and includes the
 * original source, so the scalar path can be measured against Helium in one build
 * and one boot.  See the note at the include for why it is done this way.
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

/*
 * INCLUDING A .c IS DELIBERATE AND CONFINED TO THIS FILE.  hal/tiku_simd.c
 * selects its backend at COMPILE time, so on this M55 the scalar paths are
 * compiled out.  Measuring Helium against scalar across two firmware images
 * would inherit ~3 % run-to-run variance between boots, plus a loop-alignment
 * hazard that once moved a busy-current figure by 956 uA purely because an
 * unrelated build flag shifted the code.  Forcing the backend off and renaming
 * the symbols gives a scalar twin from the SAME SOURCE in the SAME IMAGE, so
 * the two paths cannot drift and both are measured at one build's alignment.
 * Nothing else in this tree does this; it is not a pattern to copy.
 */
#include "../../hal/tiku_simd.c"
