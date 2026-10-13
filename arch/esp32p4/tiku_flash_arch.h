/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_flash_arch.h - P4 SRAM-based flash reads and bounded test writes.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_FLASH_ARCH_H_
#define TIKU_ESP32P4_FLASH_ARCH_H_
#include <stdint.h>
#include "tiku_flash_layout.h"

#define TIKU_P4_FLASH_SIZE 0x01000000u
#define TIKU_P4_FLASH_SCRATCH ((uint32_t)TIKU_P4_FLASH_SCRATCH_START)
#define TIKU_P4_FLASH_SECTOR_SIZE 4096u
#define TIKU_P4_FLASH_PAGE_SIZE 256u

/** @brief Results from the flash controller. */
typedef enum {
    TIKU_P4_FLASH_OK = 0,
    TIKU_P4_FLASH_PARAM = -1,
    TIKU_P4_FLASH_DOWN = -2,
    TIKU_P4_FLASH_BUSY = -3,
    TIKU_P4_FLASH_IO = -4,
    TIKU_P4_FLASH_NEEDS_ERASE = -5
} tiku_p4_flash_err_t;

/** @brief Attach the board's EF:4018 flash with a 10 MHz single-line clock.
 * @note Requires CPU0, internal SRAM execution and exclusive MSPI ownership;
 * CPU1/LP must be parked, with no XIP or mapped external-memory users.
 */
tiku_p4_flash_err_t tiku_p4_flash_init(void);
/** @brief Return the verified JEDEC bytes, manufacturer in bits 7:0, or zero.
 */
uint32_t tiku_p4_flash_jedec(void);
/** @brief Read SR1/SR2 without changing status or protection bits.
 * @note On failure, *status is unchanged. ISR callers receive BUSY.
 */
tiku_p4_flash_err_t tiku_p4_flash_status(uint16_t *status);
/** @brief Read any byte-aligned range into caller-owned internal SRAM.
 * @note NULL is invalid, including zero length; offset==size permits zero
 * length. A failed read may fill a prefix. Controller timeout requires reset.
 */
tiku_p4_flash_err_t tiku_p4_flash_read(uint32_t offset, void *data,
                                      uint32_t length);
#if TIKU_P4_FLASH_WRITE_TEST || TIKU_P4_STORAGE_TEST || TIKU_P4_FIRMWARE
/** @brief Erase and verify an aligned 4 KiB sector in the writable reservation.
 * @note Scratch qualification permits [0xff8000,0xff9000); storage qualification
 * permits [0x400000,0xff8000). Requires initialized SYSTIMER. A timeout
 * traps with IRQs masked if a mutation leaves flash state uncertain.
 * Interruption may leave the sector incomplete. CPU1/LP and DMA must be stopped.
 */
tiku_p4_flash_err_t tiku_p4_flash_erase_sector(uint32_t offset);
/** @brief Program and verify at most 4096 bytes in the writable reservation.
 * @note Requires initialized SYSTIMER. Sources must be internal SRAM,
 * unchanged until staged. A 0-to-1 bit
 * transition rejects the entire request before writing. I/O failure may leave
 * a programmed prefix. NULL is invalid, including for zero length. Cache
 * exclusion preserves PSRAM data; an uncertain mutation traps as above.
 */
tiku_p4_flash_err_t tiku_p4_flash_program(uint32_t offset, const void *data,
                                         uint32_t length);
#endif
#endif
