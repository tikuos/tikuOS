/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_emmc_arch.h - Apollo510 SDIO0 host and on-board 8 GB eMMC.
 *
 * SDIO0 is an SD Host Controller, driven here in MMC mode.  The API covers
 * bring-up, 8-bit high-speed and HS200 timing, PIO and SDMA block transfers
 * by LBA, CMD5 sleep and staging into PSRAM.  There is no filesystem layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_EMMC_ARCH_H_
#define TIKU_EMMC_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TABLE 0 -- PINS                                                           */
/*---------------------------------------------------------------------------*/
/*
 *   signal        pad(s)              board header macro
 *   ------------  ------------------  ------------------------------------
 *   DAT0..DAT3    GP84..GP87          TIKU_BOARD_EMMC_PAD_D0, _D3 (ends)
 *   DAT4..DAT7    GP156..GP159        TIKU_BOARD_EMMC_PAD_D4, _D7 (ends)
 *   CLK           GP88                TIKU_BOARD_EMMC_PAD_CLK
 *   CMD           GP160               TIKU_BOARD_EMMC_PAD_CMD
 *   RSTn          GP13 (Apollo510B)   TIKU_BOARD_EMMC_PAD_RST
 *                 GP12 (Apollo510)
 *
 * The board headers define the pads, and FNCSEL per run: 2 for GP84..GP88,
 * 0 for GP156..GP160.  The Apollo510B EVB schematic names the reset net
 * SDIO0_RSTn_GP13.  The Apollo510B BSP gives RSTn as GP12, which is
 * the Apollo510 EVB's pad, and also assigns GP12 to COM_UART_TX
 * (am_bsp_pins.h:69 and :919); its RSTn value is wrong for the Apollo510B.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 1 -- HOST BRING-UP SEQUENCE                                         */
/*---------------------------------------------------------------------------*/
/*
 *  #  what                          register(s)
 * --  ----------------------------  ------------------------------------------
 *  1  power the SDIO0 domain        PWRCTRL.DEVPWREN, wait DEVPWRSTATUS
 *  2  clock the host                CLKGEN.MISC.FRCHFRC; MCUCTRL.SDIO0CTRL
 *                                   SDIO0SYSCLKEN and SDIO0XINCLKEN
 *  3  reset the host                CLOCKCTRL.SWRSTALL, wait for it to clear
 *  4  read the base clock           CAPABILITIES0.SDCLKFREQ (MHz)
 *  5  status and data timeout       INTENABLE all, INTSIG 0 (polled);
 *                                   CLOCKCTRL.TIMEOUTCNT 0xE (2^27 TMCLK)
 *  6  pads to the SDIO function     pad config, FNCSEL per run (table 0)
 *  7  bus voltage, then bus power   HOSTCTRL1.VOLTSELECT 1.8 V, SDBUSPOWER
 *  8  400 kHz identification clock  CLOCKCTRL.FREQSEL, CLKEN, wait CLKSTABLE,
 *                                   then SDCLKEN
 *  9  1-bit bus                     HOSTCTRL1.XFERWIDTH 0, DATATRANSFERWIDTH 0
 * 10  pulse the card reset          RSTn pad as GPIO: high 200 us, low
 *                                   200 us, high, then 2 ms for card boot
 * 11  card identification           table 2
 * 12  raise width and clock         table 4
 *
 * The clock divider is the smallest power of two from 1 to 256 that brings
 * base / divider to or below the target; FREQSEL takes divider >> 1.  The
 * clock starts in three steps: CLKEN, a bounded poll of CLKSTABLE, SDCLKEN.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 2 -- CARD IDENTIFICATION (MMC commands, in order)                   */
