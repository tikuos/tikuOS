/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nor_arch.h - Apollo510 MSPI1 and external octal NOR flash (8 MB).
 *
 * Reads are cheap and XIP-able; writes are expensive and stateful -- a page
 * program touches at most 256 B and a bit can only go 1 to 0, so clearing needs a
 * sector erase costing one of ~100,000 cycles.  Erases are a counted budget.
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
 * FOR THE APOLLO510B (BLUE) EVB, taken from the BSP for THAT board,
 * `boards/apollo510b_evb/bsp/am_bsp_pins.h`:
 *
 *   signal      pad          note
 *   ----------  -----------  ----------------------------------------------
 *   D0..D7      GP95..GP102  MSPI1 data, FNCSEL 0
 *   SCK         GP103        FNCSEL 0
 *   DQS/DM      GP104        FNCSEL 0
 *   RSTn        GP17         PLAIN GPIO, per the 510B BSP
 *   CE0         *** NOT DEFINED ON THIS BOARD ***
 *
 * READ THE LAST LINE AGAIN.  The 510B BSP does not define an MSPI1 chip
 * select at all, and in am_bsp_mspi_pins_enable() Ambiq COMMENTED OUT the
 * CE pinconfig for module 1:
 *
 *     // am_hal_gpio_pinconfig(AM_BSP_GPIO_MSPI1_CE0, g_AM_BSP_GPIO_MSPI1_CE0);
 *
 * A vendor does that when the device is not on the variant.  Combined with
 * the owner finding no U12 on the board, the conclusion is that THE BLUE EVB
 * DOES NOT CARRY THE NOR, and this driver has never had a device to talk to.
 *
 * BUT IT DOES HAVE A TARGET: the lab also has the **Apollo510 EVB (green)**,
 * which carries U12 per its own schematic.  On THAT board the pins are the
 * ones the (correct, for it) AP510EVB_Rev2.2 schematic gives:
 *
 *     RSTn  GP54    CE0  GP53    load switch  GP208
 *
 * so this driver is board-conditional, not dead.  See the pin defines in the
 * .c: they select by TIKU_BOARD_APOLLO510_EVB.  Finish N1-N5 on the green
 * board; the Blue board simply has nothing to talk to.
 *
 * THE MISTAKE THIS COMMENT EXISTS TO RECORD:
 *
 * An earlier version of this table asserted RSTn = GP54 and a load switch on
 * GP208, and called the BSP "stale for this board revision".  Both values came
 * from `AP510EVB_Rev2.2_Schematic.pdf` (now filed under
 * hardware/ambiq/boards/ as the labelled counter-example) -- which is the
 * schematic for the
 * **Apollo510 EVB, not the Apollo510B (Blue) EVB in the lab**.  Its
 * title block says "Apollo510 EVB" and its SoC is AP510NFA-CBR; the file name
 * says AP510EVB with no B.  The proof is exact: the NON-B board's BSP says
 * MSPI1_RST = 54 and mentions GP208, while ours says 17 and never mentions
 * GP208.  The schematic matches the other board perfectly.
 *
 * So the BSP was right, the schematic was for a different board, and I
 * overrode the correct source with the wrong one -- confidently, in a
 * comment, in the tree.  The rule that follows: BEFORE a schematic is
 * allowed to overrule a BSP, confirm the schematic is for THIS BOARD --
 * title block and part number, not the file name.
 *
 * (The MSPI0/PSRAM work is unaffected: both boards share the MSPI0 pinout
 * pad-for-pad, including the x16 group and CE0 = GP199, so every PSRAM
 * result stands.)
 */

