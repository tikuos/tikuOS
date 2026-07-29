/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_emmc_arch.h - Apollo510 SDIO0 and on-board 8 GB eMMC.
 *
 * SDIO is an SD-Host-Controller-class peripheral speaking MMC to a managed flash
 * device, not an MSPI variant.  This is a minimal vertical slice: four card
 * states, PIO and simple DMA blocks, 8-bit high speed, raw LBA API, no filesystem.
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
/* TABLE 4 -- THE E3 UPGRADE: 1 BIT AT 400 kHz -> 8 BITS AT ~48 MHz          */
/*---------------------------------------------------------------------------*/
/*
 * Identification is deliberately crippled: the MMC spec mandates a 1-bit bus
 * at 400 kHz because the host does not yet know what it is talking to.  That
 * is 50 KB/s of wire, and E2 measured the card through it.  E3 is the rung
 * that turns a proven-correct block device into a useful one.  Two switches,
 * a wider bus, a faster clock, and then the transfer engine stops issuing one
 * command per 512 bytes:
 *
 *   what              from            to              factor
 *   ----------------  --------------  --------------  ---------------------
 *   bus width         1 bit           8 bits          8x the wire
 *   clock             400 kHz         base/2          ~120x
 *   blocks/command    1               up to 65535     amortises the command
 *   byte path         CPU (PIO)       SDMA            frees the CPU
 *
 * ORDER IS NOT NEGOTIABLE, and each step is separately reversible:
 *
 *   1. CMD6 BUS_WIDTH -- the CARD switches first.  CMD6 travels on the CMD
 *      line, which is 1 bit wide whatever the DAT lines are doing, so the
 *      command itself is unaffected by the change it requests.
 *   2. HOSTCTRL1.XFERWIDTH -- the HOST follows.  Between (1) and (2) the two
 *      ends disagree about the bus width and NO DATA COMMAND MAY BE ISSUED.
 *   3. CMD6 HS_TIMING, then HOSTCTRL1.HISPEEDEN, then the clock divider --
 *      same shape, same reason.  Raising the clock before the card has been
 *      told to expect high-speed timing is how a bus starts corrupting data
 *      intermittently rather than failing.
 *
 * *** THE GATE IS THE CARD'S OWN ACCOUNT OF ITSELF ***
 *
 * After the switches, EXT_CSD is read AGAIN and bytes 183 and 185 are checked
 * against what was asked for.  This is a better gate than any pattern test:
 * the re-read is itself a data-phase transfer over the new width at the new
 * clock, so a bus that cannot carry data at the new setting fails the gate by
 * being unable to deliver the evidence -- and a card that quietly declined
 * the switch is caught saying so in its own register file.  A checksum test
 * alone would pass a host and card that had BOTH stayed at the old setting.
 *
 * EXT_CSD[196] DEVICE_TYPE says which speeds this card actually supports:
 *   bit 0  26 MHz   bit 1  52 MHz   bits 2-7  HS200 / HS400 / DDR variants
 * It is READ, not assumed, and the requested clock is clamped to it.  (The
 * PSRAM driver got five bugs from deriving values that were there to be read;
 * this driver has already had one.  The habit is now to look.)
 *
 * OUT OF SCOPE, ON PURPOSE: HS200 and HS400 need a tuning procedure and 1.8 V
 * signalling changes; DDR modes need a different clock relationship.  The
 * bench prints what it did NOT test so its table cannot be read as a ceiling.
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
    /* --- E3: the card's own account of its configuration (table 4) ------ */
    uint8_t  device_type;    /**< EXT_CSD[196]: speeds the card supports    */
    uint8_t  ext_bus_width;  /**< EXT_CSD[183] READ BACK after the switch   */
    uint8_t  ext_hs_timing;  /**< EXT_CSD[185] READ BACK after the switch   */
} tiku_emmc_id_t;

/*---------------------------------------------------------------------------*/
/* API -- E1/E2/E3 surface.  The tier and the staging demo land in E4.       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Power the host, reset the card, identify it, and go fast.
 *
 * Table 1 steps 1-8 at the mandatory 400 kHz / 1-bit identification setting,
 * then the table 4 upgrade to an 8-bit bus at high speed.  Fails closed and
 * bounded at every wait; the step tracer names the rung a wedge happened on.
 *
 * If the UPGRADE fails the card is left in the identification configuration,
 * which is slow but proven -- a driver that half-switched a bus is worse than
 * one that stayed slow, so this path never leaves the two ends disagreeing.
 */
tiku_emmc_err_t tiku_emmc_init(void);

/**
 * @brief Init to an explicit bus configuration -- one code path, two uses.
 *
 * @p width 1, 4 or 8; @p hz the requested bus clock (clamped to the card's
 * EXT_CSD[196] DEVICE_TYPE and to the host's divider grid).  `(1, 400000)`
 * reproduces the E2 configuration exactly, which is what makes the E3 bench
 * able to price the upgrade rather than merely assert it.
 */
