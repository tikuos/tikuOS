/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xflash_arch.h - EK-RA8P1 Octo-SPI NOR (MX25LW51245G, 64 MB).
 *
 * Controller and pin bring-up, identification over the plain 1-1-1 SPI the
 * part powers up in, 8D-8D-8D entry with DQS calibration, and a memory-mapped
 * read/write path exposed as an NVM backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_XFLASH_ARCH_H_
#define TIKU_RA8P1_XFLASH_ARCH_H_

#include <stdint.h>

struct tiku_nvm_backend;

/** @brief Result codes; negative values are failures. */
#define TIKU_RA8P1_XFLASH_OK          0
#define TIKU_RA8P1_XFLASH_ERR_TIMEOUT -1  /**< timeout, or DQS fails at speed */
#define TIKU_RA8P1_XFLASH_ERR_ID      -2  /**< wrong ID, bad args, no DQS eye */
#define TIKU_RA8P1_XFLASH_ERR_BUSY    -3  /**< WIP or WEL did not settle    */
#define TIKU_RA8P1_XFLASH_ERR_RANGE   -4  /**< address, length or alignment */

/** @brief RDID of the EK-RA8P1's part: C2 (Macronix), 86 (1.8 V octal), 3A
 *         (512 Mb).  The 3 V MX25LM reports 85 in the type byte. */
#define TIKU_RA8P1_XFLASH_ID0   0xC2U
#define TIKU_RA8P1_XFLASH_ID1   0x86U
#define TIKU_RA8P1_XFLASH_ID2   0x3AU

/** @brief Mapped window and capacity of the board's part. */
#define TIKU_RA8P1_XFLASH_ADDR   0x90000000UL   /* OSPI0 CS1 */
#define TIKU_RA8P1_XFLASH_BYTES  (64UL * 1024UL * 1024UL)

/** @brief Bring up OSPI0 and its pins, and reset the flash into single-bit
 *         mode.  Idempotent. */
void tiku_ra8p1_xflash_init(void);

/**
 * @brief Issue one manual-command transaction.
 *
 * Every other transfer is built on it: opcode, optional address, optional
 * dummy cycles, up to 8 bytes in or out.  Brings the controller up first.
 *
 * @param cmd        Opcode, already positioned for the active protocol
 * @param addr       Address, ignored when @p addr_bytes is 0
 * @param addr_bytes 0..4
 * @param dummy      Latency cycles between address and data
 * @param data       Buffer read into, or written from; may be NULL when len 0
 * @param len        0..8 bytes
 * @param is_write   Non-zero for a transaction that sends data
 * @return TIKU_RA8P1_XFLASH_OK; ERR_ID when @p len > 8 or @p addr_bytes > 4;
 *         ERR_RANGE for an odd address in octal mode; ERR_TIMEOUT when the
 *         transaction does not complete
 */
int tiku_ra8p1_xflash_cmd(uint16_t cmd, uint32_t addr, uint8_t addr_bytes,
                          uint8_t dummy, void *data, uint8_t len,
                          int is_write);

/**
 * @brief Read @p len bytes of the SFDP parameter table at @p addr.
 *
 * JESD216 puts the signature "SFDP" at offset 0, a known answer to a
 * transaction that carries an address and dummy cycles.
 *
 * @param addr  SFDP offset
 * @param dst   Destination, up to 8 bytes
 * @param len   0..8
 * @return TIKU_RA8P1_XFLASH_OK, or a negative error code
 */
int tiku_ra8p1_xflash_read_sfdp(uint32_t addr, void *dst, uint8_t len);

/**
 * @brief Open the mapped window so the CPU can read flash as memory.
 *
 * Programs the read command for the active protocol, FAST READ 4B in
 * single-bit mode or 8DTRD in octal, turns prefetch on and enables read
 * access for CS1.  tiku_ra8p1_xflash_write() opens write access itself.
 *
 * @return TIKU_RA8P1_XFLASH_OK
 */
int tiku_ra8p1_xflash_mmap_enable(void);

/**
 * @brief Read @p len bytes of the array, whichever protocol is active.
 *
 * @param addr byte offset into the device
 * @param dst  destination, up to 8 bytes (one manual transaction)
 * @param len  byte count, 1..8
 * @return TIKU_RA8P1_XFLASH_OK, or a negative error code
 */
int tiku_ra8p1_xflash_read(uint32_t addr, void *dst, uint8_t len);

/*
 * DTR octal is the only octal this controller can express: LIOCFGCSn.PRTMD
 * has no 8S-8S-8S encoding.  In it the bus runs at up to 125 MHz OM_SCLK on
 * eight lanes at both edges; at reset it runs at 4 MHz on one lane.  Entry is
 * confirmed against the factory SFDP signature, and any failure resets the
 * device to single-bit mode.
 *
 * DOPI transfers bytes pair-swapped (D1 D0 D3 D2 ...) relative to SPI, on
 * the mapped window as well as the command path, and no controller setting
 * undoes it: data reads back correctly only in the protocol it was written
 * in.
 *
 * DOPI also addresses the array in 2-byte units (A0 must be 0).  The device
 * accepts an odd address, reports success and moves the wrong bytes, so odd
 * addresses are refused, and the NVM backend refuses odd lengths too.
 */

