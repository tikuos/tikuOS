/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_ccm_arch.c - CCM00 hardware AES-CCM for the BLE link layer.
 *
 * Register recipe (datasheet 8.4, decoded against the Core-spec v5.4
 * Vol 6 Part C sample data the datasheet itself uses):
 *   - KEY.VALUE[0..3]   = the 16-byte session key byte-REVERSED, read as
 *                         little-endian words (VALUE[0] low byte = sk[15]).
 *   - NONCE.VALUE[0..3] = the 13-byte BLE nonce byte-reversed into the
 *                         low 13 bytes (VALUE[0] low byte = nonce[12]),
 *                         top 3 bytes zero.  With the tiku_ble_enc_nonce
 *                         layout (ctr LSO || dir || IV LSO) this lands
 *                         exactly on the datasheet's worked example.
 *   - IN/OUT are TYPED MVDMA job lists: ALEN(11,2-byte l(a)) MLEN(12,
 *     2-byte l(m)) ADATA(13,a) MDATA(14,m or c) then a NULL terminator.
 *     Plain-data jobs leave the engine waiting forever -- the Phase-E
 *     "standalone crypt never completes" wall.
 *   - Job lists and field buffers must be word-aligned (probe-proven).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#if defined(PLATFORM_NORDIC)

#include <arch/nordic/tiku_ble_ccm_arch.h>
#include <arch/nordic/tiku_device_select.h>
#include <arch/nordic/tiku_crypto_arch.h>      /* software oracle (selftest) */
#include <interfaces/bluetooth/tiku_ble_enc.h> /* nonce layout (selftest)    */
#include <string.h>

#define CCM  NRF_CCM00_S

/* MVDMA job element: {pointer, (attribute << 24) | count}. */
#define CCM_JOB(p, n, attr) \
    (uint32_t)(p), (((uint32_t)(attr) << 24) | ((uint32_t)(n) & 0xFFFFFFu))
#define CCM_ATTR_ALEN   11u
#define CCM_ATTR_MLEN   12u
#define CCM_ATTR_ADATA  13u
#define CCM_ATTR_MDATA  14u

/* Static, word-aligned working set: job lists + the small typed fields.
 * One crypt at a time (single-threaded LL use). */
static uint32_t ccm_injob[10] __attribute__((aligned(4)));
static uint32_t ccm_outjob[10] __attribute__((aligned(4)));
static uint16_t ccm_alen_in  __attribute__((aligned(4)));
static uint16_t ccm_mlen_in  __attribute__((aligned(4)));
static uint16_t ccm_alen_out __attribute__((aligned(4)));
static uint16_t ccm_mlen_out __attribute__((aligned(4)));
static uint8_t  ccm_aad_in   __attribute__((aligned(4)));
static uint8_t  ccm_aad_out  __attribute__((aligned(4)));

/* Byte-reverse @p n bytes of @p src into the low bytes of the 16-byte
 * @p val register image (top bytes zero), then it is memcpy-ready. */
static void ccm_rev_load(volatile uint32_t val[4], const uint8_t *src,
                         uint8_t n)
{
    uint8_t img[16];
    uint8_t i;
    for (i = 0u; i < 16u; i++) {
        img[i] = (i < n) ? src[n - 1u - i] : 0u;
    }
    for (i = 0u; i < 4u; i++) {
        val[i] = (uint32_t)img[4u * i] |
                 ((uint32_t)img[4u * i + 1u] << 8) |
                 ((uint32_t)img[4u * i + 2u] << 16) |
                 ((uint32_t)img[4u * i + 3u] << 24);
    }
}

