/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nor_arch.h - Apollo510 MSPI1 and external octal NOR flash (8 MB).
 *
 * Reads are cheap and can go through the XIP aperture.  A page program writes
 * at most 256 B and only clears bits; setting them takes an erase, and each
 * sector endures about 100,000.  The driver counts its erases.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NOR_ARCH_H_
#define TIKU_NOR_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TABLE 0 -- PINS                                                           */
/*---------------------------------------------------------------------------*/
/*
 * The board header defines the pads (TIKU_BOARD_NOR_PAD_*).  Only the
 * Apollo510 (green) EVB carries the NOR (U12) and declares the capability:
 *
 *   signal      pad          note
 *   ----------  -----------  ----------------------------------------------
 *   D0..D7      GP95..GP102  MSPI1 data, FNCSEL 0
 *   SCK         GP103        FNCSEL 0
 *   DQS/DM      GP104        FNCSEL 0
 *   CE0         GP53         MSPI1 CE0
 *   RSTn        GP54         plain GPIO; the BSP names this pad MSPI1_CE1
 *   LS_EN       GP208        load-switch enable, polarity unconfirmed
 *
 * The Apollo510B (Blue) EVB has no U12: its BSP defines no MSPI1 chip select
 * and am_bsp_mspi_pins_enable() has the module-1 CE pinconfig commented out,
 * and the Makefile refuses a NOR build for it.  AP510EVB_Rev2.2_Schematic.pdf
 * (title block "Apollo510 EVB", SoC AP510NFA-CBR) is the green board's
 * schematic; its RSTn GP54 and load switch GP208 do not apply to the Blue
 * board, whose BSP gives MSPI1 reset as GP17.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 1 -- BRING-UP SEQUENCE                                             */
/*---------------------------------------------------------------------------*/
/*
 * The controller comes up as the PSRAM's does (power the MSPI1 domain, force
 * the oscillator, select and enable the IO clock, program DEV0CFG, pads after
 * the controller), with MSPI1's base and pads.  The device wakes in 1-line
 * SPI and is switched to octal DDR by command, so bring-up has two stages:
 *
 *  SERIAL (tiku_nor_init_serial)
 *   1  controller at 24 MHz, DEVCFG0 = SERIAL0, 1-byte instruction, 4-byte
 *      address, TURNAROUND 10, SEPIO, no DDR, PADOUTEN = SERIAL0; then pads
 *   2  reset pulse on RSTn (low 50 us, then 500 us recovery); the load
 *      switch is not touched
 *   3  RESET_ENABLE (0x66) then RESET_MEMORY (0x99) -- software reset
 *   4  READ_ID (0x9F) through tiku_nor_read_id(): manufacturer 0x9D (ISSI)
 *
 *  OCTAL DDR (tiku_nor_enter_octal)
 *   5  READ_ID again, with non-volatile CR[6]; unless CR[6] reads 0xFF (XIP
 *      disabled) the driver returns TIKU_NOR_ERR_STATE.  It never writes
 *      non-volatile configuration, which is permanent.
 *   6  WRITE_ENABLE (0x06), ENTER_4BYTE_ADDRESS_MODE (0xB7)
 *   7  WRITE volatile CR[0x00] = 0xE7 (octal DDR)   <-- volatile, reversible
 *   8  serial READ_ID, traced only: a device still answering 0x9D did not
 *      take the VCR write
 *   9  reconfigure the controller: 2-byte instructions, TURNAROUND 31,
 *      EMULATEDDR, DQS enabled, read op 0xFDFD, write op 0x1212
 *  10  octal WRITE_DISABLE, then READ_ID in octal for the trace only: octal
 *      READ_ID is unreliable on this part, so entry does not gate on it
 *
 * The DMA boundary is none for this part, as in the vendor driver: NOR has
 * no DRAM rows or refresh to break bursts for.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 2 -- COMMAND SET                                                   */
