/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crypto_arch.c - P4 SHA-256 and AES engines, register-fed, no DMA.
 *
 * Register fields follow ESP-IDF 4d59230 sha_reg.h, aes_reg.h, sha_ll.h,
 * aes_ll.h and hp_sys_clkrst_reg.h (hw_ver1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "tiku_crypto_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"

#define AES_BASE        0x50090000u
#define AES_KEY(n)      (AES_BASE + 4u * (n))
#define AES_IN(n)       (AES_BASE + 0x20u + 4u * (n))
#define AES_OUT(n)      (AES_BASE + 0x30u + 4u * (n))
#define AES_MODE        (AES_BASE + 0x40u)
#define AES_TRIGGER     (AES_BASE + 0x48u)
#define AES_STATE       (AES_BASE + 0x4Cu)
#define AES_DMA         (AES_BASE + 0x90u)
#define AES_DECRYPT     4u
#define SHA_BASE        0x50091000u
#define SHA_MODE        SHA_BASE
#define SHA_START       (SHA_BASE + 0x10u)
#define SHA_CONTINUE    (SHA_BASE + 0x14u)
#define SHA_BUSY        (SHA_BASE + 0x18u)
#define SHA_H(n)        (SHA_BASE + 0x40u + 4u * (n))
#define SHA_M(n)        (SHA_BASE + 0x80u + 4u * (n))
#define SHA2_256        2u
/* HP_SYS_CLKRST: the engines' clock gates (PERI_CLK_CTRL25) and resets
 * (HP_RST_EN2).  An engine runs only with its own reset and those sha_ll.h
 * and aes_ll.h clear beside it released: crypto, DS, HMAC, ECDSA. */
#define CLK_CRYPTO      0x500E60A8u
#define CLK_AES         (1u << 14)
#define CLK_SHA         (1u << 20)
#define RESET_CRYPTO    0x500E60C8u
#define RESET_AES       ((1u << 14) | (1u << 16) | (1u << 17))
#define RESET_SHA       ((1u << 14) | (1u << 17) | (1u << 18) | (1u << 19) | \
                         (1u << 20))
#define BUSY_SPINS      4096u
#define SHA_CHUNK       8u        /* blocks per interrupt-masked chunk */

static uint8_t hw_mode;
static uint16_t hw_ops, sw_ops, hw_errs;

/** @brief Set @p bits of register @p reg to their values in @p value. */
static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

uint8_t tiku_crypto_hw_mode(void)
{
    return hw_mode;
}

void tiku_crypto_hw_mode_set(uint8_t mode)
{
    hw_mode = mode != 0u ? TIKU_CRYPTO_HW_MODE_SW : TIKU_CRYPTO_HW_MODE_AUTO;
}

void tiku_crypto_hw_counters(uint16_t *hw, uint16_t *sw, uint16_t *errs)
{
    if (hw != NULL) {
        *hw = hw_ops;
    }
    if (sw != NULL) {
        *sw = sw_ops;
    }
    if (errs != NULL) {
        *errs = hw_errs;
    }
}

void tiku_crypto_hw_count_sw(void)
{
    sw_ops++;
}

/**
 * @brief Run @p blocks 64-byte blocks through the SHA-256 engine with
 *        interrupts masked.
 *
 * The first chunk starts from the standard initial value; later ones load
 * @p state back first, so another caller may use the engine between chunks.
 * @return 0 with @p state updated, or -1 if the engine stays busy
 */
static int sha_chunk(const uint8_t *data, unsigned blocks, uint32_t state[8],
                     int first)
{
    uint32_t irq = TIKU_P4_IRQ_SAVE(), word;
    unsigned b, i, n;
    int rc = 0;

    put_bits(CLK_CRYPTO, CLK_SHA, CLK_SHA);
    put_bits(RESET_CRYPTO, RESET_SHA, 0u);
    TIKU_P4_REG_WRITE(SHA_MODE, SHA2_256);
    if (!first) {
        for (i = 0; i < 8u; i++) {
            TIKU_P4_REG_WRITE(SHA_H(i), state[i]);
        }
    }
    for (b = 0; b < blocks && rc == 0; b++, data += 64u) {
        for (i = 0; i < 16u; i++) {
            memcpy(&word, data + 4u * i, 4u);
            TIKU_P4_REG_WRITE(SHA_M(i), word);
        }
        TIKU_P4_REG_WRITE(first && b == 0u ? SHA_START : SHA_CONTINUE, 1u);
        for (n = 0; n < BUSY_SPINS && TIKU_P4_REG_READ(SHA_BUSY); n++) {
        }
        rc = n < BUSY_SPINS ? 0 : -1;
    }
    if (rc == 0) {
        for (i = 0; i < 8u; i++) {
            state[i] = TIKU_P4_REG_READ(SHA_H(i));
        }
    }
    TIKU_P4_IRQ_RESTORE(irq);
    return rc;
}

