/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compute_dot.h - signed dot products with exact int64 results.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_COMPUTE_DOT_H_
#define TIKU_COMPUTE_DOT_H_
#include "tiku_compute.h"

/**
 * @brief Store the exact sum of n signed byte products in result.
 * @note Inputs hold n elements; result is disjoint from both inputs.
 * @note With n zero, inputs may be null and result receives zero.
 * @return Status; invalid spans, alignment or overlap leave result unchanged.
 */
tiku_compute_status_t tiku_compute_dot_s8(const int8_t *a, const int8_t *b,
                                         uint32_t n, int64_t *result);

/**
 * @brief Store the exact sum of n signed int16 products in result.
 * @note Inputs hold n naturally aligned elements; result is disjoint.
 * @note With n zero, inputs may be null and result receives zero.
 * @return Status; invalid spans, alignment or overlap leave result unchanged.
 */
tiku_compute_status_t tiku_compute_dot_s16(const int16_t *a, const int16_t *b,
                                          uint32_t n, int64_t *result);

#endif
