/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_memcpy_arch.S - P4 memcpy, memset and memmove on the PIE: 16 bytes
 * a load or store once the destination is 16-byte aligned, the shifter for
 * a source that is not, words and bytes around them.  A copy puts CSR
 * 0x7F2 back as it found it unless the context was already dirty.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "pie/tiku_pie.inc"
#include "tiku_irq_arch.h"

#define VECTOR_MIN 32

    .text

/* void *memcpy(void *d, const void *s, size_t n).  d in a3 and s in a4
 * move; a0 is returned.  Below VECTOR_MIN bytes the small path copies
 * words when d and s agree modulo 4, bytes otherwise; it also takes the
 * tail of a vector copy and, at any length, a copy with an end outside
 * 0x40000000-0x4FFFFFFF (flash, PSRAM, ROM, L2 SRAM): the vector unit's
 * 16-byte accesses fault on the peripheral bus, LP SRAM included. */
    .global memcpy
    .type memcpy, @function
memcpy:
    mv   a3, a0
    mv   a4, a1
    li   t0, VECTOR_MIN
    bltu a2, t0, .Lcpy_small
    srli t1, a3, 28
    addi t1, t1, -4
    bnez t1, .Lcpy_small
    srli t1, a4, 28
    addi t1, t1, -4
    bnez t1, .Lcpy_small
    neg  t1, a3
    andi t1, t1, 15
    beqz t1, .Lcpy_body
    sub  a2, a2, t1
.Lcpy_head:
    lbu  t2, 0(a4)
    sb   t2, 0(a3)
    addi a4, a4, 1
    addi a3, a3, 1
    addi t1, t1, -1
    bnez t1, .Lcpy_head
.Lcpy_body:
    csrr t5, TIKU_P4_PIE_STATE_CSR
    srli t1, a2, 4
    andi a2, a2, 15
    andi t2, a4, 15
    bnez t2, .Lcpy_shift
    srli t2, t1, 2
    beqz t2, .Lcpy_al16
.Lcpy_al64:
    esp_vld_128_ip q0, a4, 16
    esp_vld_128_ip q1, a4, 16
    esp_vld_128_ip q2, a4, 16
    esp_vld_128_ip q3, a4, 16
    esp_vst_128_ip q0, a3, 16
    esp_vst_128_ip q1, a3, 16
    esp_vst_128_ip q2, a3, 16
    esp_vst_128_ip q3, a3, 16
    addi t2, t2, -1
    bnez t2, .Lcpy_al64
.Lcpy_al16:
    andi t1, t1, 3
    beqz t1, .Lcpy_state
.Lcpy_al16_loop:
    esp_vld_128_ip q0, a4, 16
    esp_vst_128_ip q0, a3, 16
    addi t1, t1, -1
    bnez t1, .Lcpy_al16_loop
    j    .Lcpy_state
.Lcpy_shift:
    /* s is not 16-byte aligned: q0 holds the block s lies in and the
     * shifter's byte count is s & 15; each further block, loaded aligned
     * through t3, yields the next 16 bytes of the stream. */
    esp_ld_128_usar_ip q0, a4, 0
    andi t3, a4, -16
    addi t3, t3, 16
    slli t2, t1, 4
    add  a4, a4, t2
    srli t2, t1, 1
    beqz t2, .Lcpy_sh16
.Lcpy_sh32:
    esp_vld_128_ip q1, t3, 16
    esp_src_q_qup q2, q0, q1
    esp_vst_128_ip q2, a3, 16
    esp_vld_128_ip q1, t3, 16
    esp_src_q_qup q2, q0, q1
    esp_vst_128_ip q2, a3, 16
    addi t2, t2, -1
    bnez t2, .Lcpy_sh32
.Lcpy_sh16:
    andi t1, t1, 1
    beqz t1, .Lcpy_state
    esp_vld_128_ip q1, t3, 16
    esp_src_q_qup q2, q0, q1
    esp_vst_128_ip q2, a3, 16
.Lcpy_state:
    andi t2, t5, 3
    addi t2, t2, -3
    beqz t2, .Lcpy_small
    csrw TIKU_P4_PIE_STATE_CSR, t5
.Lcpy_small:
    beqz a2, .Lcpy_ret
    xor  t2, a3, a4
    andi t2, t2, 3
    bnez t2, .Lcpy_bytes
    andi t1, a3, 3
    beqz t1, .Lcpy_words
    li   t2, 4
    sub  t1, t2, t1
    bltu a2, t1, .Lcpy_bytes
    sub  a2, a2, t1
.Lcpy_align4:
    lbu  t2, 0(a4)
    sb   t2, 0(a3)
    addi a4, a4, 1
    addi a3, a3, 1
    addi t1, t1, -1
    bnez t1, .Lcpy_align4
.Lcpy_words:
    srli t1, a2, 2
    beqz t1, .Lcpy_bytes
    andi a2, a2, 3