/*---------------------------------------------------------------------------*/
/*
 * SERIAL (1 byte opcode, 1 data line):
 *   0x9F READ_ID            -> 3+ bytes: mfr, type, capacity
 *   0x66/0x99 RESET_ENABLE/RESET_MEMORY   (must be issued as a pair)
 *   0x06 WRITE_ENABLE       (arms exactly one program/erase)
 *   0x04 WRITE_DISABLE
 *   0x05 READ_STATUS        -> bit0 WIP (1 = busy)
 *   0x70 READ_FLAG_STATUS   -> program/erase error bits
 *   0xB7 ENTER_4BYTE_ADDR   0xE9 EXIT
 *   0x85/0x81 READ/WRITE VOLATILE CR      (reversible; mode lives here)
 *   0xB5 READ NON-VOLATILE CR; 0xB1 (write, permanent) is not in the driver
 *   0x0C FAST_READ_4B       0x12 PAGE_PROGRAM_4B
 *   0x21 SUBSECTOR_ERASE_4B (4 KB)   0xDC SECTOR_ERASE_4B (128 KB)
 *   0xC7 CHIP_ERASE         not in the driver
 *
 * OCTAL DDR (2-byte duplicated opcodes, 8 data lines, like the PSRAM):
 *   0xFDFD read   0x1212 page program   0x2121 subsector erase
 *   0xDCDC sector erase   0x0606 / 0x0404 write enable / disable
 *   0x0505 read status    0x9F9F read ID   0x6666 / 0x9999 reset
 *
 * GEOMETRY: page 256 B (program unit), subsector 4 KB, sector 128 KB
 * (64 sectors of 128 KB = 8 MB), 4-byte addressing throughout.
 *
 * STATUS: every program/erase is WREN -> op -> WIP poll.  Programs poll every
 * 100 us up to 5 ms, erases every 10 ms up to 10 s, with a hang-detector
 * check-in per poll; a tight status spin is APB traffic into the busy
 * controller.
 */

/*---------------------------------------------------------------------------*/
/* PUBLIC CONSTANTS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief MSPI1 XIP aperture base, 0x80000000..0x84000000.
 *
 * MSPI1_APERTURE_START_ADDR in am_reg_base_addresses.h.  MSPI0's base plus a
 * 0x04000000 stride is 0x64000000, inside MSPI0's PSRAM window; a read there
 * reaches the wrong controller and stalls the bus with no software recovery.
 */
#define TIKU_NOR_XIP_BASE     0x80000000UL
/** @brief Device size: 64 Mbit. */
#define TIKU_NOR_SIZE_BYTES   0x00800000UL
/** @brief Program unit. */
#define TIKU_NOR_PAGE_SIZE    256u
/** @brief Small erase unit. */
#define TIKU_NOR_SUBSECTOR    4096u
/** @brief Large erase unit. */
#define TIKU_NOR_SECTOR_SIZE  0x20000UL
/** @brief Expected manufacturer id (ISSI). */
#define TIKU_NOR_MFR_ISSI     0x9Du

/**
 * @brief Scratch sector: the last 128 KB of the die.
 *
 * tiku_nor_erase() refuses a lower address unless forced, and the self-tests
 * write only here.  Endurance is about 100k erases per sector, and the driver
 * counts every erase (tiku_nor_erase_count()).
 */
#define TIKU_NOR_SCRATCH_ADDR (TIKU_NOR_SIZE_BYTES - TIKU_NOR_SECTOR_SIZE)

/** @brief Clock rows; the serial phase always runs at 24 MHz. */
#define TIKU_NOR_CLK_24MHZ    0u   /**< 24 MHz */
#define TIKU_NOR_CLK_48MHZ    1u   /**< 48 MHz */
#define TIKU_NOR_CLK_96MHZ    2u   /**< 96 MHz */

/** @brief Result codes of the NOR driver. */
typedef enum {
    TIKU_NOR_OK = 0,
    TIKU_NOR_ERR_POWER,     /**< domain did not come up, or driver is down   */
    TIKU_NOR_ERR_CLOCK,     /**< IO clock select/enable did not stick        */
    TIKU_NOR_ERR_TIMEOUT,   /**< transfer or WIP wait ran out, or DMA error  */
    TIKU_NOR_ERR_ID,        /**< device answered with the wrong identity     */
    TIKU_NOR_ERR_ARG,       /**< bad argument (incl. PIO-while-XIP)          */
    TIKU_NOR_ERR_STATE,     /**< device config not what this driver requires */
    TIKU_NOR_ERR_PROGRAM,   /**< program/erase reported failure in FLAGSTAT  */
} tiku_nor_err_t;

/** @brief Identity, as read from the device. */
typedef struct {
    uint8_t mfr;        /**< 0x9D expected (ISSI)                           */
    uint8_t type;       /**< memory type                                    */
    uint8_t capacity;   /**< capacity code                                  */
    uint8_t ncr6;       /**< non-volatile CR[6]: 0xFF = XIP disabled        */
    uint8_t status;     /**< status register at read time                   */
    uint8_t octal;      /**< 1 if the identity was read in octal DDR mode   */
} tiku_nor_id_t;

