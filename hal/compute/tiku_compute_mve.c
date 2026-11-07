/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compute_mve.c - signed reductions and saturating addition on MVE.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_compute_internal.h"
#if !defined(__ARM_FEATURE_MVE) || !(__ARM_FEATURE_MVE & 1)
#error "The MVE compute provider requires integer MVE instructions"
#endif
#include <arm_mve.h>

int64_t tiku_compute_impl_dot_s8(const int8_t *a, const int8_t *b, uint32_t n,
                                tiku_compute_op_t op)
{
    int64_t sum = 0;
    while (n >= 16U) {
        uint32_t count = n < 131056U ? n & ~UINT32_C(15) : 131056U;
        uint32_t i;
        int32_t chunk = 0;
        for (i = 0; i < count; i += 16U) {
            chunk = vmladavaq_s8(chunk, vld1q_s8(a + i), vld1q_s8(b + i));
        }
        sum += chunk;
        tiku_compute_note(op, count, 0);
        a += count;
        b += count;
        n -= count;
    }
    tiku_compute_note(op, 0, n);
    return sum + tiku_compute_ref_dot_s8(a, b, n);
}

int64_t tiku_compute_impl_dot_s16(const int16_t *a, const int16_t *b, uint32_t n,
                                 tiku_compute_op_t op)
{
    int64_t sum = 0;
    uint32_t count = n & ~UINT32_C(7);
    uint32_t i;
    for (i = 0; i < count; i += 8U) {
        sum = vmlaldavaq_s16(sum, vld1q_s16(a + i), vld1q_s16(b + i));
    }
    tiku_compute_note(op, count, n - count);
    if (count == 0) {
        return tiku_compute_ref_dot_s16(a, b, n);
    }
    return sum + tiku_compute_ref_dot_s16(a + count, b + count, n - count);
}

void tiku_compute_impl_add_s32(const int32_t *a, const int32_t *b,
                              int32_t *out, uint32_t n)
{
    uint32_t count = n & ~UINT32_C(3);
    uint32_t i;
    for (i = 0; i < count; i += 4U) {
        vst1q_s32(out + i, vqaddq_s32(vld1q_s32(a + i), vld1q_s32(b + i)));
    }
    tiku_compute_note(TIKU_COMPUTE_ADD_S32, count, n - count);
    if (count == 0) {
        tiku_compute_ref_add_s32(a, b, out, n);
    } else {
        tiku_compute_ref_add_s32(a + count, b + count, out + count, n - count);
    }
}
