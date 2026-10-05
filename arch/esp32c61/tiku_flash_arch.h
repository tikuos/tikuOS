/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flash_arch.h - ESP32-C61 external flash driver.
 *
 * The ROM's routines erase and program it; the MMU maps it, one-to-one and
 * cached, into the external-memory window for pointer reads and XIP code.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_FLASH_ARCH_H_
#define TIKU_ESP32C61_FLASH_ARCH_H_

#include <stdint.h>

/** @brief Result of a flash call. */
typedef enum {
    TIKU_FLASH_OK        = 0,
    TIKU_FLASH_ERR_PARAM = -1,  /**< NULL buffer, or range outside the part */
    TIKU_FLASH_ERR_IO    = -2,  /**< the ROM reported an error or a timeout */
    TIKU_FLASH_ERR_DOWN  = -3,  /**< init has not succeeded */
} tiku_flash_err_t;

#define TIKU_FLASH_SIZE_BYTES   0x00800000UL    /* the DevKitC's 8 MB part */
#define TIKU_FLASH_SECTOR_SIZE  4096U
#define TIKU_FLASH_BLOCK_SIZE   0x10000U
#define TIKU_FLASH_PAGE_SIZE    256U

/* Flash offset 0 appears here, and every offset at the same distance on. */
#define TIKU_FLASH_MMAP_BASE    0x42000000UL

/* Layout of the 8 MB part:
 *
 *   0x000000  boot        2 MB  the boot image; the factory app lives here
 *   0x200000  /data       5 MB  the carved NVM region (tier + TFS store)
 *   0x700000  unclaimed  ~1 MB
 *   0x7F7000  scratch     4 KB  what tests may erase
 *   0x7F8000  mirror 1   16 KB  the durable .uninit mirror, two slots
 *   0x7FC000  mirror 0   16 KB  written in turn (tiku_mem_arch.c)
 *
 * The region starts past the 2 MB a factory backup covers, and on a 64 KB
 * boundary, the MMU's page. */
#define TIKU_FLASH_BOOT_BYTES       0x200000UL
#define TIKU_FLASH_REGION_ADDR      0x200000UL
#define TIKU_FLASH_REGION_BYTES     (5UL * 1024UL * 1024UL)
#define TIKU_FLASH_MIRROR_SECTORS   4U
#define TIKU_FLASH_MIRROR_BYTES     (TIKU_FLASH_SECTOR_SIZE * TIKU_FLASH_MIRROR_SECTORS)
#define TIKU_FLASH_MIRROR_SLOTS     2U
#define TIKU_FLASH_MIRROR_SLOT(i)   (TIKU_FLASH_SIZE_BYTES - \
                                     ((i) + 1UL) * TIKU_FLASH_MIRROR_BYTES)
#define TIKU_FLASH_SCRATCH_ADDR     (TIKU_FLASH_SIZE_BYTES - TIKU_FLASH_SECTOR_SIZE - \
                                     TIKU_FLASH_MIRROR_SLOTS * TIKU_FLASH_MIRROR_BYTES)

/**
 * @brief Attach the part, lift write protection, map it, read its identity.
 *
 * @return TIKU_FLASH_OK, or an error leaving the driver down
 * @note Call before tiku_esp32c61_psram_init(): the mapping resets every MMU
 *       entry, the PSRAM's included.
 */
tiku_flash_err_t tiku_flash_init(void);

/** @brief Whether init succeeded this boot. @return 1 when usable */
int tiku_flash_ready(void);

/** @brief The JEDEC identity init read, manufacturer in the low byte. */
uint32_t tiku_flash_jedec_id(void);

/**
 * @brief Read through the mapped window.
 *
 * @param addr  Byte offset into the part
 * @param buf   Destination
 * @param len   Byte count
 * @return TIKU_FLASH_OK, or an error
 */
tiku_flash_err_t tiku_flash_read(uint32_t addr, void *buf, uint32_t len);

/**
 * @brief Erase the 4 KB sector containing @p addr.
 *
 * @param addr  Any byte in the sector
 * @return TIKU_FLASH_OK, or an error
 * @note Erase is the wear-limited operation; a sector tolerates a finite count.
 */
tiku_flash_err_t tiku_flash_erase_sector(uint32_t addr);

/**
 * @brief Program bytes at any alignment; programming only clears bits.
 *
 * @param addr  Byte offset into the part
 * @param buf   Source
 * @param len   Byte count
 * @return TIKU_FLASH_OK, or an error
 */
tiku_flash_err_t tiku_flash_program(uint32_t addr, const void *buf, uint32_t len);

/** @brief Where @p addr reads by pointer, or NULL when the driver is down or
 *         @p addr is past the part. */
const uint8_t *tiku_flash_map(uint32_t addr);

#endif /* TIKU_ESP32C61_FLASH_ARCH_H_ */