/*---------------------------------------------------------------------------*/
/*
 *  cmd  name              arg                  response  what it achieves
 *  ---  ----------------  -------------------  --------  -------------------
 *   0   GO_IDLE_STATE     0                    none      card -> idle
 *   1   SEND_OP_COND      OCR w/ sector bit    R3        wait !busy; >2 GB
 *                         (0x40FF8080)                   cards report sector
 *                                                        addressing here
 *   2   ALL_SEND_CID      0                    R2 (136)  CID: maker, product
 *                                                        name, serial, date
 *   3   SET_RELATIVE_ADDR RCA<<16              R1        assign an address
 *   9   SEND_CSD          RCA<<16              R2 (136)  card-specific data
 *   7   SELECT_CARD       RCA<<16              R1b       standby -> transfer
 *  16   SET_BLOCKLEN      512                  R1
 *   8   SEND_EXT_CSD      0                    R1 + 512B EXT_CSD; SEC_COUNT
 *                                              data      at bytes 212..215
 *                                                        = capacity
 *   6   SWITCH            see below            R1b       bus width / speed
 *
 * After identification:
 *
 *  17   READ_SINGLE_BLOCK LBA                  R1 + data
 *  18   READ_MULTIPLE     LBA                  R1 + data
 *  24   WRITE_BLOCK       LBA                  R1 + data
 *  25   WRITE_MULTIPLE    LBA                  R1 + data
 *  12   STOP_TRANSMISSION 0                    R1b       ends 18/25; the host
 *                                                        sends it (Auto CMD12)
 *  13   SEND_STATUS       RCA<<16              R1        poll ready
 *   7   SELECT_CARD       0                    none      deselect before sleep
 *   5   SLEEP_AWAKE       RCA<<16, bit 15      R1b       bit 15 set: sleep;
 *                                                        clear: wake
 *
 * The driver's CMD6 writes only these EXT_CSD indexes:
 *
 *      183  BUS_WIDTH     (0=1bit, 1=4bit, 2=8bit)
 *      185  HS_TIMING     (0=legacy, 1=high speed, 2=HS200)
 *
 * Part of EXT_CSD is one-time programmable: writing another index can
 * permanently disable a feature or repartition the device.  emmc_switch()
 * returns TIKU_EMMC_ERR_ARG for any other index without issuing a command.
 * Boot and RPMB partitions are never addressed; all traffic is to the user
 * area.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 3 -- RESPONSE TYPES, AND HOW THE HOST ENCODES THEM                  */
/*---------------------------------------------------------------------------*/
/*
 *  type   bits  meaning                    TRANSFER.RESPTYPESEL
 *  -----  ----  -------------------------  ----------------------------
 *  none      0  no response                NORESPONSE (0)
 *  R1       48  status                     LEN48 (2)
 *  R1b      48  status + busy on DAT0      LEN48CHKBUSY (3)
 *  R2      136  CID / CSD                  LEN136 (1)
 *  R3       48  OCR, no CRC, no index      LEN48 (2), CRC and index checks off
 *
 * Also in TRANSFER: CMDCRCCHKEN and CMDIDXCHKEN (both off for R3, whose OCR
 * response carries neither), DATAPRSNTSEL when a data phase follows, and
 * CMDIDX.  A 136-bit response arrives in RESPONSE0..3 with the CRC byte
 * shifted out, so every field sits 8 bits below its position in the CID or
 * CSD layout.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 4 -- BUS UPGRADE: 1 BIT AT 400 kHz TO 8 BITS AT HIGH SPEED          */
/*---------------------------------------------------------------------------*/
/*
 * The MMC spec requires identification on a 1-bit bus at 400 kHz, about
 * 50 KB/s.  After identification the driver changes:
 *
 *   what              from            to              factor
 *   ----------------  --------------  --------------  ---------------------
 *   bus width         1 bit           8 bits          8x the wire
 *   clock             400 kHz         base/2          ~120x
 *   blocks/command    1               up to 65535     amortises the command
 *   byte path         CPU (PIO)       SDMA            frees the CPU; an
 *                                                     unaligned buffer
 *                                                     stays on PIO
 *
 * The steps run in this order:
 *
 *   1. CMD6 BUS_WIDTH: the card switches first.  CMD6 travels on the 1-bit
 *      CMD line, so the width change does not affect the command itself.
 *   2. HOSTCTRL1.XFERWIDTH and DATATRANSFERWIDTH: the host follows.  Between
 *      steps 1 and 2 the two ends disagree on the width, and no data command
 *      may be issued.
 *   3. CMD6 HS_TIMING, then HOSTCTRL1.HISPEEDEN, then the clock divider.  A
 *      clock raised before the card has switched to high-speed timing
 *      corrupts data intermittently.
 *
 * After the switches the driver reads EXT_CSD again, a data transfer at the
 * new width and clock, and checks bytes 183 and 185 against the values it
 * wrote.  A bus that cannot carry data at the new setting fails that read; a
 * card that did not adopt a setting fails the comparison.  On either failure
 * both ends return to 1 bit at 400 kHz.
 *
 * EXT_CSD[196] DEVICE_TYPE lists the speeds the card supports:
 *   bit 0  26 MHz   bit 1  52 MHz   bits 2-7  HS200 / HS400 / DDR variants
 * The requested clock is clamped to the highest of bits 0 and 1.  A card
 * that sets neither keeps legacy timing and gets the requested clock as is.
 *
 * tiku_emmc_hs200() takes a card already at high speed to HS200 at 96 MHz.
 * HS400 and the DDR modes are not supported.
 */

