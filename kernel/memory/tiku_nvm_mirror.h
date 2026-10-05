/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_mirror.h - layout and integrity check for the .uninit NVM mirror.
 *
 * On the mirror platforms (Ambiq, RP2350, STM32N6, ESP32-C61) .persistent lives
 * in SRAM and is copied to NVM at relock.  A 16-byte header carries a CRC-32,
 * so a torn program is refused rather than restored as good.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NVM_MIRROR_H_
#define TIKU_NVM_MIRROR_H_

#include <stdint.h>
#include <stddef.h>

/** V1 mirror magic ('NVMT'): image at word 1, no CRC.  The Ambiq and RP2350
 *  boot restores accept it and the next flush writes V2; nothing writes V1. */
#define TIKU_NVM_MIRROR_MAGIC_V1   0x4E564D54U

/** V2 mirror magic ('NVM2' bytes): 16-byte header, CRC-validated. */
#define TIKU_NVM_MIRROR_MAGIC_V2   0x324D564EU

/** Header size in bytes (and the image offset within the mirror). */
#define TIKU_NVM_MIRROR_HDR_BYTES  16U

/*
 * Header layout -- four 32-bit words.  16 bytes keeps the image at the Ambiq
 * bootrom's program-alignment unit.
 *
 *   word 0  TIKU_NVM_MIRROR_MAGIC_V2
 *   word 1  CRC-32 (reflected, poly 0xEDB88320) over the image bytes
 *   word 2  image length in bytes (the .uninit size at flush time)
 *   word 3  0xFFFFFFFF (reserved; the erased-flash value), or where a port
 *           keeps two mirror slots, the slot's generation (ESP32-C61)
 */

/** Header word indices. */
#define TIKU_NVM_MIRROR_W_MAGIC    0U
#define TIKU_NVM_MIRROR_W_CRC      1U
#define TIKU_NVM_MIRROR_W_LEN      2U
#define TIKU_NVM_MIRROR_W_RSVD     3U

/**
 * @brief What the boot-time mirror restore found.
 *
 * Exposed by tiku_mem_arch_nvm_restore_status() on mirror platforms.
 * CRC_FAIL on an established device means a power cut tore a flush or the
 * mirror decayed; durable state then starts from its defaults.
 */
typedef enum {
    TIKU_NVM_RESTORE_VIRGIN   = 0, /**< no magic: fresh part / erased  */
    TIKU_NVM_RESTORE_V1       = 1, /**< V1 mirror accepted (no CRC)    */
    TIKU_NVM_RESTORE_V2_OK    = 2, /**< CRC-validated restore           */
    TIKU_NVM_RESTORE_CRC_FAIL = 3  /**< V2 magic but CRC mismatch: torn
                                        program detected, not restored  */
} tiku_nvm_restore_t;

/**
 * @brief Continue a CRC-32 over the next bytes of a stream.
 *
 * Takes and returns the running state: start it at 0xFFFFFFFF and finish it
 * with the same mask, as tiku_nvm_crc32() does, so a payload programmed in
 * chunks is checksummed once as the chunks pass rather than in a second pass.
 */
static inline uint32_t tiku_nvm_crc32_update(uint32_t crc, const void *data,
                                             size_t len)
{
    static const uint32_t nib[16] = {
        0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
        0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
        0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
        0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU
    };
    const uint8_t *p = (const uint8_t *)data;
    size_t i;

    for (i = 0; i < len; i++) {
        crc ^= p[i];
        crc = (crc >> 4) ^ nib[crc & 0x0FU];
        crc = (crc >> 4) ^ nib[crc & 0x0FU];
    }
    return crc;
}

/**
 * @brief CRC-32 (reflected, poly 0xEDB88320, init/final 0xFFFFFFFF).
 *
 * A nibble-table implementation: 64 bytes of table and two lookups per byte,
 * small enough to inline in every mirror backend.
 */
static inline uint32_t tiku_nvm_crc32(const void *data, size_t len)
{
    return tiku_nvm_crc32_update(0xFFFFFFFFU, data, len) ^ 0xFFFFFFFFU;
}

/**
 * @brief The image a mirror holds, or NULL when it holds none that checks.
 *
 * Only a V2 mirror qualifies: its length must fit in @p cap, the mirror's size
 * with the header, and its CRC must match.  The boot restore and the layout
 * service's search for an older image's record read the same verdict.
 */
static inline const uint8_t *
tiku_nvm_mirror_image(const uint32_t *hdr, size_t cap, size_t *len)
{
    const uint8_t *img = (const uint8_t *)hdr + TIKU_NVM_MIRROR_HDR_BYTES;
    size_t n = (size_t)hdr[TIKU_NVM_MIRROR_W_LEN];

    *len = 0u;
    if (hdr[TIKU_NVM_MIRROR_W_MAGIC] != TIKU_NVM_MIRROR_MAGIC_V2 ||
        cap < TIKU_NVM_MIRROR_HDR_BYTES ||
        n > cap - TIKU_NVM_MIRROR_HDR_BYTES ||
        tiku_nvm_crc32(img, n) != hdr[TIKU_NVM_MIRROR_W_CRC]) {
        return NULL;
    }
    *len = n;
    return img;
}

#endif /* TIKU_NVM_MIRROR_H_ */
