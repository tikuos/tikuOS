/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xspi_arch.h - STM32N6 external NOR flash over XSPI2.
 *
 * A page program writes at most 256 bytes and only clears bits; setting bits
 * again takes a sector erase, and each sector endures a finite number of them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_XSPI_ARCH_H_
#define TIKU_STM32N6_XSPI_ARCH_H_

#include <stdint.h>

/** @brief Result of an XSPI operation. */
typedef enum {
    TIKU_XSPI_OK = 0,       /**< success                                   */
    TIKU_XSPI_ERR_ARG,      /**< bad argument or range                     */
    TIKU_XSPI_ERR_TIMEOUT,  /**< a wait timed out or a transfer failed     */
    TIKU_XSPI_ERR_ID,       /**< the device answered with a wrong identity */
    TIKU_XSPI_ERR_PROGRAM,  /**< the device reported erase/program failure */
    TIKU_XSPI_ERR_STATE,    /**< called before a successful init           */
} tiku_xspi_err_t;

/** @brief Identity read from the device. */
typedef struct {
    uint8_t mfr;        /**< 0xC2 expected (Macronix) */
    uint8_t type;       /**< memory type              */
    uint8_t capacity;   /**< capacity code            */
} tiku_xspi_id_t;

/* MX25UM51245G geometry: 64 MB, 256-byte pages, 4 KB sectors.  Addresses are
 * 32-bit: 64 MB does not fit the 24-bit command set. */
#define TIKU_XSPI_SIZE_BYTES    0x04000000UL
#define TIKU_XSPI_PAGE_SIZE     256U
#define TIKU_XSPI_SECTOR_SIZE   4096U
#define TIKU_XSPI_MFR_MACRONIX  0xC2U

/**
 * @brief Base of the memory-mapped read window tiku_xspi_mmap_enable() opens.
 *
 * Erase and program run indirect; the NVM region, the durable mirror and the
 * driver's cache invalidations address the flash through this base.
 */
#define TIKU_XSPI_MMAP_BASE     0x70000000UL

/* Flash layout of the 64 MB device:
 *
 *   0x0000000  FSBL1     256 KB  the boot image; the ROM loads this one
 *   0x0040000  FSBL2     256 KB  the ROM's fallback search address
 *   0x0080000  region      8 MB  the NVM region (tier + TFS store, /data)
 *   0x0880000  unclaimed  ~55 MB model and blob space
 *   0x3FFB000  scratch     4 KB  what `xflash test` erases
 *   0x3FFC000  mirror     16 KB  the durable .uninit mirror
 *
 * The region is 8 MB, what TIKU_TFS_MAX_SLOTS slots of 4 KB can address.
 * TIKU_NVM_REGION_BYTES in kernel/memory/tiku_nvm_region.h holds the same
 * size and must change with it.
 *
 * The mirror is four sectors: a 16-byte header, then the .uninit image.  The
 * linker script fails the link when .uninit outgrows them; its bound is
 * spelled out there and must equal TIKU_XSPI_MIRROR_SECTORS. */
#define TIKU_XSPI_BOOT_SLOT_BYTES   0x40000UL
#define TIKU_XSPI_REGION_ADDR       0x80000UL
#define TIKU_XSPI_REGION_BYTES      (8UL * 1024UL * 1024UL)
#define TIKU_XSPI_MIRROR_SECTORS 4U
#define TIKU_XSPI_MIRROR_BYTES  (TIKU_XSPI_SECTOR_SIZE * TIKU_XSPI_MIRROR_SECTORS)
#define TIKU_XSPI_MIRROR_ADDR   (TIKU_XSPI_SIZE_BYTES - TIKU_XSPI_MIRROR_BYTES)
#define TIKU_XSPI_SCRATCH_ADDR  (TIKU_XSPI_MIRROR_ADDR - TIKU_XSPI_SECTOR_SIZE)

/**
 * @brief Bring up XSPI2, its I/O manager and the pins, then read the identity.
 *
 * @return TIKU_XSPI_OK, or TIKU_XSPI_ERR_TIMEOUT or TIKU_XSPI_ERR_ID with the
 *         driver left unusable
 */
tiku_xspi_err_t tiku_xspi_init(void);

/**
 * @brief Read the JEDEC identity.
 *
 * @param out  Receives the identity
 * @return TIKU_XSPI_OK, TIKU_XSPI_ERR_ARG for NULL @p out,
 *         TIKU_XSPI_ERR_STATE before init, or TIKU_XSPI_ERR_TIMEOUT
 */
tiku_xspi_err_t tiku_xspi_read_id(tiku_xspi_id_t *out);

/**
 * @brief Read from the flash.
 *
 * @param addr  Byte offset into the device
 * @param buf   Destination
 * @param len   Byte count
 * @return TIKU_XSPI_OK, or an error
 */
tiku_xspi_err_t tiku_xspi_read(uint32_t addr, void *buf, uint32_t len);

/**
 * @brief Erase the 4 KB sector containing @p addr.
 *
 * @param addr  Any byte in the sector
 * @return TIKU_XSPI_OK, or an error
 * @note Erase is the wear-limited operation; a sector tolerates a finite count.
 */
tiku_xspi_err_t tiku_xspi_erase_sector(uint32_t addr);

/**
 * @brief Program bytes, splitting the run across page boundaries.
 *
 * @param addr  Byte offset into the device
 * @param buf   Source
 * @param len   Byte count
 * @return TIKU_XSPI_OK, or an error
 * @note Programming only clears bits; the target must be erased first.
 */
tiku_xspi_err_t tiku_xspi_program(uint32_t addr, const void *buf, uint32_t len);

/**
 * @brief Map the flash into the address space for pointer reads.
 *
 * Indirect commands cannot run while the window is live: every read, erase
 * and program takes it down and leaves it down for the caller to reopen.
 *
 * @return TIKU_XSPI_OK, TIKU_XSPI_ERR_STATE before init, or
 *         TIKU_XSPI_ERR_TIMEOUT
 */
tiku_xspi_err_t tiku_xspi_mmap_enable(void);

/**
 * @brief Take the memory-mapped window down so indirect commands can run.
 *
 * @return TIKU_XSPI_OK
 */
tiku_xspi_err_t tiku_xspi_mmap_disable(void);

/**
 * @brief Report the clock the flash is being driven at.
 *
 * @return Interface clock in Hz, or 0 before a successful init
 */
unsigned long tiku_xspi_clock_hz(void);

/**
 * @brief Report whether init has succeeded this boot.
 *
 * @return 1 when the driver is usable
 */
int tiku_xspi_ready(void);

#endif /* TIKU_STM32N6_XSPI_ARCH_H_ */