/*---------------------------------------------------------------------------*/
/* TABLE 1 -- BRING-UP SEQUENCE                                             */
/*---------------------------------------------------------------------------*/
/*
 * The PSRAM's table 1 applies verbatim for the controller (power the MSPI1
 * domain, force the oscillator, select+enable the IO clock, program DEV0CFG,
 * pads AFTER the controller, and so on) -- with MSPI1's base and pads.  What
 * differs is that this device WAKES UP IN PLAIN 1-LINE SPI and must be
 * TALKED into octal DDR.  So bring-up has two phases:
 *
 *  PHASE A -- SERIAL (easy, and where identity is established)
 *   1  load switch on (GP208 high), reset pulse (GP54 low ~10 us, high)
 *   2  controller at 24 MHz, DEVCFG0 = SERIAL0, 1-byte instruction,
 *      4-byte address, TURNAROUND 8, no DDR, PADOUTEN = SERIAL0 (259)
 *   3  RESET_ENABLE (0x66) then RESET_MEMORY (0x99) -- software reset
 *   4  READ_ID (0x9F): expect manufacturer 0x9D (ISSI)  <-- THE N1 GATE
 *   5  WRITE_ENABLE (0x06), ENTER_4BYTE_ADDRESS_MODE (0xB7)
 *
 *  PHASE B -- OCTAL DDR (only after identity is proven)
 *   6  READ non-volatile CR[6]; it must read 0xFF (XIP disabled).  IF IT
 *      DOES NOT, THIS DRIVER REFUSES AND REPORTS -- the vendor writes the
 *      NON-VOLATILE register here, and non-volatile config writes are the
 *      permanent kind.  A one-line fix is not worth an irreversible write
 *      performed by a robot at 3 a.m.
 *   7  WRITE volatile CR[0x00] = 0xE7 (octal DDR)   <-- volatile, reversible
 *   8  reconfigure the controller: 2-byte instructions, TURNAROUND 31,
 *      EMULATEDDR, DQS enabled, read op 0xFDFD, write op 0x1212
 *   9  octal WRITE_DISABLE, then re-read identity IN OCTAL to prove the
 *      mode change took (identity twice, in two modes, is this part's
 *      equivalent of the PSRAM's bit-bang arbiter)
 *
 * DMA BOUNDARY IS *NONE* FOR THIS PART (vendor).  NOR has no DRAM rows and
 * no refresh, so there is nothing to break bursts for -- which is why this
 * driver's bandwidth number doubles as an independent test of whether the
 * PSRAM's ~14 us/KB plateau was really row economics.
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
 *   0xB5/0xB1 READ/WRITE NON-VOLATILE CR  ** PERMANENT -- READ ONLY HERE **
 *   0x0C FAST_READ_4B       0x12 PAGE_PROGRAM_4B
 *   0x20 SUBSECTOR_ERASE (4 KB)   0xD8 SECTOR_ERASE (128 KB)
 *   0xC7 CHIP_ERASE         ** NEVER ISSUED BY THIS DRIVER **
 *
 * OCTAL DDR (2-byte duplicated opcodes, 8 data lines, like the PSRAM):
 *   0xFDFD read   0x1212 page program   0x2121 subsector erase
 *   0xD8D8 / 0xDCDC sector erase        0x6060 chip erase (never used)
 *
 * GEOMETRY: page 256 B (program unit), subsector 4 KB, sector 128 KB
 * (64 sectors of 128 KB = 8 MB), 4-byte addressing throughout.
 *
 * STATUS DISCIPLINE: every program/erase is WREN -> op -> poll WIP with the
 * BACKOFF cadence the PSRAM taught (a tight status spin is APB traffic into
 * the working controller; it cost 20 % there).  Programs poll at ~100 us,
 * erases at ~10 ms, both bounded, both hang-checkin'd.
 */

/*---------------------------------------------------------------------------*/
/* PUBLIC CONSTANTS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief MSPI1 XIP aperture base (module stride 0x04000000 from MSPI0). */
/* MSPI1's aperture, 0x80000000..0x84000000 -- NOT 0x64000000, which lies
 * inside MSPI0's window (0x60000000..0x70000000) where the PSRAM lives. A read
 * there goes to the wrong controller and stalls the bus with no software
 * recovery. Vendor: MSPI1_APERTURE_START_ADDR in am_reg_base_addresses.h. */
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
 * @brief The ONE sector this driver's own gates are allowed to erase.
 *
 * The last 128 KB of the die.  Erase endurance is finite (~100 k cycles per
 * sector), so every self-test writes here and the driver counts every erase it
 * performs (tiku_nor_erase_count) for the record.
 */
#define TIKU_NOR_SCRATCH_ADDR (TIKU_NOR_SIZE_BYTES - TIKU_NOR_SECTOR_SIZE)

/** @brief Bring-up clock rows (serial first, then octal DDR). */
#define TIKU_NOR_CLK_24MHZ    0u
#define TIKU_NOR_CLK_48MHZ    1u
#define TIKU_NOR_CLK_96MHZ    2u