/*---------------------------------------------------------------------------*/
/* PUBLIC CONSTANTS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Block size in bytes; a card above 2 GB is addressed by sector. */
#define TIKU_EMMC_BLOCK_SIZE   512u

/**
 * @brief Size of the scratch region, the top 1024 blocks of the user area.
 *
 * The bench, the diagnostic and HS200 tuning write only there, and
 * tiku_emmc_write_blocks() refuses an LBA below it unless forced.
 */
#define TIKU_EMMC_SCRATCH_BLOCKS  1024u

/** @brief Result codes of the eMMC driver. */
typedef enum {
    TIKU_EMMC_OK = 0,
    TIKU_EMMC_ERR_POWER,    /**< SDIO0 did not power up, or driver is down  */
    TIKU_EMMC_ERR_CLOCK,    /**< clock did not start, or HS200 fell back    */
    TIKU_EMMC_ERR_TIMEOUT,  /**< a command or data phase ran out of time    */
    TIKU_EMMC_ERR_CMD,      /**< host or card error, or a data mismatch     */
    TIKU_EMMC_ERR_ID,       /**< decoded capacity is 0 or above 32 GB       */
    TIKU_EMMC_ERR_ARG,      /**< bad argument, incl. a forbidden CMD6 index */
    TIKU_EMMC_ERR_STATE,    /**< asleep, busy, or bus width not adopted     */
    TIKU_EMMC_ERR_NOMEM,    /**< the SRAM tier could not lend the buffer    */
} tiku_emmc_err_t;

/** @brief Card identity and bus setting, filled by tiku_emmc_init_at(). */
typedef struct {
    uint8_t  mfr_id;         /**< CID[127:120] manufacturer                 */
    uint16_t oem_id;         /**< CID OEM/application                       */
    char     product[7];     /**< CID product name, NUL-terminated          */
    uint8_t  rev;            /**< product revision                          */
    uint32_t serial;         /**< product serial number                     */
    uint8_t  mfg_month;      /**< manufacture date                          */
    uint16_t mfg_year;       /**< year; epoch set by EXT_CSD revision       */
    uint32_t rca;            /**< relative card address assigned here       */
    uint32_t sec_count;      /**< EXT_CSD[215:212]: capacity in 512 B blocks */
    uint8_t  ext_csd_rev;    /**< EXT_CSD revision                          */
    uint8_t  spec_vers;      /**< CSD spec version                          */
    uint8_t  bus_width;      /**< bus width in force (1/4/8)                */
    uint32_t clock_hz;       /**< bus clock in force                        */
    /* EXT_CSD as read back after the bus upgrade (table 4) */
    uint8_t  device_type;    /**< EXT_CSD[196]: speeds the card supports    */
    uint8_t  ext_bus_width;  /**< EXT_CSD[183] read back after the switch   */
    uint8_t  ext_hs_timing;  /**< EXT_CSD[185] read back after the switch   */
} tiku_emmc_id_t;

/*---------------------------------------------------------------------------*/
/* API                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bring up the host and card, then raise the bus to 8 bits, 48 MHz.
 *
 * Runs table 1 and the table 2 identification at 400 kHz on one bit, then the
 * table 4 upgrade.  Every wait is bounded; the first failing step's code is
 * returned.
 *
 * @note When the upgrade fails, both ends return to 1 bit at 400 kHz and the
 *       driver stays up, but the upgrade's error code is returned.
 */
tiku_emmc_err_t tiku_emmc_init(void);

/**
 * @brief Bring the card up as tiku_emmc_init() does, at a chosen setting.
 *
 * @p width 1, 4 or 8; @p hz is clamped to the card's DEVICE_TYPE and rounded
 * down to base / 2^n.  (1, 400000) skips the upgrade and leaves the card at
 * the identification setting.
 */
tiku_emmc_err_t tiku_emmc_init_at(unsigned width, uint32_t hz);

