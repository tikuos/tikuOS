/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_flash_arch.h - C5 flash mapping and protected storage geometry.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_FLASH_ARCH_H_
#define TIKU_ESP32C5_FLASH_ARCH_H_
#include <stdint.h>

#define TIKU_FLASH_SIZE_BYTES   0x400000UL
#define TIKU_FLASH_MMAP_BASE    0x42000000UL
#define TIKU_FLASH_SECTOR_SIZE  4096u
#define TIKU_FLASH_PAGE_SIZE    256u
#define TIKU_FLASH_BOOT_BYTES   0x100000UL
#define TIKU_FLASH_XIP_ADDR     0x100000UL
#define TIKU_FLASH_XIP_BYTES    0x100000UL
#define TIKU_FLASH_REGION_ADDR  0x200000UL
#define TIKU_FLASH_REGION_BYTES 0x1F7000UL
#define TIKU_FLASH_SCRATCH_ADDR 0x3F7000UL
#define TIKU_FLASH_MIRROR_BYTES 0x4000UL
#define TIKU_FLASH_MIRROR_SLOTS 2u
#define TIKU_FLASH_MIRROR_SLOT(n) (0x3F8000UL + (n) * TIKU_FLASH_MIRROR_BYTES)

typedef enum {
    TIKU_FLASH_OK = 0,
    TIKU_FLASH_ERR_PARAM = -1,
    TIKU_FLASH_ERR_IO = -2,
    TIKU_FLASH_ERR_DOWN = -3,
    TIKU_FLASH_ERR_BUSY = -4
} tiku_flash_err_t;

/** @brief Identify and map a 4 MiB flash; refuse other capacities. */
tiku_flash_err_t tiku_flash_init(void);
/** @brief Return nonzero after mapping and readback validation succeed. */
int tiku_flash_ready(void);
/** @brief Return the JEDEC ID, with manufacturer in the low byte. */
uint32_t tiku_flash_jedec_id(void);
/** @brief Return a mapped flash pointer, or NULL when unavailable/out of range. */
const uint8_t *tiku_flash_map(uint32_t offset);
/** @brief Copy mapped bytes; reject invalid ranges and NULL destinations. */
tiku_flash_err_t tiku_flash_read(uint32_t offset, void *data, uint32_t length);
/** @brief Erase an aligned storage sector; refuse firmware and XIP addresses. */
tiku_flash_err_t tiku_flash_erase_sector(uint32_t offset);
/** @brief Clear bits in storage; preserve neighboring bytes and refuse firmware writes. */
tiku_flash_err_t tiku_flash_program(uint32_t offset, const void *data, uint32_t length);

#endif