int tiku_crypto_arch_sha256(const void *msg, size_t len, uint8_t out[32])
{
    const uint8_t *p = msg;
    uint64_t bits = (uint64_t)len * 8u;
    uint8_t block[128];
    uint32_t state[8];
    size_t left = len;
    unsigned i, n;
    int first = 1;

    if ((msg == NULL && len != 0u) || out == NULL) {
        return -2;
    }
    for (; left >= 64u; left -= 64u * n, p += 64u * n, first = 0) {
        n = left / 64u < SHA_CHUNK ? (unsigned)(left / 64u) : SHA_CHUNK;
        if (sha_chunk(p, n, state, first) != 0) {
            goto fail;
        }
    }
    /* The final block: the message tail, 0x80, zeros and the 64-bit
     * big-endian bit length, spilling into a second block past byte 55. */
    memset(block, 0, sizeof block);
    memcpy(block, p, left);
    block[left] = 0x80u;
    n = left >= 56u ? 2u : 1u;
    for (i = 0; i < 8u; i++) {
        block[64u * n - 1u - i] = (uint8_t)(bits >> (8u * i));
    }
    if (sha_chunk(block, n, state, first) != 0) {
        goto fail;
    }
    memcpy(out, state, 32u);
    hw_ops++;
    return 0;
fail:
    hw_errs++;
    return -1;
}

/** @brief One AES block in typical mode, interrupts masked.  @return 0, or -1
 *         if the engine stays busy */
static int aes_block(int decrypt, const uint8_t *key, size_t key_sz,
                     const uint8_t in[16], uint8_t out[16])
{
    uint32_t irq = TIKU_P4_IRQ_SAVE(), word;
    unsigned i, n;
    int rc = -1;

    put_bits(CLK_CRYPTO, CLK_AES, CLK_AES);
    put_bits(RESET_CRYPTO, RESET_AES, 0u);
    TIKU_P4_REG_WRITE(AES_DMA, 0u);
    TIKU_P4_REG_WRITE(AES_MODE, (decrypt ? AES_DECRYPT : 0u) +
                                    (uint32_t)(key_sz / 8u - 2u));
    for (i = 0; i < key_sz / 4u; i++) {
        memcpy(&word, key + 4u * i, 4u);
        TIKU_P4_REG_WRITE(AES_KEY(i), word);
    }
    for (i = 0; i < 4u; i++) {
        memcpy(&word, in + 4u * i, 4u);
        TIKU_P4_REG_WRITE(AES_IN(i), word);
    }
    TIKU_P4_REG_WRITE(AES_TRIGGER, 1u);
    for (n = 0; n < BUSY_SPINS; n++) {
        if (TIKU_P4_REG_READ(AES_STATE) == 0u) {
            for (i = 0; i < 4u; i++) {
                word = TIKU_P4_REG_READ(AES_OUT(i));
                memcpy(out + 4u * i, &word, 4u);
            }
            rc = 0;
            break;
        }
    }
    TIKU_P4_IRQ_RESTORE(irq);
    return rc;
}

int tiku_crypto_arch_aes_ecb(int decrypt, const uint8_t *key, size_t key_sz,
                             const uint8_t in[16], uint8_t out[16])
{
    if (key == NULL || in == NULL || out == NULL ||
        (key_sz != 16u && key_sz != 32u)) {
        return -2;
    }
    if (aes_block(decrypt, key, key_sz, in, out) != 0) {
        hw_errs++;
        return -1;
    }
    hw_ops++;
    return 0;
}

