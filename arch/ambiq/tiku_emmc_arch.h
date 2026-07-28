/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_emmc_arch.h - Apollo510 SDIO0 + on-board eMMC (8 GB).
 *
 * The warehouse.  Schematic U11 on the Apollo510B EVB is an IS21EF08G-JCLI
 * (ISSI, 8 GB eMMC).  Eight gigabytes against the SoC's 4 MB of internal
 * NVM: a thousandfold, permanent, and the missing floor under the PSRAM
 * working tier.
 *
 * THIS IS NOT AN MSPI VARIANT, and the plan says so on purpose.  SDIO is an
 * SD-Host-Controller-class peripheral speaking the MMC command protocol to a
 * managed flash device that runs its own firmware.  The vendor stack is
 * ~8000 lines across am_hal_sdhc.c, am_hal_card.c and am_hal_card_host.c.
 * We transcribe a MINIMAL VERTICAL SLICE of it:
 *
 *   - card states idle -> identify -> standby -> transfer, and nothing else
 *   - PIO and simple DMA block transfers, bounded backoff polls, no interrupts
 *   - 8-bit bus at high speed (<= ~48 MHz); HS200 and its tuning procedure
 *     are explicitly OUT OF SCOPE for the first pass
 *   - raw block API (LBA read/write); NO filesystem
 *
 * SOURCES READ (2026-07-29):
 *   mcu/apollo510/hal/mcu/am_hal_sdhc.c   host controller
 *   mcu/apollo510/hal/mcu/am_hal_card.c   card protocol
 *   hardware/ambiq/boards/AP510BEVB_Rev2.0_Schematic_Prints.pdf  OUR board
 *   boards/apollo510b_evb/bsp/am_bsp_pins.h                      pads
 *   hardware/ambiq/soc/Apollo_SoC_Family_Technical_Reference_Manual_v1p1.pdf
 * And, available from day one, the arbiter that cracked the PSRAM: a
 * PREBUILT vendor example for this exact board at
 *   boards/apollo510b_evb/examples/bm_sdmmc_sdio/emmc_raw_block_read_write/
 *   gcc/bin/emmc_raw_block_read_write.bin
 * Breakpoint it at a known-good moment and diff its register file against
 * ours whenever transcription fights silicon.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_EMMC_ARCH_H_
#define TIKU_EMMC_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TABLE 0 -- PINS, AND A BSP THAT CONTRADICTS ITSELF                        */
/*---------------------------------------------------------------------------*/
/*
 *   signal        pad(s)              source
 *   ------------  ------------------  ------------------------------------
 *   DAT0..DAT3    GP84..GP87          schematic and BSP agree
 *   DAT4..DAT7    GP156..GP159        schematic and BSP agree
 *   CLK           GP88                schematic and BSP agree
 *   CMD           GP160               schematic and BSP agree
 *   RSTn          GP13                *** SCHEMATIC.  THE BSP SAYS 12. ***
 *
 * THE RESET PIN, RESOLVED THREE WAYS.  Yesterday a session was lost to
 * overriding a BSP with the wrong board's schematic, so this one is settled
 * carefully rather than confidently:
 *
 *   1. The Apollo510B EVB schematic -- the RIGHT board this time, verified
 *      by title block -- carries the net `SDIO0_RSTn_GP13`.
 *   2. The 510B BSP says 12, but it ALSO assigns GP12 to COM_UART_TX
 *      (am_bsp_pins.h:69 vs :919).  One pad cannot be both the console
 *      transmitter and the eMMC reset; the BSP contradicts ITSELF, which
 *      condemns it without reference to any schematic.
 *   3. The GREEN board's schematic says `SDIO0_RSTn_GP12` -- exactly the
 *      BSP's value, i.e. the BSP carries the other board's number.
 *
 * So: GP13 on the Blue board.  Note the difference between this and
 * yesterday's mistake -- there, the BSP disagreed with a schematic for a
 * DIFFERENT board and the BSP was right; here, the BSP disagrees with its
 * OWN board's schematic and disagrees with itself, and the schematic wins.
 * The lesson is not "trust schematics" or "trust BSPs"; it is *find the
 * disagreement's source before picking a side.*
 */

