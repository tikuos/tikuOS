/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pie_arch.S - P4 PIE kernels and state access, through the encoded
 * instructions of pie/tiku_pie.inc.  The 128-bit loads and stores take
 * their base from x8-x15 or x24-x31: the argument registers and t3-t6.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "pie/tiku_pie.inc"

    .text

/* int64_t tiku_p4_pie_dot_s16(const int16_t *a, const int16_t *b,
 *                             uint32_t n): 8 products an iteration into
 * the 40-bit xacc, returned sign-extended in a1:a0. */
    .global tiku_p4_pie_dot_s16
    .type tiku_p4_pie_dot_s16, @function
tiku_p4_pie_dot_s16:
    esp_zero_xacc
    srli t0, a2, 3
    beqz t0, 2f
1:
    esp_vld_128_ip q0, a0, 16
    esp_vld_128_ip q1, a1, 16
    esp_vmulas_s16_xacc q0, q1
    addi t0, t0, -1
    bnez t0, 1b
2:
    esp_movx_r_xacc_l a0
    esp_movx_r_xacc_h a1
    slli a1, a1, 24
    srai a1, a1, 24
    ret
    .size tiku_p4_pie_dot_s16, .-tiku_p4_pie_dot_s16

/* int64_t tiku_p4_pie_dot_s8(const int8_t *a, const int8_t *b,
 *                            uint32_t n): 16 products an iteration. */
    .global tiku_p4_pie_dot_s8
    .type tiku_p4_pie_dot_s8, @function
tiku_p4_pie_dot_s8:
    esp_zero_xacc
    srli t0, a2, 4
    beqz t0, 2f
1:
    esp_vld_128_ip q0, a0, 16
    esp_vld_128_ip q1, a1, 16
    esp_vmulas_s8_xacc q0, q1
    addi t0, t0, -1
    bnez t0, 1b
2:
    esp_movx_r_xacc_l a0
    esp_movx_r_xacc_h a1
    slli a1, a1, 24
    srai a1, a1, 24
    ret
    .size tiku_p4_pie_dot_s8, .-tiku_p4_pie_dot_s8

/* void tiku_p4_pie_matvec_s8(const int8_t *m, uint32_t rows, uint32_t cols,
 *                            const int8_t *v, int32_t *out): one int8 dot
 * product a row, cols / 16 iterations each, the low 32 bits of xacc out. */
    .global tiku_p4_pie_matvec_s8
    .type tiku_p4_pie_matvec_s8, @function
tiku_p4_pie_matvec_s8:
    beqz a1, 4f
    srli t1, a2, 4
1:
    esp_zero_xacc
    mv t0, t1
    mv t3, a3
    beqz t0, 3f
2:
    esp_vld_128_ip q0, a0, 16
    esp_vld_128_ip q1, t3, 16
    esp_vmulas_s8_xacc q0, q1
    addi t0, t0, -1
    bnez t0, 2b
3:
    esp_movx_r_xacc_l t3
    sw t3, 0(a4)
    addi a4, a4, 4
    addi a1, a1, -1
    bnez a1, 1b
4:
    ret
    .size tiku_p4_pie_matvec_s8, .-tiku_p4_pie_matvec_s8

/* void tiku_p4_pie_vadd_s32(const int32_t *a, const int32_t *b,
 *                           int32_t *c, uint32_t n): 4 lanes an iteration. */
    .global tiku_p4_pie_vadd_s32
    .type tiku_p4_pie_vadd_s32, @function
tiku_p4_pie_vadd_s32:
    srli t0, a3, 2
    beqz t0, 2f
1:
    esp_vld_128_ip q0, a0, 16
    esp_vld_128_ip q1, a1, 16
    esp_vadd_s32 q2, q0, q1
    esp_vst_128_ip q2, a2, 16
    addi t0, t0, -1
    bnez t0, 1b
2:
    ret
    .size tiku_p4_pie_vadd_s32, .-tiku_p4_pie_vadd_s32

/* void tiku_p4_pie_qget(uint32_t *words): q0-q7 to 128 bytes. */
    .global tiku_p4_pie_qget
    .type tiku_p4_pie_qget, @function
tiku_p4_pie_qget:
    esp_vst_128_ip q0, a0, 16
    esp_vst_128_ip q1, a0, 16
    esp_vst_128_ip q2, a0, 16
    esp_vst_128_ip q3, a0, 16
    esp_vst_128_ip q4, a0, 16
    esp_vst_128_ip q5, a0, 16
    esp_vst_128_ip q6, a0, 16
    esp_vst_128_ip q7, a0, 16
    ret
    .size tiku_p4_pie_qget, .-tiku_p4_pie_qget

/* void tiku_p4_pie_qset(const uint32_t *words): q0-q7 from 128 bytes. */
    .global tiku_p4_pie_qset
    .type tiku_p4_pie_qset, @function
tiku_p4_pie_qset:
    esp_vld_128_ip q0, a0, 16
    esp_vld_128_ip q1, a0, 16
    esp_vld_128_ip q2, a0, 16
    esp_vld_128_ip q3, a0, 16
    esp_vld_128_ip q4, a0, 16
    esp_vld_128_ip q5, a0, 16
    esp_vld_128_ip q6, a0, 16
    esp_vld_128_ip q7, a0, 16
    ret
    .size tiku_p4_pie_qset, .-tiku_p4_pie_qset

/* void tiku_p4_pie_xacc_get(uint32_t *lo_hi) */
    .global tiku_p4_pie_xacc_get
    .type tiku_p4_pie_xacc_get, @function
tiku_p4_pie_xacc_get:
    esp_movx_r_xacc_l a1
    esp_movx_r_xacc_h a2
    sw a1, 0(a0)
    sw a2, 4(a0)
    ret
    .size tiku_p4_pie_xacc_get, .-tiku_p4_pie_xacc_get

/* void tiku_p4_pie_xacc_set(uint32_t lo, uint32_t hi) */
    .global tiku_p4_pie_xacc_set
    .type tiku_p4_pie_xacc_set, @function
tiku_p4_pie_xacc_set:
    esp_movx_w_xacc_l a0
    esp_movx_w_xacc_h a1
    ret
    .size tiku_p4_pie_xacc_set, .-tiku_p4_pie_xacc_set

/* uint32_t tiku_p4_pie_state(void) */
    .global tiku_p4_pie_state
    .type tiku_p4_pie_state, @function
tiku_p4_pie_state:
    csrr a0, 0x7F2
    ret
    .size tiku_p4_pie_state, .-tiku_p4_pie_state