int tiku_ble_ccm_arch_crypt(int decrypt, const uint8_t sk[16],
                            const uint8_t nonce[13], uint8_t aad,
                            const uint8_t *in, uint8_t len, uint8_t *out)
{
    uint32_t spin;
    uint8_t  j;

    CCM->ENABLE = 2u;                            /* CCM enable code          */
    CCM->MODE = (decrypt ? 2u : 0u)              /* Enc / FastDecryption     */
              | (0u << 8)                        /* PROTOCOL = Ble           */
              | (3u << 16)                       /* DATARATE = 1M            */
              | (1u << 24);                      /* MACLEN   = M4            */
    CCM->ADATAMASK = 0xE3u;                      /* BLE: NESN/SN/MD masked   */
    ccm_rev_load(CCM->KEY.VALUE, sk, 16u);
    ccm_rev_load(CCM->NONCE.VALUE, nonce, 13u);

    ccm_alen_in  = 1u;
    ccm_aad_in   = aad;
    j = 0u;
    if (decrypt) {
        ccm_mlen_in = (uint16_t)(len + 4u);      /* l(c) = payload + MIC     */
    } else {
        ccm_mlen_in = len;                       /* l(m)                     */
    }
    ccm_injob[j++] = (uint32_t)&ccm_alen_in;
    ccm_injob[j++] = (2u) | (CCM_ATTR_ALEN << 24);
    ccm_injob[j++] = (uint32_t)&ccm_mlen_in;
    ccm_injob[j++] = (2u) | (CCM_ATTR_MLEN << 24);
    ccm_injob[j++] = (uint32_t)&ccm_aad_in;
    ccm_injob[j++] = (1u) | (CCM_ATTR_ADATA << 24);
    ccm_injob[j++] = (uint32_t)in;
    ccm_injob[j++] = ((uint32_t)ccm_mlen_in) | (CCM_ATTR_MDATA << 24);
    ccm_injob[j++] = 0u;                         /* NULL terminator          */
    ccm_injob[j++] = 0u;

    j = 0u;
    ccm_outjob[j++] = (uint32_t)&ccm_alen_out;
    ccm_outjob[j++] = (2u) | (CCM_ATTR_ALEN << 24);
    ccm_outjob[j++] = (uint32_t)&ccm_mlen_out;
    ccm_outjob[j++] = (2u) | (CCM_ATTR_MLEN << 24);
    ccm_outjob[j++] = (uint32_t)&ccm_aad_out;
    ccm_outjob[j++] = (1u) | (CCM_ATTR_ADATA << 24);
    ccm_outjob[j++] = (uint32_t)out;
    ccm_outjob[j++] = ((uint32_t)(decrypt ? len : (uint8_t)(len + 4u)))
                    | (CCM_ATTR_MDATA << 24);
    ccm_outjob[j++] = 0u;
    ccm_outjob[j++] = 0u;

    CCM->IN.PTR  = (uint32_t)ccm_injob;
    CCM->OUT.PTR = (uint32_t)ccm_outjob;
    CCM->EVENTS_END   = 0u;
    CCM->EVENTS_ERROR = 0u;
    (void)CCM->EVENTS_END;
    CCM->TASKS_START = 1u;

    for (spin = 0u; spin < 4000000u; spin++) {
        if (CCM->EVENTS_END != 0u || CCM->EVENTS_ERROR != 0u) {
            break;
        }
    }
    if (CCM->EVENTS_ERROR != 0u || CCM->EVENTS_END == 0u) {
        int err = (int)(CCM->ERRORSTATUS & 0xFFu);
        CCM->TASKS_STOP = 1u;
        CCM->ENABLE = 0u;
        return -2 - err;
    }
    if (decrypt && (CCM->MACSTATUS & 1u) == 0u) {
        CCM->ENABLE = 0u;
        return -1;                               /* MIC check failed         */
    }
    CCM->ENABLE = 0u;
    return 0;
}

int tiku_ble_ccm_arch_selftest(void)
{
    /* Core-spec v5.4 Vol 6 Part C sample session: SK + IV as the datasheet's
     * own worked register example, so the byte-order recipe is cross-checked
     * against both the spec AND our two-board-proven software CCM. */
    static const uint8_t sk[16] = {
        0x99u, 0xADu, 0x1Bu, 0x52u, 0x26u, 0xA3u, 0x7Eu, 0x3Eu,
        0x05u, 0x8Eu, 0x3Bu, 0x8Eu, 0x27u, 0xC2u, 0xC6u, 0x66u
    };
    static const uint8_t iv[8] = {                /* IVm||IVs, LSO (E3c)     */
        0x24u, 0xABu, 0xDCu, 0xBAu, 0xBEu, 0xBAu, 0xAFu, 0xDEu
    };
    static const uint8_t pt[17] = {
        'T','I','K','U','-','C','C','M','0','0','-','I','N','L','I','N','E'
    };
    uint8_t nonce[13], aad_masked, hdr = 0x02u;   /* LLID=2: survives mask   */
    uint8_t sw_ct[21], sw_mic[4];
    uint8_t hw_ct[24], hw_pt[24];
    int r, rc = 0;

    tiku_ble_enc_nonce(nonce, 1u, 1u, iv);        /* ctr=1 dir=1 (datasheet) */
    aad_masked = (uint8_t)(hdr & 0xE3u);          /* NESN/SN/MD masked       */

    /* Software oracle: ciphertext + MIC via the E3c CRACEN path. */
    if (tiku_crypto_arch_aes_ccm_star(0, sk, 16u, nonce, &aad_masked, 1u,
                                      pt, sizeof(pt), 4u, sw_ct,
                                      sw_mic) != 0) {
        return 0;
    }

    /* 1: hardware encrypt matches software ct||mic. */
    r = tiku_ble_ccm_arch_crypt(0, sk, nonce, hdr, pt, sizeof(pt), hw_ct);
    if (r == 0 && memcmp(hw_ct, sw_ct, sizeof(pt)) == 0 &&
        memcmp(&hw_ct[sizeof(pt)], sw_mic, 4u) == 0) {
        rc |= 1;
    }

    /* 2: hardware decrypt round-trips with MIC verified. */
    r = tiku_ble_ccm_arch_crypt(1, sk, nonce, hdr, hw_ct, sizeof(pt), hw_pt);
    if (r == 0 && memcmp(hw_pt, pt, sizeof(pt)) == 0) {
        rc |= 2;
    }

    /* 3: a tampered MIC is rejected. */
    hw_ct[sizeof(pt)] ^= 0x5Au;
    r = tiku_ble_ccm_arch_crypt(1, sk, nonce, hdr, hw_ct, sizeof(pt), hw_pt);
    if (r == -1) {
        rc |= 4;
    }

    return rc;
}

#endif /* PLATFORM_NORDIC */
