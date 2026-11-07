/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compute_matrix.h - signed byte matrix-vector products.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_COMPUTE_MATRIX_H_
#define TIKU_COMPUTE_MATRIX_H_
#include "tiku_compute.h"

/**
 * @brief Store rows exact int64 dot products of matrix rows and vector.
 * @note Stride counts int8 elements and must be at least cols for nonempty work.
 * @note Output is naturally aligned and disjoint from both input spans.
 * @note Zero rows accesses nothing; zero cols fills output with zeros without inputs.
 * @return Status; errors leave output unchanged, including invalid output spans.
 */
tiku_compute_status_t tiku_compute_matvec_s8(const int8_t *matrix,
                                            const int8_t *vector,
                                            uint32_t rows, uint32_t cols,
                                            uint32_t stride, int64_t *out);

#endif