/*---------------------------------------------------------------------------*/
/* TABLE 1 -- HOST BRING-UP SEQUENCE                                         */
/*---------------------------------------------------------------------------*/
/*
 *  #  what                          register(s)                why
 * --  ----------------------------  -------------------------  --------------
 *  1  power the SDIO0 domain        PWRCTRL.DEVPWREN           domain is off
 *                                   wait DEVPWRSTATUS          at boot
 *  2  learn the host's own limits   CAPABILITIES0.SDCLKFREQ    base clock MHz
 *                                   SLOTSTAT.SPECVER           spec version
 *  3  pads to SDIO function         GPIO.PINCFG[84-88,156-160] after the host
 *  4  release the card reset        GP13 high (GPIO)           see table 0
 *  5  bus power + voltage           PWRCTRLREG                 3.3 V / 1.8 V
 *  6  IDENTIFICATION CLOCK, 400 kHz CLOCKCTRL.FREQSEL          the MMC spec's
 *                                   CLKEN, wait CLKSTABLE,     mandatory slow
 *                                   then SDCLKEN               start
 *  7  1-bit bus                     HOSTCTRL1.XFERWIDTH=0,     identification
 *                                   .DATATRANSFERWIDTH=0       is 1-bit only
 *  8  the card ladder               see table 2
 *  9  only THEN raise clock/width   CLOCKCTRL / HOSTCTRL1      table 3
 *
 * The divider is a power of two: FREQSEL = divider>>1 where the divider is
 * the smallest power of 2 making (base / divider) <= the target.  Enabling
 * is a three-step dance -- CLKEN, poll CLKSTABLE, then SDCLKEN -- and the
 * poll is bounded like every other wait in this port.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 2 -- THE CARD LADDER (MMC commands, in order)                       */
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
 *   8   SEND_EXT_CSD      0                    R1 + 512B EXT_CSD; SEC_COUNT
 *                                              data      at bytes 212..215
 *                                                        = capacity
 *   6   SWITCH            see below            R1b       bus width / speed
 *  16   SET_BLOCKLEN      512                  R1
 *  17   READ_SINGLE_BLOCK LBA                  R1 + data
 *  18   READ_MULTIPLE     LBA                  R1 + data (needs CMD12)
 *  24   WRITE_BLOCK       LBA                  R1 + data
 *  25   WRITE_MULTIPLE    LBA                  R1 + data (needs CMD12)
 *  12   STOP_TRANSMISSION 0                    R1b       ends 18/25
 *  13   SEND_STATUS       RCA<<16              R1        poll ready
 *
 * *** THE ONLY CMD6 SWITCH INDEXES THIS DRIVER MAY EVER WRITE ***
 *
 *      183  BUS_WIDTH     (0=1bit, 1=4bit, 2=8bit)
 *      185  HS_TIMING     (0=legacy, 1=high speed)
 *
 * EXT_CSD is partly ONE-TIME-PROGRAMMABLE.  Writing the wrong index can
 * permanently disable a feature or repartition the device -- irreversibly,
 * on an 8 GB part we do not own a spare of.  The allowed list is enforced in
 * the .c by a switch that REFUSES anything else, not by a comment.  Boot and
 * RPMB partitions are never addressed; all traffic is the user area.
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
 *  R3       48  OCR, no CRC, no index      LEN48 (2), CRC and index checks OFF
 *
 * Also in TRANSFER: CMDCRCCHKEN and CMDIDXCHKEN (both OFF for R3 -- the OCR
 * response carries neither), DATAPRSNTSEL when a data phase follows, and
 * CMDIDX.  A 136-bit response arrives in RESPONSE0..3 with the CRC byte
 * shifted out, so the fields land 8 bits low -- the classic place a CID
 * decode goes wrong and prints plausible nonsense.
 */

/*---------------------------------------------------------------------------*/
/* PUBLIC CONSTANTS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Block size.  eMMC is 512-byte-addressed above 2 GB, always. */
#define TIKU_EMMC_BLOCK_SIZE   512u