/** @brief Result codes -- distinct causes stay distinct. */
typedef enum {
    TIKU_NOR_OK = 0,
    TIKU_NOR_ERR_POWER,     /**< controller domain never came up            */
    TIKU_NOR_ERR_CLOCK,     /**< IO clock select/enable did not stick        */
    TIKU_NOR_ERR_TIMEOUT,   /**< a transfer or a WIP wait never finished     */
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
 * @brief Phase A: power, reset, serial mode, and read identity.
 * @param clk  TIKU_NOR_CLK_* (serial phase runs at 24 MHz regardless)
 */
tiku_nor_err_t tiku_nor_init_serial(unsigned clk);

/**
 * @brief Phase B: switch the device and controller to octal DDR.
 *
 * Refuses (ERR_STATE) if the non-volatile XIP-disable bit is not already in
 * the state octal mode needs -- this driver never writes non-volatile
 * configuration.  Re-reads identity in octal to prove the switch.
 */
tiku_nor_err_t tiku_nor_enter_octal(unsigned clk);

/**
 * @brief Enter octal and STAY there even if the identity read comes back empty.
 *
 * For diagnostics that need to examine the octal configuration. The normal
 * entry rolls back to serial on that failure, which leaves a probe reading a
 * serial controller against an octal device.
 */
tiku_nor_err_t tiku_nor_enter_octal_raw(unsigned clk);

/**
 * @brief Configure the CONTROLLER for octal DDR without the device handshake.
 *
 * Diagnostic: for a part that is already in octal (non-volatile IO-mode
 * default), every serial command is noise and the device looks dead.
 */
tiku_nor_err_t tiku_nor_force_octal(unsigned clk);

/** @brief Release the controller domain (device keeps its contents). */
void tiku_nor_deinit(void);

/** @brief Cut the load switch: VDD_FLASH off, TRUE zero (contents kept). */
void tiku_nor_power(int on);

/** @brief 1 if the MSPI1 controller domain is powered. */
int tiku_nor_powered(void);

/** @brief Identity + config snapshot (works in either mode). */
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
 * The DQS capture point is board and speed dependent, which is why the vendor
 * scans for it rather than assuming one. Octal only; 0 when nothing captures.
 *
 * @return Bit d set when delay d produced a valid identity.
 */
uint32_t tiku_nor_scan_rxdqs(int with_dqs);

/**
 * @brief Test whether the device parses OCTAL commands at all.
 *
 * Sends the octal software reset and checks the part comes back on serial.
 * Uses no read capture, so it isolates the command path from the data path.
 *
 * @return 1 heard, 0 did not, -1 when not in octal.
 */
int tiku_nor_octal_hears(void);

/**
 * @brief Sweep the octal array-read turnaround against known-good bytes.
 *
 * The device's array dummy count lives in VCR 0x01 and the controller must
 * match it; a mismatch shifts the data rather than losing it.
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
 * Split across page boundaries internally; each page is WREN -> program ->
 * WIP-poll.  NOR can only clear bits, so programming over non-erased data
 * silently ANDs -- the verify gates catch that, and the API does not pretend.
 */
tiku_nor_err_t tiku_nor_program(uint32_t addr, const void *buf, uint32_t n);

/**
 * @brief Erase one 128 KB sector (or 4 KB subsector with @p small).
 *
 * Counted in tiku_nor_erase_count().  Refuses any address outside the
 * scratch sector unless @p force -- the driver protects the rest of the die
 * from its own test machinery by default.
 */
tiku_nor_err_t tiku_nor_erase(uint32_t addr, int small, int force);

/** @brief Total erases this driver has performed since boot. */
uint32_t tiku_nor_erase_count(void);

/**
 * @brief Run norbench: erase, program, sequential read and random reads.
 *
 * DWT-timed and checksum-gated; a leg that cannot prove its bytes reports FAIL
 * rather than a bandwidth. Spends one subsector erase per run.
 *
 * @note Requires the NOR to be up; run `power nor` first.
 */
void tiku_nor_bench_run(void);

/** @brief Include the XIP leg in the next bench run (off by default). */
void tiku_nor_bench_set_xip(int on);

/** @brief Include the 128 KB sector-erase leg; spends a second erase cycle. */
void tiku_nor_bench_set_sector(int on);

/**
 * @brief Read @p n bytes device -> SRAM with the DMA engine.
 *
 * @note Cache coherency is the caller's job; the engine writes physical SRAM.
 *       Refused while the XIP aperture is live -- that combination wedges the
 *       APB, as on the PIO path.
 */
tiku_nor_err_t tiku_nor_dma_read(uint32_t addr, void *sram, uint32_t n);

/**
 * @brief Read ONE word through the XIP aperture.
 *
 * @note A mis-decoding aperture stalls the bus with no software recovery; the
 *       board needs a reflash. One word keeps the exposure minimal.
 * @return 0 on success, -1 when the NOR is down or the aperture will not open.
 */
int tiku_nor_xip_probe(uint32_t *out);

/** @brief Map/unmap the 8 MB read aperture at 0x64000000. */
tiku_nor_err_t tiku_nor_xip_enable(int enable);
/** @brief 1 if the aperture is live. */
int tiku_nor_xip_enabled(void);

/** @brief Live IO clock in Hz, 0 if down. */
unsigned long tiku_nor_clock_hz(void);
/** @brief 1 once the device is in octal DDR. */
int tiku_nor_is_octal(void);

/** @brief Install a step tracer (a wedged bring-up must name its step). */
void tiku_nor_set_trace(void (*fn)(const char *step));

/**
 * @brief Controller-free identity read: bit-bang serial SPI on GPIO.
 *
 * The PSRAM's ground-truth instrument, ported to MSPI1's pads.  Answers
 * "is the device alive and does it speak SPI at all" with no dependence on
 * controller framing, latency or lane assignment.
 */
void tiku_nor_bitbang_id(uint8_t *out, uint32_t n_bytes);

/**
 * @brief Prove the bit-bang read path before trusting its verdict.
 * @return b0 read-while-driving-low, b1 read-while-driving-high,
 *         b2 D1 released, b3 D0 released.  0x02/0x06 = instrument OK.
 */
uint32_t tiku_nor_bitbang_selftest(void);

/** @brief Snapshot controller registers (read back, never assumed). */
void tiku_nor_regs(uint32_t *out, unsigned n);

/** @brief Drive the load-switch pad: 0 low, 1 high, -1 high-Z (polarity TBD). */
void tiku_nor_ls_set(int level);

/** @brief Deliberately break the bus (steal D0) so guards can be seen firing. */
void tiku_nor_fault_inject(int enable);

#endif /* TIKU_NOR_ARCH_H_ */