/*---------------------------------------------------------------------------*/
/* API                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Serial bring-up: power the controller, set serial mode, reset.
 *
 * Table 1 steps 1-3; the identity is read by tiku_nor_read_id().
 *
 * @param clk  TIKU_NOR_CLK_*, range-checked only: the serial phase runs at
 *             24 MHz
 */
tiku_nor_err_t tiku_nor_init_serial(unsigned clk);

/**
 * @brief Switch the device and controller from serial to octal DDR.
 *
 * Returns TIKU_NOR_ERR_STATE when non-volatile CR[6] is not 0xFF; the driver
 * never writes non-volatile configuration.  The octal identity read is traced
 * and does not gate entry.  Returns TIKU_NOR_OK at once when already octal.
 */
tiku_nor_err_t tiku_nor_enter_octal(unsigned clk);

/**
 * @brief Enter octal and stay there even if an identity read fails.
 *
 * For diagnostics that examine the octal configuration.  On
 * TIKU_NOR_ERR_ID from tiku_nor_enter_octal() it configures the controller
 * for octal anyway and returns TIKU_NOR_OK.
 */
tiku_nor_err_t tiku_nor_enter_octal_raw(unsigned clk);

/**
 * @brief Configure the controller for octal DDR without the device handshake.
 *
 * Diagnostic: for a part that is already in octal (non-volatile IO-mode
 * default), every serial command is noise and the device looks dead.
 */
tiku_nor_err_t tiku_nor_force_octal(unsigned clk);

/** @brief Stop the IO clock and power the MSPI1 domain down. */
void tiku_nor_deinit(void);

/**
 * @brief Drive the load-switch pad high (@p on) or low.
 *
 * @p on 0 also marks the driver down.  The NOR keeps its contents through a
 * supply cut and comes back in serial mode: call tiku_nor_init_serial().
 *
 * @warning The pad's polarity is unconfirmed, and driving it can leave the
 *          board unreachable over SWD until a power cycle.
 */
void tiku_nor_power(int on);

/** @brief 1 if the MSPI1 controller domain is powered. */
int tiku_nor_powered(void);

/**
 * @brief Read identity, status and (serial mode) non-volatile CR[6].
 *
 * Fills @p out (may be NULL) in either mode and caches an ISSI identity.
 *
 * @return TIKU_NOR_OK for manufacturer 0x9D, TIKU_NOR_ERR_ID for any other,
 *         or the transfer's error
 */
tiku_nor_err_t tiku_nor_read_id(tiku_nor_id_t *out);

/**
 * @brief The last identity that validated, without touching the bus.
 *
 * For status readers such as /sys/flash/id: a `cat` should not be a bus
 * transaction, and must not wake a part whose load switch is off.
 *
 * @return 0 when an identity has been read since boot, -1 otherwise.
 */
int tiku_nor_id_cached(tiku_nor_id_t *out);

/**
 * @brief Sweep RXDQSDELAY 0..31 and report which settings read a valid ID.
 *
 * The DQS capture point depends on board and speed.  Octal only; the
 * previous delay and DQS enable are restored afterwards.
 *
 * @param with_dqs  non-zero to sample with DQS enabled during the sweep
 * @return Bit d set when delay d produced a valid identity; 0 when not octal
 */
uint32_t tiku_nor_scan_rxdqs(int with_dqs);

/**
 * @brief Test whether the device parses octal commands at all.
 *
 * Sends the octal software reset and checks the part answers in serial.
 * Uses no read capture, so it isolates the command path from the data path.
 * Leaves the controller in serial mode at 24 MHz.
 *
 * @return 1 heard, 0 did not, -1 when not in octal.
 */
int tiku_nor_octal_hears(void);

/**
 * @brief Sweep the array-read turnaround (0..31) against known-good bytes.
 *
 * Works in either mode for @p n up to 32; the setting is restored afterwards.
 * In octal the count must match VCR 0x01; a mismatch shifts the data.
 *
 * @return Bit t set when turnaround t reproduced @p want exactly.
 */
uint32_t tiku_nor_scan_turnaround(uint32_t addr, const uint8_t *want,
                                  uint32_t n);

/** @brief Read @p n bytes from @p addr (PIO; XIP must be off). */
tiku_nor_err_t tiku_nor_read(uint32_t addr, void *buf, uint32_t n);

/**
 * @brief Program @p n bytes at @p addr.  Caller must have erased first.
 *
 * Split at page boundaries; each page is WREN -> program -> WIP poll.  NOR can
 * only clear bits, so programming over unerased data ANDs it into the old
 * contents and no error is reported.
 */
tiku_nor_err_t tiku_nor_program(uint32_t addr, const void *buf, uint32_t n);

