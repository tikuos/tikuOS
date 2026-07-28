/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nor_arch.h - Apollo510 MSPI1 + external octal NOR flash (8 MB).
 *
 * The board's first EXTERNAL NON-VOLATILE tier.  Schematic U12 on the AP510
 * EVB is an IS25WX064-JHL (ISSI, 64 Mbit = 8 MB octal xSPI NOR) on MSPI1.
 * Unlike the PSRAM it keeps its contents with the power off -- and unlike the
 * SoC's internal MRAM it can be switched to LITERALLY ZERO by a board load
 * switch (see the pin table).  That combination is a durability grade TikuOS
 * has never had: bulk, read-mostly, byte-readable through an aperture, and
 * free to hold.
 *
 * ITS PERSONALITY, IN ONE PARAGRAPH: reads are cheap and fast (XIP-able,
 * no row-boundary tax -- the vendor sets DMA boundary NONE for this part,
 * which also makes it an independent cross-check on the PSRAM's per-KB
 * plateau).  Writes are expensive and stateful: a page program touches at
 * most 256 B and takes hundreds of microseconds, and a byte can only go
 * 1 -> 0 -- clearing bits back to 1 requires ERASING a whole 128 KB sector
 * (or a 4 KB subsector), which takes hundreds of milliseconds and consumes
 * one of roughly 100 000 lifetime cycles.  Erases are therefore a BUDGET
 * this driver counts, not an action it takes casually.
 *
 * BARE METAL, as always: the vendor sources are read and transcribed, never
 * linked.  The MSPI controller knowledge is inherited wholesale from the
 * PSRAM bring-up (arch/ambiq/tiku_psram_arch.c) -- same register file at a
 * different base -- which is why this driver starts with twelve bugs already
 * paid for.  Those lessons are listed at the top of the .c and are not
 * repeated here.
 *
 * SOURCES READ (2026-07-29, AmbiqSuite 5.1.0 + AP510EVB Rev 2.2 schematic):
 *   devices/am_devices_mspi_is25wx064.{c,h}   device protocol
 *   mcu/apollo510/hal/mcu/am_hal_mspi.c       controller (already transcribed)
 *   boards/apollo510b_evb/bsp/am_bsp_pins.h   pad assignments
 *   hardware/ambiq/boards/AP510BEVB_Rev2.0_*.pdf   OUR board's schematic
 *   (an earlier version of this file cited AP510EVB_Rev2.2, which is a
 *    DIFFERENT board -- see the correction in table 0)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NOR_ARCH_H_
#define TIKU_NOR_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TABLE 0 -- PINS, AND A CORRECTION I GOT BACKWARDS ONCE                    */
/*---------------------------------------------------------------------------*/
/*
 * FOR THE APOLLO510B (BLUE) EVB -- our board -- taken from the BSP for THAT
 * board, `boards/apollo510b_evb/bsp/am_bsp_pins.h`:
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
 * **Apollo510 EVB, not the Apollo510B (Blue) EVB we actually have**.  Its
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
#define TIKU_NOR_XIP_BASE     0x64000000UL
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
 * sector) and a test that loops erases is a test that consumes hardware, so
 * every self-test writes here and the driver counts every erase it performs
 * (tiku_nor_erase_count) for the record.
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

/** @brief Read @p n bytes from @p addr (PIO; XIP must be off). */
tiku_nor_err_t tiku_nor_read(uint32_t addr, void *buf, uint32_t n);

/**
 * @brief Program @p n bytes at @p addr.  Caller must have erased first.
 *
 * Split across page boundaries internally; each page is WREN -> program ->
 * WIP-poll.  NOR can only clear bits, so programming over non-erased data
 * silently ANDs -- the verify gates catch that, and the API does not
 * pretend otherwise.
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
