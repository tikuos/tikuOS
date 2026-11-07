/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compute.c - buffer validation and shared arithmetic entry points.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_compute_dot.h"
#include "tiku_compute_vector.h"
#include "tiku_compute_matrix.h"
#include "tiku_compute_internal.h"
#include <stddef.h>

/** @brief Check natural alignment and representable byte and address extents. */
static tiku_compute_status_t span(const void *ptr, uint64_t bytes,
                                  uintptr_t alignment)
{
    uintptr_t base = (uintptr_t)ptr;
    if (bytes == 0) {
        return TIKU_COMPUTE_OK;
    }
    if (ptr == NULL || base % alignment != 0) {
        return TIKU_COMPUTE_ERR_PARAM;
    }
    if (bytes > SIZE_MAX || bytes > UINTPTR_MAX ||
        bytes > UINTPTR_MAX - base) {
        return TIKU_COMPUTE_ERR_RANGE;
    }
    return TIKU_COMPUTE_OK;
}

/** @brief Test overlap of two validated byte spans. */
static int overlap(const void *a, uint64_t an, const void *b, uint64_t bn)
{
    uintptr_t ap = (uintptr_t)a;
    uintptr_t bp = (uintptr_t)b;
    return an != 0 && bn != 0 && ap < bp + (uintptr_t)bn &&
           bp < ap + (uintptr_t)an;
}

/** @brief Validate a reduction and its disjoint result before publication. */
static tiku_compute_status_t dot_spans(const void *a, const void *b,
                                       uint32_t n, uintptr_t element_size,
                                       uintptr_t alignment, int64_t *result)
{
    uint64_t bytes = (uint64_t)n * element_size;
    tiku_compute_status_t status = span(result, sizeof(*result), _Alignof(int64_t));
    if (status != TIKU_COMPUTE_OK) {
        return status;
    }
    status = span(a, bytes, alignment);
    if (status != TIKU_COMPUTE_OK) {
        return status;
    }
    status = span(b, bytes, alignment);
    if (status != TIKU_COMPUTE_OK) {
        return status;
    }
    if (overlap(a, bytes, result, sizeof(*result)) ||
        overlap(b, bytes, result, sizeof(*result))) {
        return TIKU_COMPUTE_ERR_OVERLAP;
    }
    return TIKU_COMPUTE_OK;
}

tiku_compute_provider_t tiku_compute_provider(tiku_compute_op_t op)
{
    switch (op) {
    case TIKU_COMPUTE_DOT_S8:
    case TIKU_COMPUTE_DOT_S16:
    case TIKU_COMPUTE_ADD_S32:
    case TIKU_COMPUTE_MATVEC_S8:
        return (tiku_compute_provider_t)TIKU_COMPUTE_BACKEND;
    default:
        return TIKU_COMPUTE_PROVIDER_UNKNOWN;
    }
}

tiku_compute_status_t tiku_compute_dot_s8(const int8_t *a, const int8_t *b,
                                         uint32_t n, int64_t *result)
{
    tiku_compute_status_t status = dot_spans(a, b, n, sizeof(*a),
                                             _Alignof(int8_t), result);
    if (status == TIKU_COMPUTE_OK) {
        *result = tiku_compute_impl_dot_s8(a, b, n, TIKU_COMPUTE_DOT_S8);
    }
    return status;
}

tiku_compute_status_t tiku_compute_dot_s16(const int16_t *a, const int16_t *b,
                                          uint32_t n, int64_t *result)
{
    tiku_compute_status_t status = dot_spans(a, b, n, sizeof(*a),
                                             _Alignof(int16_t), result);
    if (status == TIKU_COMPUTE_OK) {
        *result = tiku_compute_impl_dot_s16(a, b, n, TIKU_COMPUTE_DOT_S16);
    }
    return status;
}

tiku_compute_status_t tiku_compute_add_s32(const int32_t *a, const int32_t *b,
                                          int32_t *dst, uint32_t n)
{
    uint64_t bytes = (uint64_t)n * sizeof(*a);
    tiku_compute_status_t status = span(a, bytes, _Alignof(int32_t));
    if (status != TIKU_COMPUTE_OK) {
        return status;
    }
    status = span(b, bytes, _Alignof(int32_t));
    if (status != TIKU_COMPUTE_OK) {
        return status;
    }
    status = span(dst, bytes, _Alignof(int32_t));
    if (status != TIKU_COMPUTE_OK) {
        return status;
    }
    if ((dst != a && overlap(a, bytes, dst, bytes)) ||
        (dst != b && overlap(b, bytes, dst, bytes))) {
        return TIKU_COMPUTE_ERR_OVERLAP;
    }
    tiku_compute_impl_add_s32(a, b, dst, n);
    return TIKU_COMPUTE_OK;
}

tiku_compute_status_t tiku_compute_matvec_s8(const int8_t *matrix,
                                            const int8_t *vector,
                                            uint32_t rows, uint32_t cols,
                                            uint32_t stride, int64_t *out)
{
    uint64_t matrix_bytes;
    uint64_t out_bytes = (uint64_t)rows * sizeof(*out);
    uint32_t r;
    tiku_compute_status_t status;
    if (rows == 0) {
        return TIKU_COMPUTE_OK;
    }
    status = span(out, out_bytes, _Alignof(int64_t));
    if (status != TIKU_COMPUTE_OK) {
        return status;
    }
    if (cols == 0) {
        for (r = 0; r < rows; ++r) {
            out[r] = 0;
        }
        return TIKU_COMPUTE_OK;
    }
    if (stride < cols) {
        return TIKU_COMPUTE_ERR_PARAM;
    }
    matrix_bytes = (uint64_t)(rows - 1U) * stride + cols;
    status = span(matrix, matrix_bytes, _Alignof(int8_t));
    if (status != TIKU_COMPUTE_OK) {
        return status;
    }
    status = span(vector, cols, _Alignof(int8_t));
    if (status != TIKU_COMPUTE_OK) {
        return status;
    }
    if (overlap(matrix, matrix_bytes, out, out_bytes) ||
        overlap(vector, cols, out, out_bytes)) {
        return TIKU_COMPUTE_ERR_OVERLAP;
    }
    for (r = 0; r < rows; ++r) {
        out[r] = tiku_compute_impl_dot_s8(matrix + (size_t)r * stride,
                                        vector, cols, TIKU_COMPUTE_MATVEC_S8);
    }
    return TIKU_COMPUTE_OK;
}

#if TIKU_COMPUTE_TRACE
static tiku_compute_trace_t counters[4];
static int trace_enabled = 1;

void tiku_compute_note(tiku_compute_op_t op, uint32_t vector, uint32_t scalar)
{
    if (trace_enabled && (unsigned int)op < 4U) {
        counters[op].vector_elements += vector;
        counters[op].scalar_elements += scalar;
    }
}

void tiku_compute_trace_enable(int enabled)
{
    trace_enabled = enabled != 0;
}

void tiku_compute_trace_reset(void)
{
    unsigned int i;
    for (i = 0; i < 4U; ++i) {
        counters[i].vector_elements = 0;
        counters[i].scalar_elements = 0;
    }
}

tiku_compute_trace_t tiku_compute_trace(tiku_compute_op_t op)
{
    tiku_compute_trace_t empty = {0, 0};
    return (unsigned int)op < 4U ? counters[op] : empty;
}
#endif
