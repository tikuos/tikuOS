/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compute_ref.c - portable arithmetic on validated buffers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_compute_internal.h"

int64_t tiku_compute_ref_dot_s8(const int8_t *a, const int8_t *b, uint32_t n)
{
    int64_t sum = 0;
    uint32_t i;
    for (i = 0; i < n; ++i) {
        sum += (int64_t)a[i] * b[i];
    }
    return sum;
}

int64_t tiku_compute_ref_dot_s16(const int16_t *a, const int16_t *b, uint32_t n)
{
    int64_t sum = 0;
    uint32_t i;
    for (i = 0; i < n; ++i) {
        sum += (int64_t)a[i] * b[i];
    }
    return sum;
}

void tiku_compute_ref_add_s32(const int32_t *a, const int32_t *b,
                             int32_t *dst, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i) {
        int64_t sum = (int64_t)a[i] + b[i];
        dst[i] = sum > INT32_MAX ? INT32_MAX :
                 sum < INT32_MIN ? INT32_MIN : (int32_t)sum;
    }
}
