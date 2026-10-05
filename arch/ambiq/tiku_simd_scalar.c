/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_simd_scalar.c - scalar build of the Helium kernels, in the same image.
 *
 * Forces the SIMD backend off, renames the public symbols and includes
 * hal/tiku_simd.c, so the scalar kernels sit in the same image as the Helium
 * ones and tiku_simd_power.c can time both in one boot.
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
 * hal/tiku_simd.c selects its backend at compile time, so on the M55 its
 * scalar paths are compiled out of the normal build.  Included here with the
 * backend forced off and the symbols renamed, it gives scalar kernels from the
 * same source, in the same image and build as the Helium ones.
 */
#include "../../hal/tiku_simd.c"