.Lcpy_wloop:
    lw   t2, 0(a4)
    sw   t2, 0(a3)
    addi a4, a4, 4
    addi a3, a3, 4
    addi t1, t1, -1
    bnez t1, .Lcpy_wloop
.Lcpy_bytes:
    beqz a2, .Lcpy_ret
.Lcpy_bloop:
    lbu  t2, 0(a4)
    sb   t2, 0(a3)
    addi a4, a4, 1
    addi a3, a3, 1
    addi a2, a2, -1
    bnez a2, .Lcpy_bloop
.Lcpy_ret:
    ret
    .size memcpy, .-memcpy

/* void *memset(void *d, int c, size_t n): the byte replicated into a word
 * in a1, then into q0 for 16-byte stores within the window above. */
    .global memset
    .type memset, @function
memset:
    mv   a3, a0
    andi a1, a1, 0xFF
    slli t1, a1, 8
    or   a1, a1, t1
    slli t1, a1, 16
    or   a1, a1, t1
    li   t0, VECTOR_MIN
    bltu a2, t0, .Lset_small
    srli t1, a3, 28
    addi t1, t1, -4
    bnez t1, .Lset_small
    neg  t1, a3
    andi t1, t1, 15
    beqz t1, .Lset_body
    sub  a2, a2, t1
.Lset_head:
    sb   a1, 0(a3)
    addi a3, a3, 1
    addi t1, t1, -1
    bnez t1, .Lset_head
.Lset_body:
    csrr t5, TIKU_P4_PIE_STATE_CSR
    esp_movi_32_q q0, a1, 0
    esp_movi_32_q q0, a1, 1
    esp_movi_32_q q0, a1, 2
    esp_movi_32_q q0, a1, 3
    srli t1, a2, 4
    andi a2, a2, 15
    srli t2, t1, 2
    beqz t2, .Lset_v16
.Lset_v64:
    esp_vst_128_ip q0, a3, 16
    esp_vst_128_ip q0, a3, 16
    esp_vst_128_ip q0, a3, 16
    esp_vst_128_ip q0, a3, 16
    addi t2, t2, -1
    bnez t2, .Lset_v64
.Lset_v16:
    andi t1, t1, 3
    beqz t1, .Lset_state
.Lset_v16_loop:
    esp_vst_128_ip q0, a3, 16
    addi t1, t1, -1
    bnez t1, .Lset_v16_loop
.Lset_state:
    andi t2, t5, 3
    addi t2, t2, -3
    beqz t2, .Lset_small
    csrw TIKU_P4_PIE_STATE_CSR, t5
.Lset_small:
    beqz a2, .Lset_ret
    andi t1, a3, 3
    beqz t1, .Lset_words
    li   t2, 4
    sub  t1, t2, t1
    bltu a2, t1, .Lset_bytes
    sub  a2, a2, t1
.Lset_align4:
    sb   a1, 0(a3)
    addi a3, a3, 1
    addi t1, t1, -1
    bnez t1, .Lset_align4
.Lset_words:
    srli t1, a2, 2
    beqz t1, .Lset_bytes
    andi a2, a2, 3
.Lset_wloop:
    sw   a1, 0(a3)
    addi a3, a3, 4
    addi t1, t1, -1
    bnez t1, .Lset_wloop
.Lset_bytes:
    beqz a2, .Lset_ret
.Lset_bloop:
    sb   a1, 0(a3)
    addi a3, a3, 1
    addi a2, a2, -1
    bnez a2, .Lset_bloop
.Lset_ret:
    ret
    .size memset, .-memset

/* void *memmove(void *d, const void *s, size_t n): memcpy unless d lies
 * inside the source; then the bytes go down from the end, as words when
 * d and s agree modulo 4. */
    .global memmove
    .type memmove, @function
memmove:
    sub  t0, a0, a1
    bgeu t0, a2, memcpy
    add  a3, a0, a2
    add  a4, a1, a2
    xor  t2, a0, a1
    andi t2, t2, 3
    bnez t2, .Lmv_bytes
    andi t1, a3, 3
    bltu a2, t1, .Lmv_bytes
    sub  a2, a2, t1
    beqz t1, .Lmv_words
.Lmv_align4:
    addi a4, a4, -1
    addi a3, a3, -1
    lbu  t2, 0(a4)
    sb   t2, 0(a3)
    addi t1, t1, -1
    bnez t1, .Lmv_align4
.Lmv_words:
    srli t1, a2, 2
    beqz t1, .Lmv_bytes
    andi a2, a2, 3
.Lmv_wloop:
    addi a4, a4, -4
    addi a3, a3, -4
    lw   t2, 0(a4)
    sw   t2, 0(a3)
    addi t1, t1, -1
    bnez t1, .Lmv_wloop
.Lmv_bytes:
    beqz a2, .Lmv_ret
.Lmv_bloop:
    addi a4, a4, -1
    addi a3, a3, -1
    lbu  t2, 0(a4)
    sb   t2, 0(a3)
    addi a2, a2, -1
    bnez a2, .Lmv_bloop
.Lmv_ret:
    ret
    .size memmove, .-memmove