/**
 * @brief Erase one 128 KB sector (or 4 KB subsector with @p small).
 *
 * Counted in tiku_nor_erase_count().  Returns TIKU_NOR_ERR_ARG for an
 * address below the scratch sector unless @p force is non-zero.
 */
tiku_nor_err_t tiku_nor_erase(uint32_t addr, int small, int force);

/** @brief Total erases this driver has performed since boot. */
uint32_t tiku_nor_erase_count(void);

/**
 * @brief Run norbench: erase, program, PIO, random, DMA and XIP reads.
 *
 * Each leg is DWT-timed and checked against its pattern, and prints bit-exact
 * or FAIL.  Works in the scratch sector, spending one sector erase and one
 * subsector erase per run.
 *
 * @note Requires the NOR to be up; run `power nor` first.
 */
void tiku_nor_bench_run(void);

/** @brief Include the XIP leg in the next bench run (off by default). */
void tiku_nor_bench_set_xip(int on);

/**
 * @brief Include the 128 KB sector-erase leg; spends a second erase cycle.
 *
 * tiku_nor_bench_run() does not read this flag: every run erases the sector.
 */
void tiku_nor_bench_set_sector(int on);

/**
 * @brief Read @p n bytes device -> SRAM with the DMA engine.
 *
 * @p n must be a multiple of 4, @p sram 4-byte aligned and the range inside
 * the device, or the call returns TIKU_NOR_ERR_ARG.
 *
 * @note Cache coherency is the caller's job; the engine writes physical SRAM.
 *       Refused while the XIP aperture is live -- that combination wedges the
 *       APB, as on the PIO path.
 */
tiku_nor_err_t tiku_nor_dma_read(uint32_t addr, void *sram, uint32_t n);

/**
 * @brief Read one word of the scratch sector through the XIP aperture.
 *
 * @note A mis-decoding aperture stalls the bus with no software recovery; the
 *       board needs a reflash. One word keeps the exposure minimal.
 * @return 0 on success, -1 when the NOR is down or the aperture will not open.
 */
int tiku_nor_xip_probe(uint32_t *out);

/** @brief Map/unmap the 8 MB read aperture at TIKU_NOR_XIP_BASE. */
tiku_nor_err_t tiku_nor_xip_enable(int enable);
/** @brief 1 if the aperture is live. */
int tiku_nor_xip_enabled(void);

/** @brief Live IO clock in Hz, 0 if down. */
unsigned long tiku_nor_clock_hz(void);
/** @brief 1 once the device is in octal DDR. */
int tiku_nor_is_octal(void);

/** @brief Install @p fn to get each step's name as it starts; NULL for none. */
void tiku_nor_set_trace(void (*fn)(const char *step));

/**
 * @brief Controller-free identity read: bit-bang serial SPI on GPIO.
 *
 * Sends READ_ID by driving CE, CLK and D0 and samples @p n_bytes from D1, with
 * no dependence on controller framing, latency or lane assignment.  Leaves D0,
 * CLK and CE as GPIO inputs; the next init reclaims them.
 */
void tiku_nor_bitbang_id(uint8_t *out, uint32_t n_bytes);

/**
 * @brief Check the bit-bang read path before trusting its verdict.
 *
 * Drives and reads back D1, CE, CLK, RSTn and the load-switch pad.
 *
 * @warning Drives the load-switch pad high (see tiku_nor_power()).
 * @return b0/b1 D1 read while driven low/high, b2 D1 released, b3 D0
 *         released, b4/b5 CE low/high, b6/b7 CLK low/high, b8 RSTn high,
 *         b9 load switch high.  Bits 0-3 of 0x2 or 0x6: the read path works.
 */
uint32_t tiku_nor_bitbang_selftest(void);

/**
 * @brief Copy DEVPWRSTATUS, MSPIIOCLKCTRL and 10 MSPI1 registers into @p out.
 *
 * Entries past those, and the MSPI1 ones while its domain is unpowered, read
 * 0xDEADDEAD; no MSPI1 register is read then.
 */
void tiku_nor_regs(uint32_t *out, unsigned n);

/**
 * @brief Set the load-switch pad: 0 low, 1 high, -1 high-Z input; waits 2 ms.
 *
 * @warning As tiku_nor_power(): the polarity is unconfirmed.
 */
void tiku_nor_ls_set(int level);

/** @brief Take D0 off MSPI1 (@p enable 1) to exercise the failure paths. */
void tiku_nor_fault_inject(int enable);

#endif /* TIKU_NOR_ARCH_H_ */