/**
 * @brief Report how long the last tiku_emmc_init_at() took, in microseconds.
 *
 * Either pointer may be NULL.  Both read 0 when that call failed before it
 * read EXT_CSD.
 *
 * @param ladder_us from the call to the end of the 400 kHz identification
 * @param total_us  including the table 4 upgrade
 */
void tiku_emmc_init_time(uint32_t *ladder_us, uint32_t *total_us);

/** @brief Stop the bus and power SDIO0 down; the card keeps its contents. */
void tiku_emmc_deinit(void);

/** @brief 1 if the SDIO0 power domain is on, else 0. */
int tiku_emmc_powered(void);

/**
 * @brief Copy the decoded identity to @p out (may be NULL).
 *
 * @return TIKU_EMMC_ERR_POWER when the driver is down, TIKU_EMMC_ERR_ID when
 *         the capacity is 0 or above 32 GB, else TIKU_EMMC_OK
 */
tiku_emmc_err_t tiku_emmc_read_id(tiku_emmc_id_t *out);

/**
 * @brief Read @p n_blk 512-byte blocks starting at @p lba.
 *
 * A 4-byte-aligned @p buf is filled by SDMA, any other by PIO.  Returns
 * TIKU_EMMC_ERR_STATE while the card sleeps, and TIKU_EMMC_ERR_ARG for zero
 * blocks or a range past the end of the card.
 */
tiku_emmc_err_t tiku_emmc_read_blocks(uint32_t lba, uint32_t n_blk, void *buf);

/**
 * @brief Start an SDMA read and return; collect it with tiku_emmc_read_wait().
 *
 * One read may be outstanding; a second start returns TIKU_EMMC_ERR_STATE.
 * @p buf must be 4-byte aligned and @p n_blk must fit one command at the
 * current bus setting, or the call returns TIKU_EMMC_ERR_ARG.
 */
tiku_emmc_err_t tiku_emmc_read_start(uint32_t lba, uint32_t n_blk, void *buf);

/**
 * @brief Raise a card at high speed to HS200 at 96 MHz.
 *
 * Writes a pattern block at the scratch region, reads it at each of 32 RX
 * taps and keeps the centre of the widest passing run.  A failure from the
 * HS200 switch on returns the card to high speed at 48 MHz.
 *
 * @return TIKU_EMMC_OK; TIKU_EMMC_ERR_CLOCK after that fallback; before the
 *         switch, TIKU_EMMC_ERR_POWER, TIKU_EMMC_ERR_STATE (asleep), or the
 *         pattern write's error (TIKU_EMMC_ERR_CMD if it reads back wrong)
 * @note Expects the 8-bit high-speed setting tiku_emmc_init() leaves.
 */
tiku_emmc_err_t tiku_emmc_hs200(void);

/**
 * @brief Wait for the read started by tiku_emmc_read_start().
 *
 * Returns TIKU_EMMC_OK at once when no read is outstanding.
 */
tiku_emmc_err_t tiku_emmc_read_wait(void);

/**
 * @brief Write @p n_blk 512-byte blocks starting at @p lba.
 *
 * Returns TIKU_EMMC_ERR_ARG for an LBA below the scratch region unless
 * @p force is non-zero.  Returns once the card reports it has finished
 * programming.
 */
tiku_emmc_err_t tiku_emmc_write_blocks(uint32_t lba, uint32_t n_blk,
                                       const void *buf, int force);

/** @brief First LBA of the scratch region; 0 before the capacity is known. */
uint32_t tiku_emmc_scratch_lba(void);

/** @brief Install @p fn to get each step's name as it starts; NULL for none. */
void tiku_emmc_set_trace(void (*fn)(const char *step));

/**
 * @brief INTSTAT at the last controller-reported error.
 *
 * A driver timeout does not change it.
 */
uint32_t tiku_emmc_last_error(void);

/**
 * @brief Copy up to 8 host registers into @p out[0..n-1].
 *
 * Order: DEVPWRSTATUS, PRESENT, CLOCKCTRL, HOSTCTRL1, INTSTAT, CAPABILITIES0,
 * RESPONSE0, TRANSFER.  Entries past the eighth, and every entry after the
 * first while SDIO0 is unpowered, read 0xDEADDEAD; no SDIO0 read is made then.
 */
void tiku_emmc_regs(uint32_t *out, unsigned n);

