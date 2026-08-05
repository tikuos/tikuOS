/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xflash_arch.h - EK-RA8P1 Octo-SPI NOR (MX25LW51245G, 64 MB).
 *
 * Phase 1: controller and pins up, device identified over plain 1-1-1 SPI,
 * which is the mode the part powers up in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_XFLASH_ARCH_H_
#define TIKU_RA8P1_XFLASH_ARCH_H_

#include <stdint.h>

struct tiku_nvm_backend;

#define TIKU_RA8P1_XFLASH_OK          0
#define TIKU_RA8P1_XFLASH_ERR_TIMEOUT -1  /**< transaction never completed  */
#define TIKU_RA8P1_XFLASH_ERR_ID      -2  /**< no Macronix device answered  */
#define TIKU_RA8P1_XFLASH_ERR_BUSY    -3  /**< device still busy after tMAX */
#define TIKU_RA8P1_XFLASH_ERR_RANGE   -4  /**< off the device, or spans a page */

/** @brief Known-good RDID on the EK-RA8P1: C2 (Macronix) 86 (1.8 V octa) 3A
 *         (512 Mb).  The 3 V LM sibling reports 85 in the type byte, which is
 *         how the silicon distinguishes the two when the docs disagree. */
#define TIKU_RA8P1_XFLASH_ID0   0xC2U
#define TIKU_RA8P1_XFLASH_ID1   0x86U
#define TIKU_RA8P1_XFLASH_ID2   0x3AU

/** @brief Mapped window and capacity of the board's part. */
#define TIKU_RA8P1_XFLASH_ADDR   0x90000000UL   /* OSPI0 CS1 */
#define TIKU_RA8P1_XFLASH_BYTES  (64UL * 1024UL * 1024UL)

/** @brief Bring up the OSPI1 controller and its pins.  Idempotent. */
void tiku_ra8p1_xflash_init(void);

/**
 * @brief Issue one manual-command transaction.
 *
 * The single primitive every other call is built from: opcode, optional
 * address, optional dummy cycles, up to 8 bytes in or out.
 *
 * @param cmd        Opcode, already positioned for the active protocol
 * @param addr       Address, ignored when @p addr_bytes is 0
 * @param addr_bytes 0..4
 * @param dummy      Latency cycles between address and data
 * @param data       Buffer read into, or written from; may be NULL when len 0
 * @param len        0..8 bytes
 * @param is_write   Non-zero for a transaction that sends data
 * @return TIKU_RA8P1_XFLASH_OK, or a negative error code
 */
int tiku_ra8p1_xflash_cmd(uint16_t cmd, uint32_t addr, uint8_t addr_bytes,
                          uint8_t dummy, void *data, uint8_t len,
                          int is_write);

/**
 * @brief Read @p len bytes of the SFDP parameter table at @p addr.
 *
 * Self-verifying at offset 0, where JESD216 requires the signature "SFDP" --
 * which is why this is the first transaction to carry an address and dummy
 * cycles rather than a data read the caller has to trust.
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
 * Programs the command map with FAST READ 4B (0x0C, four address bytes, eight
 * dummy cycles) and enables read access for CS1.  Reads only: the window
 * stays write-disabled, so a stray store cannot start a program cycle.
 *
 * @return TIKU_RA8P1_XFLASH_OK, or a negative error code
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
 * DTR octal is not a preference but the only octal this controller can
 * express: LIOCFGCSn.PRTMD has no 8S-8S-8S encoding.  On success the bus runs
 * at OM_SCLK = 120 MHz on eight lanes at both edges, against 4 MHz on one
 * lane at reset.  Entry is confirmed against the factory SFDP signature, and
 * any failure resets the device to single-bit mode rather than leaving it in
 * a state nothing can talk to.
 *
 * WARNING: DOPI transfers bytes PAIR-SWAPPED (D1 D0 D3 D2 ...) relative to
 * SPI, on the mapped window as well as the command path -- and no controller
 * setting undoes it, so software cannot hide it from a memory-mapped read.
 * Read data back in the protocol it was written in.
 *
 * DOPI also addresses the array in 2-byte units (A0 must be 0).  Odd
 * addresses and odd lengths are REFUSED rather than obeyed, because the
 * device accepts them, reports success, and moves the wrong bytes.
 */

