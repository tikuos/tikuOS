/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compute_vector.h - saturating signed vector arithmetic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_COMPUTE_VECTOR_H_
#define TIKU_COMPUTE_VECTOR_H_
#include "tiku_compute.h"

/**
 * @brief Add n int32 pairs, clamping each result to INT32_MIN..INT32_MAX.
 * @note Buffers are naturally aligned; dst may equal a or b exactly.
 * @note Partial output/input overlap is rejected. With n zero, no access occurs.
 * @return Status; errors leave dst unchanged.
 */
tiku_compute_status_t tiku_compute_add_s32(const int32_t *a, const int32_t *b,
                                          int32_t *dst, uint32_t n);

#endif