/**
 * @brief Switch device and controller to 8D-8D-8D and raise the bus clock.
 *
 * @return TIKU_RA8P1_XFLASH_OK; ERR_ID when no DQS delay works at the slow
 *         clock, ERR_TIMEOUT when none works at speed, or a transfer error
 */
int tiku_ra8p1_xflash_opi_enter(void);

/**
 * @brief Report whether the flash is in 8D-8D-8D.
 *
 * OCTACLK's divider is chosen at OPI entry for the core clock of that time,
 * so the rung code refuses a core-clock change while this is non-zero.
 *
 * @return Non-zero when the device is in octal mode
 */
int tiku_ra8p1_xflash_in_opi(void);

/**
 * @brief Pulse OM_RESET, returning the device to its power-on protocol.
 *
 * The protocol-select bits in CR2 are volatile, so the device answers
 * single-bit SPI after the pulse.  The controller's protocol is left as it
 * was; tiku_ra8p1_xflash_opi_exit() returns both to single-bit mode.
 */
void tiku_ra8p1_xflash_reset(void);

/**
 * @brief Return device and controller to single-bit mode at the slow clock.
 *
 * @return TIKU_RA8P1_XFLASH_OK
 */
int tiku_ra8p1_xflash_opi_exit(void);

/** @brief Non-zero while the octal protocol is active. */
int tiku_ra8p1_xflash_opi_active(void);

/** @brief The DDR sampling extension calibration settled on. */
int tiku_ra8p1_xflash_ddrsmpex(void);

/** @brief The OM_DQS delay cell count calibration settled on. */
int tiku_ra8p1_xflash_dqs_shift(void);

/** @brief Width of the passing DQS window, in delay cells. */
int tiku_ra8p1_xflash_dqs_margin(void);

/** @brief Erase granularities (sector, block) and the program page, in
 *         bytes. */
#define TIKU_RA8P1_XFLASH_SECTOR  4096UL
#define TIKU_RA8P1_XFLASH_BLOCK   65536UL
#define TIKU_RA8P1_XFLASH_PAGE    256UL

/**
 * @brief Erase one 4 KB sector containing @p addr.
 *
 * Blocks until the device reports idle, for at most 2 s.
 *
 * @param addr  Any address inside the sector
 * @return TIKU_RA8P1_XFLASH_OK, or a negative error code
 */
int tiku_ra8p1_xflash_erase_sector(uint32_t addr);

/** @brief Erase the 64 KB block containing @p addr, waiting up to 10 s. */
int tiku_ra8p1_xflash_erase_block(uint32_t addr);

/**
 * @brief Program up to 8 bytes at @p addr, which must not cross a page.
 *
 * Eight bytes is the manual-command data limit; the part's page is 256
 * bytes.  tiku_ra8p1_xflash_write() is the bulk path.
 *
 * @param addr  Destination, within one 256-byte page
 * @param src   Bytes to write
 * @param len   1..8
 * @return TIKU_RA8P1_XFLASH_OK, or a negative error code
 */
int tiku_ra8p1_xflash_program(uint32_t addr, const void *src, uint8_t len);

/**
 * @brief Write @p len bytes at @p addr through the mapped window.
 *
 * @param addr  destination, 64-byte aligned
 * @param src   source, 8-byte aligned
 * @param len   byte count, a multiple of 64
 * @return TIKU_RA8P1_XFLASH_OK, or a negative error code
 * @note The target range must already be erased.
 */
int tiku_ra8p1_xflash_write(uint32_t addr, const void *src, uint32_t len);

/**
 * @brief The external flash as an NVM backend, or NULL if the map failed.
 *
 * Reads are pointer dereferences into the mapped window; writes and erases
 * go through this driver.  Octal mode is entered first.  tiku_tfs_mount()
 * can mount the backend as a volume beside the internal MRAM region.
 */
struct tiku_nvm_backend *tiku_ra8p1_xflash_backend(void);

/**
 * @brief Read the status register (RDSR).
 *
 * @param sr  Receives it
 * @return TIKU_RA8P1_XFLASH_OK, or a negative error code
 */
int tiku_ra8p1_xflash_read_status(uint8_t *sr);

/**
 * @brief Read the JEDEC ID over 1-1-1 SPI.
 *
 * Exercises pins, clock and controller together: only a Macronix part
 * answers 0xC2 in the first byte.
 *
 * @param out  Receives 3 bytes: manufacturer, memory type, density
 * @return TIKU_RA8P1_XFLASH_OK, ERR_ID when the first byte is not 0xC2 or
 *         @p out is NULL, or a transfer error
 */
int tiku_ra8p1_xflash_read_id(uint8_t out[3]);

#endif /* TIKU_RA8P1_XFLASH_ARCH_H_ */