/**
 * @brief Scratch region: the top 1024 blocks of the user area.
 *
 * Every self-test writes HERE and nowhere else.  The card's existing
 * contents are treated as opaque and precious: we did not put them there and
 * cannot replace them.  Low LBAs are where a future filesystem would live,
 * so tests stay far away from them.
 */
#define TIKU_EMMC_SCRATCH_BLOCKS  1024u

/** @brief Result codes -- distinct causes stay distinct. */
typedef enum {
    TIKU_EMMC_OK = 0,
    TIKU_EMMC_ERR_POWER,    /**< SDIO domain never came up                  */
    TIKU_EMMC_ERR_CLOCK,    /**< internal clock never stabilised            */
    TIKU_EMMC_ERR_TIMEOUT,  /**< a command or data phase never completed    */
    TIKU_EMMC_ERR_CMD,      /**< card reported an error (CRC / index / etc) */
    TIKU_EMMC_ERR_ID,       /**< identity implausible                       */
    TIKU_EMMC_ERR_ARG,      /**< bad argument, incl. a forbidden CMD6 index */
    TIKU_EMMC_ERR_STATE,    /**< card not in the state the operation needs  */
} tiku_emmc_err_t;

/** @brief Decoded identity -- the day-one trophy. */
typedef struct {
    uint8_t  mfr_id;         /**< CID[127:120] manufacturer                 */
    uint16_t oem_id;         /**< CID OEM/application                       */
    char     product[7];     /**< CID product name, NUL-terminated          */
    uint8_t  rev;            /**< product revision                          */
    uint32_t serial;         /**< product serial number                     */
    uint8_t  mfg_month;      /**< manufacture date                          */
    uint16_t mfg_year;
    uint32_t rca;            /**< relative card address we assigned         */
    uint32_t sec_count;      /**< EXT_CSD[215:212]: capacity in 512 B blocks */
    uint8_t  ext_csd_rev;    /**< EXT_CSD revision                          */
    uint8_t  spec_vers;      /**< CSD spec version                          */
    uint8_t  bus_width;      /**< bus width in force (1/4/8)                */
    uint32_t clock_hz;       /**< bus clock in force                        */
} tiku_emmc_id_t;

/*---------------------------------------------------------------------------*/
/* API -- E1/E2 surface.  Speed, DMA and the tier land in E3/E4.             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Power the host, reset the card, run the identification ladder.
 *
 * Table 1 steps 1-8 at the mandatory 400 kHz / 1-bit identification setting.
 * Fails closed and bounded at every wait; the step tracer names the rung a
 * wedge happened on.
 */
tiku_emmc_err_t tiku_emmc_init(void);

/** @brief Release the SDIO0 domain (card keeps its contents, obviously). */
void tiku_emmc_deinit(void);

/** @brief 1 if the SDIO0 domain is powered. */
int tiku_emmc_powered(void);

/** @brief Decoded identity after init: CID, CSD, EXT_CSD capacity. */
tiku_emmc_err_t tiku_emmc_read_id(tiku_emmc_id_t *out);

/** @brief Read @p n_blk 512-byte blocks starting at @p lba. */
tiku_emmc_err_t tiku_emmc_read_blocks(uint32_t lba, uint32_t n_blk, void *buf);

/**
 * @brief Write @p n_blk 512-byte blocks starting at @p lba.
 *
 * Refuses any LBA below the scratch region unless @p force -- the card's
 * existing contents are opaque and this driver runs unattended.
 */
tiku_emmc_err_t tiku_emmc_write_blocks(uint32_t lba, uint32_t n_blk,
                                       const void *buf, int force);

/** @brief First LBA of the scratch region (derived from EXT_CSD capacity). */
uint32_t tiku_emmc_scratch_lba(void);

/** @brief Install a step tracer, so a wedged bring-up names its own rung. */
void tiku_emmc_set_trace(void (*fn)(const char *step));

/** @brief INTSTAT captured at the last command failure -- which error, not that. */
uint32_t tiku_emmc_last_error(void);

/** @brief Snapshot host registers (power-safe: returns 0xDEADDEAD when down). */
void tiku_emmc_regs(uint32_t *out, unsigned n);

#endif /* TIKU_EMMC_ARCH_H_ */
