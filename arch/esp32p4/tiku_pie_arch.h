/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pie_arch.h - P4 PIE: kernels on the vector unit (xespv2p1
 * encodings from arch/esp32p4/pie/tiku_pie.inc) and its state for tests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_PIE_ARCH_H_
#define TIKU_ESP32P4_PIE_ARCH_H_
#include <stdint.h>

/** @brief Sum of a[i] * b[i] over @p n int16 elements, @p n a multiple of
 *         8, both arrays 16-byte aligned: the 40-bit accumulator, exact
 *         while |sum| < 2^39. */
int64_t tiku_p4_pie_dot_s16(const int16_t *a, const int16_t *b, uint32_t n);
/** @brief The same over int8 elements, @p n a multiple of 16. */
int64_t tiku_p4_pie_dot_s8(const int8_t *a, const int8_t *b, uint32_t n);
/** @brief c[i] = a[i] + b[i] over @p n int32 lanes, @p n a multiple of 4,
 *         saturating as this silicon does; the arrays 16-byte aligned. */
void tiku_p4_pie_vadd_s32(const int32_t *a, const int32_t *b, int32_t *c,
                          uint32_t n);
/** @brief out[r] = dot_s8(m + r * cols, v, cols) for @p rows rows, @p cols
 *         a multiple of 16, @p m and @p v 16-byte aligned; each sum taken
 *         as int32. */
void tiku_p4_pie_matvec_s8(const int8_t *m, uint32_t rows, uint32_t cols,
                           const int8_t *v, int32_t *out);

/** @brief q0-q7 into @p words (32 words, 16-byte aligned). */
void tiku_p4_pie_qget(uint32_t *words);
/** @brief q0-q7 from @p words (32 words, 16-byte aligned). */
void tiku_p4_pie_qset(const uint32_t *words);
/** @brief The 40-bit xacc: low 32 bits and the high 8 in @p lo_hi[2]. */
void tiku_p4_pie_xacc_get(uint32_t *lo_hi);
/** @brief Set the xacc from @p lo and the low 8 bits of @p hi. */
void tiku_p4_pie_xacc_set(uint32_t lo, uint32_t hi);
/** @brief The PIE state CSR (0x7F2): 0 off, 1 initial, 2 clean, 3 dirty. */
uint32_t tiku_p4_pie_state(void);
#endif