/**
 * @brief Switch device and controller to 8D-8D-8D and raise the bus clock.
 *
 * @return TIKU_RA8P1_XFLASH_OK, or a negative error code
 */
int tiku_ra8p1_xflash_opi_enter(void);

/**
 * @brief Pulse OM_RESET, returning the device to its power-on protocol.
 *
 * The unconditional escape hatch: the protocol-select bits in CR2 are
 * volatile, so a device left speaking something the controller cannot is
 * always one pulse from answering single-bit commands again.
 */
void tiku_ra8p1_xflash_reset(void);

/** @brief Return device and controller to single-bit mode at the slow clock. */
int tiku_ra8p1_xflash_opi_exit(void);

/** @brief Non-zero while the octal protocol is active. */
int tiku_ra8p1_xflash_opi_active(void);

/** @brief The DDR sampling extension calibration settled on. */
int tiku_ra8p1_xflash_ddrsmpex(void);

/** @brief The OM_DQS delay cell count calibration settled on. */
int tiku_ra8p1_xflash_dqs_shift(void);

/** @brief How many delay cells worked -- the width of the eye, in cells. */
int tiku_ra8p1_xflash_dqs_margin(void);

/** @brief Erase granularities this driver issues, in bytes. */
#define TIKU_RA8P1_XFLASH_SECTOR  4096UL
#define TIKU_RA8P1_XFLASH_BLOCK   65536UL
#define TIKU_RA8P1_XFLASH_PAGE    256UL

/**
 * @brief Erase one 4 KB sector containing @p addr.
 *
 * Blocks until the device reports idle.  tSE is 25 ms typical but 400 ms
 * worst case, so the wait is sized from the datasheet maximum rather than
 * from what a healthy part happens to do.
 *
 * @param addr  Any address inside the sector
 * @return TIKU_RA8P1_XFLASH_OK, or a negative error code
 */
int tiku_ra8p1_xflash_erase_sector(uint32_t addr);

/** @brief Erase the 64 KB block containing @p addr (tBE up to 2 s). */
int tiku_ra8p1_xflash_erase_block(uint32_t addr);

/**
 * @brief Program up to 8 bytes at @p addr, which must not cross a page.
 *
 * Eight is the manual-command data limit, not the part's: a page is 256
 * bytes.  Bulk writing wants the mapped path, which this exists to validate
 * rather than replace.
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
 */
int tiku_ra8p1_xflash_write(uint32_t addr, const void *src, uint32_t len);

/*
 * The octal flash as an NVM region, which is what lets the existing store
 * layers use it unchanged: reads are already pointer dereferences into the
 * mapped window, and write/erase are the only things that differ from MRAM.
 * tiku_tfs_mount() takes a backend by argument, so this is a second volume
 * alongside the carved internal region rather than a replacement for it.
 */

/** @brief The external flash as an NVM backend, or NULL if the map failed. */
struct tiku_nvm_backend *tiku_ra8p1_xflash_backend(void);

/** @brief Read the status register (RDSR). @param sr Receives it @return rc */
int tiku_ra8p1_xflash_read_status(uint8_t *sr);

/**
 * @brief Read the JEDEC ID over 1-1-1 SPI.
 *
 * The identifying transaction, and deliberately the first one: it proves
 * pins, clock and controller together, and its answer is self-checking
 * because only one manufacturer byte is correct.
 *
 * @param out  Receives 3 bytes: manufacturer, memory type, density
 * @return TIKU_RA8P1_XFLASH_OK, or a negative error code
 */
int tiku_ra8p1_xflash_read_id(uint8_t out[3]);

#endif /* TIKU_RA8P1_XFLASH_ARCH_H_ */