/**
 * @brief Benchmark sequential and random reads and writes and print a table.
 *
 * Timed with the DWT cycle counter.  Writes go only to the scratch region, and
 * a leg whose data fails its pattern check prints FAIL with no rate.  The
 * 512 KB buffer is taken from SRAM-tier span 0 for the run.
 */
void tiku_emmc_bench_run(void);

/*---------------------------------------------------------------------------*/
/* SLEEP, STATE AND STAGING                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Put the card into CMD5 sleep; it keeps its contents.
 *
 * Waits for programming to finish, deselects the card (CMD5 is accepted only
 * in standby) and sends CMD5.  Block I/O returns TIKU_EMMC_ERR_STATE while
 * the card sleeps.  Returns TIKU_EMMC_OK at once when already asleep.
 */
tiku_emmc_err_t tiku_emmc_sleep(void);

/**
 * @brief Wake the card from CMD5 sleep, reselect it and wait until ready.
 *
 * Returns TIKU_EMMC_OK at once when the card is not asleep.
 */
tiku_emmc_err_t tiku_emmc_wake(void);

/** @brief 1 while the card is asleep. */
int tiku_emmc_asleep(void);

/**
 * @brief Microseconds the last tiku_emmc_sleep() or tiku_emmc_wake() took.
 *
 * 0 when that call was refused, had nothing to do, or failed before the card
 * changed state.
 */
uint32_t tiku_emmc_last_op_us(void);

/** @brief Capacity in 512 B blocks (0 until identified). */
uint32_t tiku_emmc_capacity_blocks(void);

/** @brief Bus clock in force, 0 when down. */
uint32_t tiku_emmc_clock_hz(void);

/** @brief Bus width in force (1/4/8), 0 when down. */
unsigned tiku_emmc_bus_width(void);

/** @brief The driver's identity record, with no power or validity check. */
const tiku_emmc_id_t *tiku_emmc_id(void);

/**
 * @brief Copy @p mb MB from @p src_lba to PSRAM offset 0 and print the rates.
 *
 * Card -> SSRAM bounce buffer by SDMA -> PSRAM by the MSPI command queue; a
 * hash of the source is compared with a hash read back from PSRAM.  The card
 * is only read, and hashing time is reported apart from the transfer rates.
 *
 * @note Compiled only with TIKU_DRV_PSRAM_ENABLE; PSRAM must be up and awake.
 */
void tiku_emmc_stage_run(uint32_t mb, uint32_t src_lba);

/**
 * @brief Begin staging a file's extents into PSRAM, from offset 0.
 *
 * Takes the 512 KB SRAM-tier bounce buffer and turns PSRAM XIP off until
 * tiku_emmc_stage_close().  Returns TIKU_EMMC_ERR_NOMEM when the buffer cannot
 * be had, including while a staging run already holds it.
 *
 * @note Compiled only with TIKU_DRV_PSRAM_ENABLE.
 */
tiku_emmc_err_t tiku_emmc_stage_open(void);

/**
 * @brief Append @p nsec blocks from @p lba to the PSRAM image.
 *
 * One call per contiguous extent, in file order.  Returns TIKU_EMMC_ERR_NOMEM
 * outside an open staging run.
 */
tiku_emmc_err_t tiku_emmc_stage_chunk(uint32_t lba, uint32_t nsec);

/**
 * @brief Hash the image back out of PSRAM, restore XIP, release the buffer.
 *
 * The caller compares the two hashes.  Any pointer may be NULL, and a
 * @p total_bytes of 0 releases the buffer without reading PSRAM.
 *
 * @param total_bytes bytes to read back from PSRAM offset 0
 * @param src         out: hash of the bytes read from the card
 * @param dst         out: hash of the bytes read back from PSRAM
 * @param rd_us       out: time spent reading the card
 * @param wr_us       out: time spent writing PSRAM
 */
tiku_emmc_err_t tiku_emmc_stage_close(uint32_t total_bytes, uint32_t *src,
                                      uint32_t *dst, uint32_t *rd_us,
                                      uint32_t *wr_us);

/**
 * @brief Read the scratch region six ways and print each result.
 *
 * Writes four pattern blocks one at a time, then reads 1 and 4 blocks into
 * SSRAM, DTCM and an unaligned (PIO) buffer, printing the first bad byte.
 */
void tiku_emmc_diag_run(void);

#endif /* TIKU_EMMC_ARCH_H_ */