/** @brief @p x = @p x * @p h in GF(2^128), GCM's bit order (SP 800-38D 6.3). */
static void gf_mult(uint8_t x[16], const uint8_t h[16])
{
    uint8_t z[16], v[16];
    unsigned i, j, carry;

    memset(z, 0, sizeof z);
    memcpy(v, h, sizeof v);
    for (i = 0; i < 128u; i++) {
        if (x[i / 8u] & (0x80u >> (i % 8u))) {
            for (j = 0; j < 16u; j++) {
                z[j] ^= v[j];
            }
        }
        carry = v[15] & 1u;
        for (j = 15u; j > 0u; j--) {
            v[j] = (uint8_t)((v[j] >> 1) | (v[j - 1u] << 7));
        }
        v[0] >>= 1;
        if (carry) {
            v[0] ^= 0xE1u;
        }
    }
    memcpy(x, z, 16u);
}

/** @brief Fold @p len bytes into GHASH state @p y, the last block zero-padded. */
static void ghash(uint8_t y[16], const uint8_t h[16], const uint8_t *data,
                  size_t len)
{
    size_t n, i;

    while (len > 0u) {
        n = len < 16u ? len : 16u;
        for (i = 0; i < n; i++) {
            y[i] ^= data[i];
        }
        gf_mult(y, h);
        data += n;
        len -= n;
    }
}

int tiku_crypto_arch_aes_gcm_kit(int decrypt,
                                 const uint8_t *key, size_t key_sz,
                                 const uint8_t iv[12],
                                 const uint8_t *aad, size_t aad_sz,
                                 const uint8_t *in, size_t in_sz,
                                 uint8_t *out, uint8_t tag[16])
{
    uint8_t h[16], j0[16], ctr[16], ks[16], y[16], lens[16];
    uint64_t bits;
    size_t off, n, i;

    if (key == NULL || iv == NULL || tag == NULL ||
        (key_sz != 16u && key_sz != 32u) || (aad_sz != 0u && aad == NULL) ||
        (in_sz != 0u && (in == NULL || out == NULL))) {
        return -2;
    }
    memset(h, 0, sizeof h);
    memset(y, 0, sizeof y);
    if (aes_block(0, key, key_sz, h, h) != 0) {
        goto fail;
    }
    memcpy(j0, iv, 12u);
    j0[12] = j0[13] = j0[14] = 0u;
    j0[15] = 1u;
    ghash(y, h, aad, aad_sz);
    if (decrypt) {
        ghash(y, h, in, in_sz);
    }
    memcpy(ctr, j0, sizeof ctr);
    for (off = 0; off < in_sz; off += n) {
        for (i = 15u; i >= 12u && ++ctr[i] == 0u; i--) {
        }
        if (aes_block(0, key, key_sz, ctr, ks) != 0) {
            goto fail;
        }
        n = in_sz - off < 16u ? in_sz - off : 16u;
        for (i = 0; i < n; i++) {
            out[off + i] = (uint8_t)(in[off + i] ^ ks[i]);
        }
    }
    if (!decrypt) {
        ghash(y, h, out, in_sz);
    }
    for (i = 0; i < 8u; i++) {
        bits = (uint64_t)aad_sz * 8u;
        lens[7u - i] = (uint8_t)(bits >> (8u * i));
        bits = (uint64_t)in_sz * 8u;
        lens[15u - i] = (uint8_t)(bits >> (8u * i));
    }
    ghash(y, h, lens, sizeof lens);
    if (aes_block(0, key, key_sz, j0, ks) != 0) {
        goto fail;
    }
    for (i = 0; i < 16u; i++) {
        tag[i] = (uint8_t)(ks[i] ^ y[i]);
    }
    hw_ops++;
    return 0;
fail:
    hw_errs++;
    return -1;
}

int tiku_crypto_arch_aes_gcm(int decrypt, uint32_t cfg_extra,
                             const uint8_t *key, size_t key_sz,
                             const uint8_t iv[12],
                             const uint8_t *aad, size_t aad_sz,
                             const uint8_t *in, size_t in_sz,
                             uint8_t *out, uint8_t tag[16])
{
    if (cfg_extra != 0u) {
        return -2;
    }
    return tiku_crypto_arch_aes_gcm_kit(decrypt, key, key_sz, iv, aad, aad_sz,
                                        in, in_sz, out, tag);
}
