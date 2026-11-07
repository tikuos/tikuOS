/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compute_internal.h - arithmetic providers for validated buffers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_COMPUTE_INTERNAL_H_
#define TIKU_COMPUTE_INTERNAL_H_
#include "tiku_compute.h"

/** @brief Sum n byte products; callers provide valid buffers. */
int64_t tiku_compute_ref_dot_s8(const int8_t *a, const int8_t *b, uint32_t n);
/** @brief Sum n int16 products; callers provide valid aligned buffers. */
int64_t tiku_compute_ref_dot_s16(const int16_t *a, const int16_t *b, uint32_t n);
/** @brief Saturating addition over validated, disjoint or exactly aliased spans. */
void tiku_compute_ref_add_s32(const int32_t *a, const int32_t *b,
                             int32_t *dst, uint32_t n);

#ifndef TIKU_COMPUTE_BACKEND
#define TIKU_COMPUTE_BACKEND 0
#endif

#if TIKU_COMPUTE_TRACE
/** @brief Record vector and scalar elements; test counters require serialized calls. */
void tiku_compute_note(tiku_compute_op_t op, uint32_t vector, uint32_t scalar);
#else
#define tiku_compute_note(op, vector, scalar) ((void)(op))
#endif

#if TIKU_COMPUTE_BACKEND
/** @brief Reduce validated byte operands using the selected ISA provider. */
int64_t tiku_compute_impl_dot_s8(const int8_t *a, const int8_t *b, uint32_t n,
                                tiku_compute_op_t op);
/** @brief Reduce validated int16 operands using the selected ISA provider. */
int64_t tiku_compute_impl_dot_s16(const int16_t *a, const int16_t *b, uint32_t n,
                                 tiku_compute_op_t op);
/** @brief Add validated int32 operands using the selected ISA provider. */
void tiku_compute_impl_add_s32(const int32_t *a, const int32_t *b,
                              int32_t *out, uint32_t n);
#else
/** @brief Reduce bytes through the reference provider and record scalar work. */
static inline int64_t tiku_compute_impl_dot_s8(const int8_t *a, const int8_t *b,
                                              uint32_t n, tiku_compute_op_t op)
{
    tiku_compute_note(op, 0, n);
    return tiku_compute_ref_dot_s8(a, b, n);
}
/** @brief Reduce int16 through the reference provider and record scalar work. */
static inline int64_t tiku_compute_impl_dot_s16(const int16_t *a, const int16_t *b,
                                               uint32_t n, tiku_compute_op_t op)
{
    tiku_compute_note(op, 0, n);
    return tiku_compute_ref_dot_s16(a, b, n);
}
/** @brief Add through the reference provider and record scalar work. */
static inline void tiku_compute_impl_add_s32(const int32_t *a, const int32_t *b,
                                            int32_t *out, uint32_t n)
{
    tiku_compute_note(TIKU_COMPUTE_ADD_S32, 0, n);
    tiku_compute_ref_add_s32(a, b, out, n);
}
#endif
#endif
