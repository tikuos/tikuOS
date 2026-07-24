/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_mirror.h - shared layout + integrity check for the .uninit
 *                     NVM mirror (Ambiq MRAM page, RP2350 flash sector)
 *
 * On the mirror platforms the live copy of .persistent state is SRAM;
 * durability is a whole-region snapshot programmed to NVM at MPU-relock
 * time.  V1 of that snapshot was validated by a single magic word — but
 * the magic is programmed at offset 0, FIRST, so a power cut mid-program
 * could leave a torn image wearing a valid magic, and the next boot
 * would restore corrupt state as good.  V2 closes that hole: a 16-byte
 * header carrying a CRC-32 over the image, checked before any restore.
 * A torn program fails the CRC and the boot falls back to per-subsystem
 * first-boot priming — crash-consistent, never silently corrupt.
 *
 * Header layout (four 32-bit words; 16 bytes keeps the image at the
 * Ambiq bootrom's 16-byte program-alignment unit):
 *
 *   word 0  TIKU_NVM_MIRROR_MAGIC_V2
 *   word 1  CRC-32 (reflected, poly 0xEDB88320) over the image bytes
 *   word 2  image length in bytes (the .uninit size at flush time)
 *   word 3  0xFFFFFFFF (reserved; erased-flash value)
 *
 * Flush-cost note: the dirty check compares the IMAGE span (and the
 * magic/length words) against the mirror first; the CRC is computed
 * only when the image actually changed and a program is unavoidable.
 * Clean relocks — the per-packet TCP path — never pay for the CRC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NVM_MIRROR_H_
#define TIKU_NVM_MIRROR_H_

#include <stdint.h>
#include <stddef.h>

/** V1 mirror magic ('NVMT'): image follows at word 1, no integrity
 *  check.  Still ACCEPTED by the boot restore so devices upgrade
 *  seamlessly — the first post-upgrade flush rewrites the mirror as
 *  V2.  Never written anymore. */
#define TIKU_NVM_MIRROR_MAGIC_V1   0x4E564D54U

/** V2 mirror magic ('NVM2' bytes): 16-byte header, CRC-validated. */
#define TIKU_NVM_MIRROR_MAGIC_V2   0x324D564EU

/** Header size in bytes (and the image offset within the mirror). */
#define TIKU_NVM_MIRROR_HDR_BYTES  16U

/** Header word indices. */
#define TIKU_NVM_MIRROR_W_MAGIC    0U
#define TIKU_NVM_MIRROR_W_CRC      1U
#define TIKU_NVM_MIRROR_W_LEN      2U
#define TIKU_NVM_MIRROR_W_RSVD     3U

/**
 * @brief What the boot-time mirror restore found.
 *
 * Exposed by tiku_mem_arch_nvm_restore_status() on mirror platforms.
 * CRC_FAIL on an established device means a flush was torn by a power
 * cut (or the mirror rotted) — state fell back to defaults by design.
 */
typedef enum {
    TIKU_NVM_RESTORE_VIRGIN   = 0, /**< no magic: fresh part / erased  */
    TIKU_NVM_RESTORE_V1       = 1, /**< legacy mirror accepted (upgrade) */
    TIKU_NVM_RESTORE_V2_OK    = 2, /**< CRC-validated restore           */
    TIKU_NVM_RESTORE_CRC_FAIL = 3  /**< V2 magic but CRC mismatch: torn
                                        program detected, NOT restored  */
} tiku_nvm_restore_t;

/**
 * @brief CRC-32 (reflected, poly 0xEDB88320, init/final 0xFFFFFFFF).
 *
 * Nibble-table implementation: 64 bytes of table, ~2 lookups/byte —
 * small enough for every mirror backend to inline, fast enough that
 * even the RP2350's full 4 KB sector costs well under a millisecond.
 * Also used for the hibernate-marker integrity field.
 */
static inline uint32_t tiku_nvm_crc32(const void *data, size_t len)
{
    static const uint32_t nib[16] = {
        0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
        0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
        0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
        0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU
    };
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    size_t i;

    for (i = 0; i < len; i++) {
        crc ^= p[i];
        crc = (crc >> 4) ^ nib[crc & 0x0FU];
        crc = (crc >> 4) ^ nib[crc & 0x0FU];
    }
    return crc ^ 0xFFFFFFFFU;
}

#endif /* TIKU_NVM_MIRROR_H_ */