tiku_emmc_err_t tiku_emmc_init_at(unsigned width, uint32_t hz);

/**
 * @brief Microseconds the last bring-up took: identification, and total.
 *
 * The init ceremony is a MEASURED QUANTITY, not a nuisance -- the lifecycle
 * policy (sleep vs power off vs stay up) is decided by this number against
 * the domain's idle rent, exactly as the PSRAM's ladder was.  @p ladder_us
 * is POR-to-transfer-ready at 400 kHz; the difference to @p total_us is what
 * the table 4 upgrade cost.  Either pointer may be NULL.
 */
void tiku_emmc_init_time(uint32_t *ladder_us, uint32_t *total_us);

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

/**
 * @brief E3 bench: sequential read/write, random-block latency, init cost.
 *
 * DWT-timed, work-denominated and checksum-gated, like `psrambench`.  Writes
 * touch the scratch region and nowhere else; every leg that cannot prove its
 * bytes were the right bytes reports FAIL instead of a bandwidth.  Prints
 * what it did NOT measure, so the table cannot be read as a ceiling.
 */
void tiku_emmc_bench_run(void);

/*---------------------------------------------------------------------------*/
/* E4 -- LIFECYCLE AND THE WAREHOUSE                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Put the card into CMD5 sleep; contents kept, bus quiet.
 *
 * The rung between "up" and "gone", and E3 is what justifies it: a full
 * bring-up costs ~52 ms, which is too much per access and acceptable per
 * wake.  Sleep is only legal from STANDBY, so this deselects first and
 * reselects on wake; block I/O refuses while asleep rather than timing out
 * against a card that is doing exactly what it was told.
 */
tiku_emmc_err_t tiku_emmc_sleep(void);

/** @brief Wake from CMD5 sleep and reselect; trusted once it answers again. */
tiku_emmc_err_t tiku_emmc_wake(void);

/** @brief 1 while the card is asleep. */
int tiku_emmc_asleep(void);

/**
 * @brief Microseconds the last sleep or wake took.
 *
 * The number the lifecycle policy turns on: sleep earns its place only
 * if waking costs far less than the ~52 ms of a full bring-up.
 */
uint32_t tiku_emmc_last_op_us(void);

/** @brief Capacity in 512 B blocks (0 until identified). */
uint32_t tiku_emmc_capacity_blocks(void);

/** @brief Bus clock in force, 0 when down. */
uint32_t tiku_emmc_clock_hz(void);

/** @brief Bus width in force (1/4/8), 0 when down. */
unsigned tiku_emmc_bus_width(void);

/** @brief Decoded identity without a validity gate -- for observability. */
const tiku_emmc_id_t *tiku_emmc_id(void);

/**
 * @brief Stage @p mb megabytes from the card into the PSRAM tier.
 *
 * eMMC DMA -> SSRAM bounce -> MSPI command queue -> PSRAM, checksum-gated
 * against the read path E3 proved bit-exact.  Reads only; writes nothing to
 * the card.  Transfer time and verification time are reported separately so
 * the bandwidth figure is the pipeline's and not the hash's.
 *
 * Compiled only when the PSRAM driver is present.
 */
void tiku_emmc_stage_run(uint32_t mb, uint32_t src_lba);

/*
 * Extent-driven staging (F4).  E4's stage takes one LBA span because that is
 * all a raw address can express; a FILE may be fragmented, so its extents are
 * fed in one at a time and appended to the PSRAM image in order.
 *
 *   open()            XIP down, counters and the source hash reset
 *   chunk(lba, nsec)  append one contiguous extent
 *   close(...)        read the image back OUT of the PSRAM, hash it, XIP up
 *
 * close() hashes the PSRAM rather than the bounce buffer on purpose: hashing
 * on the way in would only prove the card was read correctly, not that the
 * bytes are where the tier will look for them.
 */
void tiku_emmc_stage_open(void);
tiku_emmc_err_t tiku_emmc_stage_chunk(uint32_t lba, uint32_t nsec);
tiku_emmc_err_t tiku_emmc_stage_close(uint32_t total_bytes, uint32_t *src,
                                      uint32_t *dst, uint32_t *rd_us,
                                      uint32_t *wr_us);

/**
 * @brief Read-path diagnostic: block count x buffer location x DMA/PIO.
 *
 * Varies one thing at a time against a known-good reference, for when a
 * failure reports no controller error and the bench can only say that
 * something is wrong, not which of its three entangled variables did it.
 */
void tiku_emmc_diag_run(void);

#endif /* TIKU_EMMC_ARCH_H_ */
