/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compute_p4.c - bounded PIE reductions and aligned operand staging.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/compute/tiku_compute_internal.h>
#include <arch/esp32p4/tiku_pie_arch.h>
#include <arch/esp32p4/compute/tiku_compute_p4_state.h>

#if TIKU_COMPUTE_P4_REV != 1
#error "PIE compute requires ESP32-P4 v1.x silicon"
#endif

/** @brief Test whether the complete operand lies on the PIE memory bus. */
static int vector_memory(const void *ptr, uint64_t bytes)
{
    uintptr_t base = (uintptr_t)ptr;
    return base >= UINT32_C(0x40000000) && base < UINT32_C(0x50000000) &&
           bytes <= UINT32_C(0x50000000) - base;
}

int64_t tiku_compute_impl_dot_s8(const int8_t *a, const int8_t *b, uint32_t n,
                                tiku_compute_op_t op)
{
    _Alignas(16) int8_t sa[128], sb[128];
    uint32_t state = tiku_compute_p4_state_get();
    int64_t sum = 0;
    if ((state & 3U) == 0 || !vector_memory(a, n) || !vector_memory(b, n)) {
        tiku_compute_note(op, 0, n);
        return tiku_compute_ref_dot_s8(a, b, n);
    }
    while (n >= 16U) {
        uint32_t count = n < 33554416U ? n : 33554416U;
        const int8_t *pa = a, *pb = b;
        uint32_t i;
        if ((((uintptr_t)a | (uintptr_t)b) & 15U) != 0) {
            if (count > 128U) {
                count = 128U;
            }
            count &= ~UINT32_C(15);
            for (i = 0; i < count; ++i) {
                sa[i] = a[i];
                sb[i] = b[i];
            }
            pa = sa;
            pb = sb;
        } else {
            count &= ~UINT32_C(15);
        }
        sum += tiku_p4_pie_dot_s8(pa, pb, count);
        tiku_compute_note(op, count, 0);
        a += count;
        b += count;
        n -= count;
    }
    sum += tiku_compute_ref_dot_s8(a, b, n);
    tiku_compute_note(op, 0, n);
    tiku_compute_p4_state_restore(state);
    return sum;
}

int64_t tiku_compute_impl_dot_s16(const int16_t *a, const int16_t *b, uint32_t n,
                                 tiku_compute_op_t op)
{
    _Alignas(16) int16_t sa[128], sb[128];
    uint32_t state = tiku_compute_p4_state_get();
    int64_t sum = 0;
    if ((state & 3U) == 0 || !vector_memory(a, (uint64_t)n * 2U) ||
        !vector_memory(b, (uint64_t)n * 2U)) {
        tiku_compute_note(op, 0, n);
        return tiku_compute_ref_dot_s16(a, b, n);
    }
    while (n >= 8U) {
        uint32_t count = n < 504U ? n : 504U;
        const int16_t *pa = a, *pb = b;
        uint32_t i;
        if ((((uintptr_t)a | (uintptr_t)b) & 15U) != 0) {
            if (count > 128U) {
                count = 128U;
            }
            count &= ~UINT32_C(7);
            for (i = 0; i < count; ++i) {
                sa[i] = a[i];
                sb[i] = b[i];
            }
            pa = sa;
            pb = sb;
        } else {
            count &= ~UINT32_C(7);
        }
        sum += tiku_p4_pie_dot_s16(pa, pb, count);
        tiku_compute_note(op, count, 0);
        a += count;
        b += count;
        n -= count;
    }
    sum += tiku_compute_ref_dot_s16(a, b, n);
    tiku_compute_note(op, 0, n);
    tiku_compute_p4_state_restore(state);
    return sum;
}

void tiku_compute_impl_add_s32(const int32_t *a, const int32_t *b,
                              int32_t *out, uint32_t n)
{
    _Alignas(16) int32_t sa[4], sb[4], dst[4];
    uint32_t state = tiku_compute_p4_state_get();
    if ((state & 3U) == 0 || !vector_memory(a, (uint64_t)n * 4U) ||
        !vector_memory(b, (uint64_t)n * 4U) || !vector_memory(out, (uint64_t)n * 4U)) {
        tiku_compute_note(TIKU_COMPUTE_ADD_S32, 0, n);
        tiku_compute_ref_add_s32(a, b, out, n);
        return;
    }
    while (n >= 4U) {
        uint32_t count = n & ~UINT32_C(3);
        if ((((uintptr_t)a | (uintptr_t)b | (uintptr_t)out) & 15U) != 0) {
            uint32_t i;
            count = 4;
            for (i = 0; i < 4U; ++i) {
                sa[i] = a[i];
                sb[i] = b[i];
            }
            tiku_p4_pie_vadd_s32(sa, sb, dst, 4);
            for (i = 0; i < 4U; ++i) {
                out[i] = dst[i];
            }
        } else {
            tiku_p4_pie_vadd_s32(a, b, out, count);
        }
        tiku_compute_note(TIKU_COMPUTE_ADD_S32, count, 0);
        a += count;
        b += count;
        out += count;
        n -= count;
    }
    tiku_compute_ref_add_s32(a, b, out, n);
    tiku_compute_note(TIKU_COMPUTE_ADD_S32, 0, n);
    tiku_compute_p4_state_restore(state);
}
