/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_emmc_arch.c - Apollo510 SDIO0 and IS21EF08G eMMC bring-up.
 *
 * The card runs its own firmware and a state machine, so it must be walked idle
 * -> identify -> standby -> transfer; a command in the wrong state fails like a
 * wiring fault.  The init ladder is therefore linear, traced, and fails closed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#if defined(PLATFORM_AMBIQ) && (TIKU_DRV_EMMC_ENABLE + 0)

#include "tiku_emmc_arch.h"
#include "tiku_gpio_arch.h"
#include "tiku_cpu_common.h"
#include "apollo510.h"
#include "hal/tiku_cpu.h"                /* dcache clean/invalidate: DMA     */
#include <kernel/cpu/tiku_hang.h>
#include <kernel/shell/tiku_shell_io.h>  /* the bench reports via SHELL_PRINTF */

/*---------------------------------------------------------------------------*/
/* PADS (table 0)                                                            */
/*---------------------------------------------------------------------------*/

/*
 * THE PADS ARE A BOARD FACT AND NOW LIVE IN THE BOARD HEADER.  They were
 * hard-coded here, which silently made this driver Blue-EVB-only: RSTn is
 * GP13 on the Blue board and GP12 on the green one (table 0), so an eMMC
 * build for the green EVB drove a pad that is not its reset net.  The
 * silicon-side facts -- the SDIO0 controller, its registers, the FNCSEL
 * encoding -- stay here; only "which pad" moved out.
 */
#if !defined(TIKU_BOARD_EMMC_PAD_D0)
#error "This board declares no eMMC pads (TIKU_BOARD_EMMC_PAD_*). The build \
system should not have compiled tiku_emmc_arch.c for it -- see BOARD_CAPS in \
the Makefile."
#endif

#define EMMC_PAD_D0    TIKU_BOARD_EMMC_PAD_D0
#define EMMC_PAD_D3    TIKU_BOARD_EMMC_PAD_D3
#define EMMC_PAD_CLK   TIKU_BOARD_EMMC_PAD_CLK
#define EMMC_PAD_D4    TIKU_BOARD_EMMC_PAD_D4
#define EMMC_PAD_D7    TIKU_BOARD_EMMC_PAD_D7
#define EMMC_PAD_CMD   TIKU_BOARD_EMMC_PAD_CMD
#define EMMC_PAD_RST   TIKU_BOARD_EMMC_PAD_RST

/* emmc_pads_config() walks these as two contiguous runs (D0..CLK covers
 * DAT0-3 + CLK; D4..CMD covers DAT4-7 + CMD).  A board that re-pins the bus
 * non-contiguously would have the loops configure the WRONG pads and silently
 * leave the bus half-claimed, so the assumption is checked rather than
 * commented. */
_Static_assert(EMMC_PAD_CLK - EMMC_PAD_D0 == 4,
               "eMMC low run must be DAT0..DAT3,CLK contiguous");
_Static_assert(EMMC_PAD_D3 - EMMC_PAD_D0 == 3,
               "eMMC DAT0..DAT3 must be contiguous");
_Static_assert(EMMC_PAD_CMD - EMMC_PAD_D4 == 4,
               "eMMC high run must be DAT4..DAT7,CMD contiguous");
_Static_assert(EMMC_PAD_D7 - EMMC_PAD_D4 == 3,
               "eMMC DAT4..DAT7 must be contiguous");

/*
 * FUNCTION SELECT IS NOT UNIFORM ACROSS THIS BUS, AND ASSUMING IT WAS COST A
 * DEBUGGING ROUND.  Read from am_hal_pin.h, not inferred:
 *
 *   GP84..GP88  (DAT0-3, CLK)  ->  FNCSEL 2   (AM_HAL_PIN_84_SDIF0_DAT0 = 2)
 *   GP156..GP160 (DAT4-7, CMD) ->  FNCSEL 0   (AM_HAL_PIN_160_SDIF0_CMD = 0)
 *
 * With 0 written to all ten pads, DAT0-3 and CLK were left on some other
 * function entirely -- so the card was never clocked and never answered,
 * which presented as CMD-TIMEOUT on the first command that expects a
 * response.  This is the same mistake the PSRAM driver made five times over
 * (a value derived from context rather than read from the table); the fix
 * is the same discipline, applied per-pad.
 */
#define PAD_FNCSEL_SDIO_LOW   TIKU_BOARD_EMMC_FNCSEL_LOW   /* GP84..GP88   */
#define PAD_FNCSEL_SDIO_HIGH  TIKU_BOARD_EMMC_FNCSEL_HIGH  /* GP156..GP160 */
#define PAD_FNCSEL_GPIO  3u
#define PAD_DS_0P5X     (1u << 10)
#define PAD_OUTCFG_PP   (1u << 8)
#define PAD_INPEN       (1u << 4)

/* Data and CMD are bidirectional: input buffer on, controller owns drive. */
#define PAD_CFG_SDIO_LOW   (PAD_FNCSEL_SDIO_LOW  | PAD_DS_0P5X | PAD_INPEN)
#define PAD_CFG_SDIO_HIGH  (PAD_FNCSEL_SDIO_HIGH | PAD_DS_0P5X | PAD_INPEN)
#define PAD_CFG_GPIO_OUT (PAD_FNCSEL_GPIO | PAD_DS_0P5X | PAD_OUTCFG_PP | \
                          PAD_INPEN)

/*---------------------------------------------------------------------------*/
/* MMC COMMANDS (table 2)                                                    */
/*---------------------------------------------------------------------------*/

#define MMC_GO_IDLE          0u
#define MMC_SEND_OP_COND     1u
#define MMC_SLEEP_AWAKE      5u
#define MMC_ALL_SEND_CID     2u
#define MMC_SET_RELATIVE_ADDR 3u
#define MMC_SWITCH           6u
#define MMC_SELECT_CARD      7u
#define MMC_SEND_EXT_CSD     8u
#define MMC_SEND_CSD         9u
#define MMC_STOP_TRANSMISSION 12u
#define MMC_SEND_STATUS      13u
#define MMC_SET_BLOCKLEN     16u
#define MMC_READ_SINGLE      17u
#define MMC_READ_MULTIPLE    18u
#define MMC_WRITE_SINGLE     24u
#define MMC_WRITE_MULTIPLE   25u

/** OCR for a >2 GB card: sector addressing + the full voltage window. */
#define MMC_OCR_SECTOR_MODE  0x40FF8080u
#define MMC_OCR_BUSY         0x80000000u

/** Response encodings for TRANSFER.RESPTYPESEL (table 3). */
#define RESP_NONE   0u
#define RESP_136    1u
#define RESP_48     2u
#define RESP_48BUSY 3u

/** THE ONLY EXT_CSD INDEXES THIS DRIVER MAY WRITE.  See table 2: the rest
 *  are one-time-programmable and can brick features permanently. */
#define EXT_CSD_BUS_WIDTH   183u
#define EXT_CSD_HS_TIMING   185u

/** EXT_CSD indexes this driver READS (harmless, and there is no allow-list
 *  for reading -- only writing can destroy anything). */
#define EXT_CSD_DEVICE_TYPE 196u
#define EXT_CSD_REV         192u
#define EXT_CSD_SEC_COUNT   212u
#define EXT_CSD_S_A_TIMEOUT 217u   /* sleep/awake timeout: 100 ns * 2^n     */

/** EXT_CSD[196] DEVICE_TYPE: which speeds the CARD says it supports. */
#define DEVTYPE_HS_26MHZ    (1u << 0)
#define DEVTYPE_HS_52MHZ    (1u << 1)

/** CMD6 SWITCH argument: access mode in [25:24], index [23:16], value [15:8]. */
#define SWITCH_ACCESS_WRITE_BYTE  3u

/** R1 card-status bits used by the busy poll. */
#define R1_SWITCH_ERROR    (1u << 7)
#define R1_READY_FOR_DATA  (1u << 8)
#define R1_STATE_Pos       9u
#define R1_STATE_Msk       (0xFu << 9)
#define R1_ERROR_MASK      0xFDFFA080u    /* the spec's error bits, collected */
#define MMC_STATE_TRAN     4u

/**
 * @brief SDMA boundary: how far the engine runs before it wants attention.
 *
 * SDHCI's simple DMA mode interrupts whenever the destination address crosses a
 * power-of-two boundary, and software restarts it by writing the address back.
 * 512 KB (encoding 7) is the largest offered: one service per scratch transfer.
 *
 * @note A boundary field can corrupt rather than merely slow -- the PSRAM's
 *       DMABOUND gave identical timing and wrong data above 4 KB.  Every DMA
 *       leg below is therefore checksum-gated against a known pattern.
 */
#define EMMC_SDMA_BOUND     7u    /* 0=4K, 1=8K, ... 7=512K                  */

/** Biggest single command: BLKCNT is 16 bits, so 65535 blocks = 32 MB. */
#define EMMC_MAX_BLKCNT     65535u

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

static uint8_t  s_up;
static uint32_t s_rca;
static uint32_t s_sec_count;
static uint32_t s_clock_hz;
static uint8_t  s_bus_width = 1u;
static uint8_t  s_base_mhz;
static uint8_t  s_devtype;     /**< EXT_CSD[196] -- speeds the card allows  */
static uint8_t  s_asleep;      /**< 1 while the card is in CMD5 sleep       */
static uint8_t  s_sa_timeout;  /**< EXT_CSD[217] raw exponent               */
static uint32_t s_last_err;    /**< INTSTAT at the last command failure     */
static uint32_t s_ladder_us;   /**< POR -> transfer-ready at 400 kHz        */
static uint32_t s_op_us;       /**< duration of the last sleep/wake          */
static uint32_t s_init_us;     /**< POR -> the configuration finally in use */
static tiku_emmc_id_t s_id;
static void   (*s_trace)(const char *step);

static void trace(const char *s) { if (s_trace) { s_trace(s); } }
void tiku_emmc_set_trace(void (*fn)(const char *)) { s_trace = fn; }

/** EXT_CSD image, re-read after every configuration change (table 4). */
static uint8_t s_ext[TIKU_EMMC_BLOCK_SIZE] __attribute__((aligned(4)));

/*---------------------------------------------------------------------------*/
/* CYCLE CLOCK -- shared by the init timer and the bench                     */
/*---------------------------------------------------------------------------*/

extern unsigned long tiku_cpu_ambiq_clock_get_hz(void);

static void cyc_enable(void)
{
    volatile uint32_t *demcr  = (volatile uint32_t *)0xE000EDFCUL;
    volatile uint32_t *dwtctl = (volatile uint32_t *)0xE0001000UL;
    *demcr  |= (1u << 24);      /* TRCENA                                    */
    *dwtctl |= 1u;              /* CYCCNTENA                                 */
}

static inline uint32_t cyc_now(void)
{
    return *(volatile uint32_t *)0xE0001004UL;
}

static uint32_t cyc_to_us(uint32_t cyc)
{
    unsigned long hz = tiku_cpu_ambiq_clock_get_hz();
    return (hz == 0u) ? 0u : (uint32_t)(((uint64_t)cyc * 1000000u) / hz);
}

int tiku_emmc_powered(void)
{
    return ((PWRCTRL->DEVPWRSTATUS & PWRCTRL_DEVPWRSTATUS_PWRSTSDIO0_Msk) != 0u)
           ? 1 : 0;
}

uint32_t tiku_emmc_scratch_lba(void)
{
    return (s_sec_count > TIKU_EMMC_SCRATCH_BLOCKS)
           ? (s_sec_count - TIKU_EMMC_SCRATCH_BLOCKS) : 0u;
}

/*---------------------------------------------------------------------------*/
/* COMMAND ENGINE                                                            */
/*---------------------------------------------------------------------------*/

#define EMMC_CMD_SPINS   20000u   /* -> ~200 ms per command   (see backoff)  */
/* No EMMC_DATA_SPINS any more: data phases are bounded by TIME, below.     */

/*
 * A SPIN COUNT IS NOT A TIME LIMIT, AND PRETENDING OTHERWISE COST A RUN.
 *
 * The data phase originally waited a fixed 50000 iterations, which the
 * backoff schedule turns into roughly half a second.  That is comfortable at
 * 8 bits and 48 MHz, where even a 512 KB transfer finishes in 14 ms -- and
 * hopeless at 1 bit and 375 kHz, where 128 blocks need 1.4 SECONDS.  So the
 * driver timed out in the middle of transfers that were proceeding perfectly,
 * returned ERR_TIMEOUT, and left the caller holding a partly-filled buffer.
 *
 * It presented as the nastiest shape available: no controller error (a
 * timeout is OURS, so INTSTAT stays clean and s_last_err stays zero),
 * "successful-looking" legs with wrong data, and read rates six times what
 * the wire could carry -- because the bench divided a full span by the time
 * spent before the abort.  Every symptom pointed at the silicon; the bug was
 * a constant in this file.
 *
 * The fix is to stop counting iterations and start counting TIME, with the
 * budget derived from the work: bytes / (clock x width), times a slack
 * factor, plus a fixed allowance for the card's own programming.  A budget
 * that scales with the transfer cannot silently shrink when the bus slows.
 */
#define EMMC_XFER_SLACK      4u          /* x expected wire time             */
#define EMMC_PROGRAM_US 500000u          /* + card programming allowance     */
#define EMMC_READY_US  5000000u          /* CMD13 busy poll after a write    */
#define EMMC_CHUNK_US  2000000u          /* max wire time in ONE command     */

/*
 * Backstop for the time budgets above.  A loop bounded only by a cycle counter
 * is unbounded if that counter ever stops -- and DWT's CYCCNT is a debug
 * resource that a probe detaching, or a low-power transition, can switch off.
 * The largest budget here is 8 s, which at the backoff's 10 us floor is about
 * 800k iterations, so four million is far above any legitimate wait and still
 * finite.  Bounded waits are a rule in this port, and a rule with an exception
 * for "the clock is fine" is not one.
 */
#define EMMC_SPIN_CEILING 4000000u

/**
 * @brief Escalating poll backoff -- tight, then 2 us, then 10 us.
 *
 * A tight spin forever is bus traffic aimed at the controller doing the work
 * (20 % of the DMA plateau on the PSRAM), but a flat 10 us floor dominates once
 * the clock reaches 48 MHz, where a single-block read is ~11 us on the wire.
 *
 * @note Free for the first 64 reads (short transfers finish inside that window
 *       and pay nothing), 2 us to about half a millisecond, 10 us thereafter.
 */
static void poll_backoff(uint32_t iter)
{
    if (iter < 64u)        { __NOP(); }
    else if (iter < 512u)  { tiku_cpu_ambiq_delay_us(2u); }
    else                   { tiku_cpu_ambiq_delay_us(10u); }
}

/** @brief Microseconds @p n_blk blocks need on the wire at the live setting. */
static uint32_t emmc_wire_us(uint32_t n_blk)
{
    uint32_t hz = s_clock_hz ? s_clock_hz : 400000u;
    uint32_t w  = s_bus_width ? s_bus_width : 1u;
    uint64_t us = ((uint64_t)n_blk * TIKU_EMMC_BLOCK_SIZE * 8u * 1000000u) /
                  ((uint64_t)hz * w);
    return (us > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)us;
}

/** @brief Deadline in CPU cycles for a data phase of @p n_blk blocks. */
static uint32_t emmc_xfer_budget_cyc(uint32_t n_blk)
{
    unsigned long hz = tiku_cpu_ambiq_clock_get_hz();
    uint64_t us = (uint64_t)emmc_wire_us(n_blk) * EMMC_XFER_SLACK
                  + EMMC_PROGRAM_US;
    uint64_t cyc = (us * (uint64_t)hz) / 1000000u;
    /* Keep the deadline inside a 32-bit cycle difference: at 250 MHz the
     * counter wraps every 17 s, so a budget near that is unreadable. */
    if (cyc > 2000000000u) { cyc = 2000000000u; }
    return (uint32_t)cyc;
}

/**
 * @brief Blocks per command: bounded by BLKCNT and by wall-clock sanity.
 *
 * Chunking exists so no single command can run for an unbounded time at a
 * slow bus setting -- which is what keeps the budget above from having to
 * cover a 65535-block transfer at 375 kHz (twelve minutes).
 */
static uint32_t emmc_max_chunk(void)
{
    uint32_t hz = s_clock_hz ? s_clock_hz : 400000u;
    uint32_t w  = s_bus_width ? s_bus_width : 1u;
    uint64_t blks = ((uint64_t)hz * w * (EMMC_CHUNK_US / 1000u)) /
                    (TIKU_EMMC_BLOCK_SIZE * 8u * 1000u);
    if (blks == 0u)               { blks = 1u; }
    if (blks > EMMC_MAX_BLKCNT)   { blks = EMMC_MAX_BLKCNT; }
    return (uint32_t)blks;
}

static tiku_emmc_err_t emmc_cmd_x(uint8_t idx, uint32_t arg, unsigned resp_type,
                                  int crc, int idxchk, int data,
                                  uint32_t xfer_mode, uint32_t *resp);

/**
 * @brief Issue one MMC command and collect its response.
 *
 * @param idx        command index
 * @param arg        argument register value
 * @param resp_type  RESP_* (table 3)
 * @param crc        check the response CRC (off for R3 -- OCR has none)
 * @param idxchk     check the echoed index (off for R3 and R2)
 * @param data       non-zero if a data phase follows
 * @param resp       out: RESPONSE0..3 (may be NULL); [0] alone for 48-bit
 */
static tiku_emmc_err_t emmc_cmd(uint8_t idx, uint32_t arg, unsigned resp_type,
                                int crc, int idxchk, int data, uint32_t *resp)
{
    return emmc_cmd_x(idx, arg, resp_type, crc, idxchk, data, 0u, resp);
}

/**
 * @brief The full form: transfer-mode bits travel WITH the command.
 *
 * TRANSFER is one 32-bit register holding the transfer-mode fields (direction,
 * block-count enable, DMA) in its low half and the command in its high half, and
 * writing it STARTS the command -- so an earlier mode-only write gets erased.
 *
 * @param xfer_mode extra low-half bits (DXFERDIRSEL, BLKCNTEN, ...)
 */
static tiku_emmc_err_t emmc_cmd_x(uint8_t idx, uint32_t arg, unsigned resp_type,
                                  int crc, int idxchk, int data,
                                  uint32_t xfer_mode, uint32_t *resp)
{
    uint32_t xfer = xfer_mode;
    uint32_t spins;
    /* A data command additionally needs the DAT lines free.  E2 only ever
     * issued one command per 512 bytes and the card was always long done by
     * the time the next arrived; multi-block WRITES leave the card busy
     * PROGRAMMING with DAT0 held low, and issuing the next command into that
     * is how a fast write path starts failing under load and nowhere else. */
    const uint32_t inhibit = SDIO0_PRESENT_CMDINHCMD_Msk |
                             (data ? SDIO0_PRESENT_CMDINHDAT_Msk : 0u);

    for (spins = 0u; spins < EMMC_CMD_SPINS; spins++) {
        if ((SDIO0->PRESENT & inhibit) == 0u) { break; }
        poll_backoff(spins);
    }
    if (spins == EMMC_CMD_SPINS) { return TIKU_EMMC_ERR_TIMEOUT; }

    SDIO0->INTSTAT  = 0xFFFFFFFFu;          /* write-1-to-clear             */
    SDIO0->ARGUMENT1 = arg;

    xfer |= ((uint32_t)resp_type << SDIO0_TRANSFER_RESPTYPESEL_Pos)
            & SDIO0_TRANSFER_RESPTYPESEL_Msk;
    if (crc)    { xfer |= SDIO0_TRANSFER_CMDCRCCHKEN_Msk; }
    if (idxchk) { xfer |= SDIO0_TRANSFER_CMDIDXCHKEN_Msk; }
    if (data)   { xfer |= SDIO0_TRANSFER_DATAPRSNTSEL_Msk; }
    xfer |= ((uint32_t)idx << SDIO0_TRANSFER_CMDIDX_Pos)
            & SDIO0_TRANSFER_CMDIDX_Msk;
    SDIO0->TRANSFER = xfer;                  /* writing TRANSFER starts it  */

    /* Backoff poll: a tight spin on INTSTAT is bus traffic into the host
     * that is currently clocking the card. */
    for (spins = 0u; spins < EMMC_CMD_SPINS; spins++) {
        uint32_t st = SDIO0->INTSTAT;
        if ((st & SDIO0_INTSTAT_ERRORINTERRUPT_Msk) != 0u) {
            s_last_err = st;      /* keep it: which error matters           */
            /* Reset the command line so the next attempt starts clean. */
            SDIO0->CLOCKCTRL_b.SWRSTCMD = 1u;
            { uint32_t g = 1000u;
              while (SDIO0->CLOCKCTRL_b.SWRSTCMD != 0u && --g != 0u) { } }
            return TIKU_EMMC_ERR_CMD;
        }
        if ((st & SDIO0_INTSTAT_COMMANDCOMPLETE_Msk) != 0u) { break; }
        poll_backoff(spins);
    }
    if (spins == EMMC_CMD_SPINS) { return TIKU_EMMC_ERR_TIMEOUT; }

    if (resp != (uint32_t *)0) {
        resp[0] = SDIO0->RESPONSE0;
        if (resp_type == RESP_136) {
            resp[1] = SDIO0->RESPONSE1;
            resp[2] = SDIO0->RESPONSE2;
            resp[3] = SDIO0->RESPONSE3;
        }
    }
    return TIKU_EMMC_OK;
}

/** @brief Read one 512-byte block out of the host buffer, PIO. */
static tiku_emmc_err_t emmc_read_buffer(uint8_t *dst)
{
    uint32_t spins;
    uint32_t i;
    uint32_t t0 = cyc_now();
    uint32_t budget = emmc_xfer_budget_cyc(1u);

    for (spins = 0u; ; spins++) {
        uint32_t st = SDIO0->INTSTAT;
        if ((st & SDIO0_INTSTAT_ERRORINTERRUPT_Msk) != 0u) {
            s_last_err = st;
            return TIKU_EMMC_ERR_CMD;
        }
        if ((st & SDIO0_INTSTAT_BUFFERREADREADY_Msk) != 0u) { break; }
        poll_backoff(spins);
        if ((cyc_now() - t0) > budget ||
            spins > EMMC_SPIN_CEILING) { return TIKU_EMMC_ERR_TIMEOUT; }
    }
    SDIO0->INTSTAT = SDIO0_INTSTAT_BUFFERREADREADY_Msk;

    for (i = 0u; i < TIKU_EMMC_BLOCK_SIZE; i += 4u) {
        uint32_t w = SDIO0->BUFFER;
        dst[i]     = (uint8_t)w;
        dst[i + 1] = (uint8_t)(w >> 8);
        dst[i + 2] = (uint8_t)(w >> 16);
        dst[i + 3] = (uint8_t)(w >> 24);
    }
    return TIKU_EMMC_OK;
}

/** @brief Write one 512-byte block into the host buffer, PIO. */
static tiku_emmc_err_t emmc_write_buffer(const uint8_t *src)
{
    uint32_t spins;
    uint32_t i;
    uint32_t t0 = cyc_now();
    uint32_t budget = emmc_xfer_budget_cyc(1u);

    for (spins = 0u; ; spins++) {
        uint32_t st = SDIO0->INTSTAT;
        if ((st & SDIO0_INTSTAT_ERRORINTERRUPT_Msk) != 0u) {
            s_last_err = st;
            return TIKU_EMMC_ERR_CMD;
        }
        if ((st & SDIO0_INTSTAT_BUFFERWRITEREADY_Msk) != 0u) { break; }
        poll_backoff(spins);
        if ((cyc_now() - t0) > budget ||
            spins > EMMC_SPIN_CEILING) { return TIKU_EMMC_ERR_TIMEOUT; }
    }
    SDIO0->INTSTAT = SDIO0_INTSTAT_BUFFERWRITEREADY_Msk;

    for (i = 0u; i < TIKU_EMMC_BLOCK_SIZE; i += 4u) {
        SDIO0->BUFFER = (uint32_t)src[i]
                      | ((uint32_t)src[i + 1] << 8)
                      | ((uint32_t)src[i + 2] << 16)
                      | ((uint32_t)src[i + 3] << 24);
    }
    return TIKU_EMMC_OK;
}

/**
 * @brief Wait for TRANSFERCOMPLETE, servicing SDMA boundary crossings.
 *
 * When the destination crosses the boundary in BLOCK.HOSTSDMABUFSZ the engine
 * STOPS, raises DMAINTERRUPT and leaves the continue-from address in SDMA;
 * writing it back is the whole of "restart".  Miss it and nothing completes.
 *
 * @note Boundary service does NOT back off: every microsecond here is a
 *       microsecond the bus is idle mid-transfer.
 */
static tiku_emmc_err_t emmc_wait_xfer(uint32_t budget_cyc)
{
    uint32_t spins = 0u;
    uint32_t t0 = cyc_now();

    for (;;) {
        uint32_t st = SDIO0->INTSTAT;
        if ((st & SDIO0_INTSTAT_ERRORINTERRUPT_Msk) != 0u) {
            s_last_err = st;
            return TIKU_EMMC_ERR_CMD;
        }
        if ((st & SDIO0_INTSTAT_TRANSFERCOMPLETE_Msk) != 0u) {
            SDIO0->INTSTAT = SDIO0_INTSTAT_TRANSFERCOMPLETE_Msk;
            return TIKU_EMMC_OK;
        }
        if ((st & SDIO0_INTSTAT_DMAINTERRUPT_Msk) != 0u) {
            uint32_t next = SDIO0->SDMA;      /* where it wants to resume    */
            SDIO0->INTSTAT = SDIO0_INTSTAT_DMAINTERRUPT_Msk;
            SDIO0->SDMA = next;
            t0 = cyc_now();                   /* progress: restart the clock */
            spins = 0u;
            continue;
        }
        poll_backoff(spins++);
        tiku_hang_checkin();
        if ((cyc_now() - t0) > budget_cyc ||
            spins > EMMC_SPIN_CEILING) { return TIKU_EMMC_ERR_TIMEOUT; }
    }
}

/**
 * @brief Poll CMD13 until the card is out of PROGRAMMING and back in TRAN.
 *
 * CMD6 and every write leave the card busy on its own flash, and the host's
 * TRANSFERCOMPLETE says only that the BUS is free.  Issuing a SWITCH into that
 * window is how a configuration change gets silently dropped.
 */
static tiku_emmc_err_t emmc_wait_ready(void)
{
    uint32_t resp[4];
    uint32_t spins;
    uint32_t t0 = cyc_now();
    uint32_t budget = (uint32_t)(((uint64_t)EMMC_READY_US *
                                  tiku_cpu_ambiq_clock_get_hz()) / 1000000u);

    for (spins = 0u; ; spins++) {
        tiku_emmc_err_t rc = emmc_cmd(MMC_SEND_STATUS, s_rca << 16,
                                      RESP_48, 1, 1, 0, resp);
        if (rc != TIKU_EMMC_OK) { return rc; }
        /* SWITCH_ERROR is sticky and reported HERE, not on the CMD6 itself:
         * a card that refused the switch answers the command perfectly and
         * then admits it in the next status.  Without this check the driver
         * would reconfigure the host to a width the card never adopted. */
        if ((resp[0] & R1_SWITCH_ERROR) != 0u) { return TIKU_EMMC_ERR_CMD; }
        if ((resp[0] & R1_READY_FOR_DATA) != 0u &&
            ((resp[0] & R1_STATE_Msk) >> R1_STATE_Pos) == MMC_STATE_TRAN) {
            return TIKU_EMMC_OK;
        }
        poll_backoff(spins);
        tiku_hang_checkin();
        if ((cyc_now() - t0) > budget ||
            spins > EMMC_SPIN_CEILING) { return TIKU_EMMC_ERR_TIMEOUT; }
    }
}

/*---------------------------------------------------------------------------*/
/* HOST BRING-UP (table 1)                                                   */
/*---------------------------------------------------------------------------*/

static tiku_emmc_err_t emmc_power_on(void)
{
    uint32_t spins = 100000u;

    /* THREE THINGS, AND THE DOMAIN IS ONLY THE FIRST.  Powering SDIO0 gives
     * the block a supply; it does not give it a CLOCK, and a host with no
     * clock accepts a software-reset request and never completes it -- which
     * is exactly how this presented (SWRSTALL set forever).  The MSPI
     * bring-up learned the same shape: force the oscillator, then gate the
     * peripheral's own clock enables.  Here that is:
     *
     *   1. PWRCTRL.DEVPWREN.PWRENSDIO0    the power domain
     *   2. CLKGEN.MISC.FRCHFRC            HFRC forced on (the clock manager
     *                                     does this for the first user)
     *   3. MCUCTRL.SDIO0CTRL.SDIO0SYSCLKEN + SDIO0XINCLKEN
     *                                     the host's system and card clocks
     *
     * The vendor does (2) via am_hal_clkmgr_clock_request(HFRC) and (3)
     * explicitly in am_hal_sdhc_power_control(). */
    PWRCTRL->DEVPWREN |= PWRCTRL_DEVPWREN_PWRENSDIO0_Msk;
    __DSB();
    while (((PWRCTRL->DEVPWRSTATUS & PWRCTRL_DEVPWRSTATUS_PWRSTSDIO0_Msk) == 0u)
           && --spins != 0u) { }
    if (spins == 0u) { return TIKU_EMMC_ERR_POWER; }

    CLKGEN->MISC |= CLKGEN_MISC_FRCHFRC_Msk;
    __DSB();

    MCUCTRL->SDIO0CTRL |= (MCUCTRL_SDIO0CTRL_SDIO0SYSCLKEN_Msk |
                           MCUCTRL_SDIO0CTRL_SDIO0XINCLKEN_Msk);
    __DSB();
    tiku_cpu_ambiq_delay_us(100u);
    return TIKU_EMMC_OK;
}

/**
 * @brief Set the bus clock.  Divider is a power of two; enabling is a dance.
 *
 * FREQSEL takes divider>>1, and the sequence is CLKEN -> poll CLKSTABLE ->
 * SDCLKEN.  Skipping the stability poll clocks the card before the host's PLL
 * has settled, which presents as a card that answers intermittently.
 */
static tiku_emmc_err_t emmc_set_clock(uint32_t target_hz)
{
    uint32_t base = (uint32_t)s_base_mhz * 1000000u;
    uint32_t div, spins;

    if (base == 0u) { return TIKU_EMMC_ERR_CLOCK; }
    for (div = 1u; div <= 256u; div *= 2u) {
        if ((base / div) <= target_hz) { break; }
    }
    if (div > 256u) { div = 256u; }

    SDIO0->CLOCKCTRL &= ~(SDIO0_CLOCKCTRL_CLKEN_Msk |
                          SDIO0_CLOCKCTRL_SDCLKEN_Msk);
    SDIO0->CLOCKCTRL_b.FREQSEL = (div >> 1);
    SDIO0->CLOCKCTRL_b.CLKEN   = 1u;
    __DSB();

    spins = 1000u;
    while (SDIO0->CLOCKCTRL_b.CLKSTABLE == 0u && --spins != 0u) {
        tiku_cpu_ambiq_delay_us(10u);
    }
    if (spins == 0u) { return TIKU_EMMC_ERR_CLOCK; }

    SDIO0->CLOCKCTRL_b.SDCLKEN = 1u;
    __DSB();
    s_clock_hz = base / div;
    return TIKU_EMMC_OK;
}

/*
 * PAD-BY-PAD, AND TRACED.  The first run with the corrected function selects
 * wedged the board hard enough that SWD died at every speed and reset type --
 * the third such wedge this week, and the only new thing was these ten pads.
 * So each pad announces itself before it is claimed: if it happens again the
 * last line on the wire names the exact pad, instead of leaving ten suspects.
 * Cheap insurance, and it costs nothing once the bring-up is trusted.
 */
static void emmc_pads_config(void)
{
    uint32_t p;
    static char msg[16];

    for (p = EMMC_PAD_D0; p <= EMMC_PAD_CLK; p++) {      /* 84..88  */
        msg[0] = 'p'; msg[1] = 'a'; msg[2] = 'd'; msg[3] = ' ';
        msg[4] = (char)('0' + (p / 100u));
        msg[5] = (char)('0' + ((p / 10u) % 10u));
        msg[6] = (char)('0' + (p % 10u));
        msg[7] = '\0';
        trace(msg);
        tiku_ambiq_gpio_pad_config(p, PAD_CFG_SDIO_LOW);
    }
    for (p = EMMC_PAD_D4; p <= EMMC_PAD_CMD; p++) {      /* 156..160 */
        msg[0] = 'p'; msg[1] = 'a'; msg[2] = 'd'; msg[3] = ' ';
        msg[4] = (char)('0' + (p / 100u));
        msg[5] = (char)('0' + ((p / 10u) % 10u));
        msg[6] = (char)('0' + (p % 10u));
        msg[7] = '\0';
        trace(msg);
        tiku_ambiq_gpio_pad_config(p, PAD_CFG_SDIO_HIGH);
    }
}

/** @brief Pulse the card's reset line (GP13 -- see table 0). */
static void emmc_card_reset(void)
{
    tiku_ambiq_gpio_pad_config(EMMC_PAD_RST, PAD_CFG_GPIO_OUT);
    tiku_ambiq_gpio_set(EMMC_PAD_RST, 1u);
    tiku_cpu_ambiq_delay_us(200u);
    tiku_ambiq_gpio_set(EMMC_PAD_RST, 0u);
    tiku_cpu_ambiq_delay_us(200u);      /* well past the spec's 1 us min    */
    tiku_ambiq_gpio_set(EMMC_PAD_RST, 1u);
    tiku_cpu_ambiq_delay_us(2000u);     /* card boot time                   */
}

/*---------------------------------------------------------------------------*/
/* THE E3 UPGRADE (table 4)                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Read the 512-byte EXT_CSD register into @p out (PIO, one block). */
static tiku_emmc_err_t emmc_read_ext_csd(uint8_t *out)
{
    uint32_t resp[4];
    tiku_emmc_err_t rc;

    /* PIO on purpose: this runs before DMA is trusted, it is 512 bytes once
     * per configuration change, and keeping it on the simplest path means a
     * DMA bug can never masquerade as a card that forgot its own settings. */
    SDIO0->BLOCK = TIKU_EMMC_BLOCK_SIZE;
    rc = emmc_cmd_x(MMC_SEND_EXT_CSD, 0u, RESP_48, 1, 1, 1,
                    SDIO0_TRANSFER_DXFERDIRSEL_Msk, resp);
    if (rc == TIKU_EMMC_OK) { rc = emmc_read_buffer(out); }
    if (rc == TIKU_EMMC_OK) { rc = emmc_wait_xfer(emmc_xfer_budget_cyc(1u)); }
    return rc;
}

/**
 * @brief CMD6 SWITCH -- and the allow-list that keeps this card alive.
 *
 * EXT_CSD is partly one-time-programmable, and a wrong index does not error: it
 * permanently disables a feature, repartitions the device, or locks a boot
 * configuration.  Only 183 and 185 may be written, and there is no force flag.
 */
static tiku_emmc_err_t emmc_switch(uint8_t index, uint8_t value)
{
    uint32_t resp[4];
    tiku_emmc_err_t rc;
    uint32_t arg;

    if (index != EXT_CSD_BUS_WIDTH && index != EXT_CSD_HS_TIMING) {
        return TIKU_EMMC_ERR_ARG;
    }

    arg = ((uint32_t)SWITCH_ACCESS_WRITE_BYTE << 24) |
          ((uint32_t)index << 16) | ((uint32_t)value << 8);
    rc = emmc_cmd(MMC_SWITCH, arg, RESP_48BUSY, 1, 1, 0, resp);
    if (rc != TIKU_EMMC_OK) { return rc; }
    /* R1b means the card holds DAT0 low while it applies the change; the
     * status poll is what makes the next command safe, and it is also where
     * a REFUSED switch is confessed (SWITCH_ERROR). */
    return emmc_wait_ready();
}

/**
 * @brief Widen the bus: card first, then host (table 4, and the order matters).
 */
static tiku_emmc_err_t emmc_set_bus_width(unsigned bits)
{
    uint8_t code;
    tiku_emmc_err_t rc;

    switch (bits) {
    case 1u: code = 0u; break;
    case 4u: code = 1u; break;
    case 8u: code = 2u; break;
    default: return TIKU_EMMC_ERR_ARG;
    }

    rc = emmc_switch(EXT_CSD_BUS_WIDTH, code);
    if (rc != TIKU_EMMC_OK) { return rc; }

    /* Now the host.  Between the two writes the ends disagree about the bus
     * width; no data command may be issued in that gap, which is why this
     * function does the whole change and callers cannot do half of it. */
    SDIO0->HOSTCTRL1_b.XFERWIDTH = (bits == 8u) ? 1u : 0u;
    SDIO0->HOSTCTRL1_b.DATATRANSFERWIDTH = (bits == 4u) ? 1u : 0u;
    __DSB();
    s_bus_width = (uint8_t)bits;
    return TIKU_EMMC_OK;
}

/**
 * @brief High-speed timing, clamped to what EXT_CSD[196] says the card allows.
 *
 * The clamp is the point: "eMMC high speed is 52 MHz" is true of the standard
 * and not necessarily of the part in front of you.  DEVICE_TYPE is a register,
 * so it is consulted rather than assumed.
 */
static tiku_emmc_err_t emmc_set_high_speed(uint32_t target_hz)
{
    uint32_t ceiling;
    tiku_emmc_err_t rc;

    if ((s_devtype & DEVTYPE_HS_52MHZ) != 0u)      { ceiling = 52000000u; }
    else if ((s_devtype & DEVTYPE_HS_26MHZ) != 0u) { ceiling = 26000000u; }
    else {
        /* The card claims no high-speed mode at all.  Stay at legacy timing
         * and let the caller's clock request stand or fall on its own. */
        return emmc_set_clock(target_hz);
    }
    if (target_hz > ceiling) { target_hz = ceiling; }

    rc = emmc_switch(EXT_CSD_HS_TIMING, 1u);
    if (rc != TIKU_EMMC_OK) { return rc; }

    SDIO0->HOSTCTRL1_b.HISPEEDEN = SDIO0_HOSTCTRL1_HISPEEDEN_HIGH;
    __DSB();
    return emmc_set_clock(target_hz);
}

/*---------------------------------------------------------------------------*/
/* THE LADDER (table 2)                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Put both ends back at the identification setting after a failure.
 *
 * HOST FIRST, the reverse of the upgrade order: an upgrade fails when the CARD
 * did not adopt the setting, so the host is what is out of step and narrowing it
 * is what makes the card reachable.  The CMD6s afterwards are best-effort.
 */
static void emmc_fallback_slow(void)
{
    SDIO0->HOSTCTRL1_b.XFERWIDTH = 0u;
    SDIO0->HOSTCTRL1_b.DATATRANSFERWIDTH = 0u;
    SDIO0->HOSTCTRL1_b.HISPEEDEN = SDIO0_HOSTCTRL1_HISPEEDEN_NORMAL;
    __DSB();
    (void)emmc_set_clock(400000u);
    s_bus_width = 1u;
    (void)emmc_switch(EXT_CSD_BUS_WIDTH, 0u);
    (void)emmc_switch(EXT_CSD_HS_TIMING, 0u);
}

tiku_emmc_err_t tiku_emmc_init(void)
{
    return tiku_emmc_init_at(8u, 48000000u);
}

tiku_emmc_err_t tiku_emmc_init_at(unsigned width, uint32_t hz)
{
    tiku_emmc_err_t rc;
    uint32_t resp[4];
    uint32_t spins;
    uint32_t t_start;

    cyc_enable();
    t_start = cyc_now();
    s_ladder_us = 0u;
    s_init_us   = 0u;
    s_asleep    = 0u;

    trace("power");
    rc = emmc_power_on();
    if (rc != TIKU_EMMC_OK) { return rc; }

    /* Full host reset before anything: the card may have been left mid-
     * transaction by a previous boot, and SWRSTALL is the only way back. */
    trace("host-reset");
    SDIO0->CLOCKCTRL_b.SWRSTALL = 1u;
    spins = 10000u;
    while (SDIO0->CLOCKCTRL_b.SWRSTALL != 0u && --spins != 0u) {
        tiku_cpu_ambiq_delay_us(10u);
    }
    if (spins == 0u) { return TIKU_EMMC_ERR_TIMEOUT; }

    s_base_mhz = (uint8_t)SDIO0->CAPABILITIES0_b.SDCLKFREQ;
    if (s_base_mhz == 0u) { return TIKU_EMMC_ERR_CLOCK; }

    /* ENABLE THE STATUS BITS, OR NOTHING IS EVER OBSERVABLE.
     *
     * SDHCI splits interrupt control in two: INTENABLE decides which events
     * may APPEAR in INTSTAT at all, and INTSIG decides which of those also
     * raise a CPU interrupt.  A driver that polls INTSTAT still needs
     * INTENABLE set -- otherwise commands complete perfectly and the status
     * register stays stubbornly zero, which is precisely how CMD0 presented
     * here: no completion, no error, bus powered, clock stable, CMD line not
     * inhibited.  INTSIG stays 0: this driver polls and takes no interrupts. */
    SDIO0->INTENABLE = 0xFFFFFFFFu;
    SDIO0->INTSIG    = 0u;
    __DSB();

    /*
     * GIVE THE CARD TIME TO ANSWER, OR IT WILL BE CUT OFF MID-WRITE.
     *
     * CLOCKCTRL.TIMEOUTCNT sets the DATA timeout as 2^(13+n) ticks of TMCLK,
     * whose rate CAPABILITIES0[7:0] reports (1 MHz here).  A software reset
     * leaves it at 0, i.e. 8.192 ms -- and an eMMC is allowed to hold DAT0
     * low far longer than that while it programs, hundreds of milliseconds in
     * the worst case.  The spec's own maximum is what belongs here; the reset
     * default is not a considered value, it is the absence of one.
     *
     * THIS BUG WAS LATENT FOR THREE RUNS.  A warm card answered inside 8 ms
     * every time and the bench was bit-exact at 35.8 MB/s.  The first write
     * after a POWER CYCLE took longer -- a cold flash translation layer has
     * work to do -- and the controller cut the transfer off at 8545 us with
     * DATA-TIMEOUT and a knock-on Auto-CMD12 error.  An intermittent fault
     * that depends on how recently the board was powered is the worst kind to
     * leave in a driver whose whole job is staging megabytes reliably.
     */
    SDIO0->CLOCKCTRL_b.TIMEOUTCNT = 0xEu;   /* 2^27 TMCLK -- the maximum     */
    __DSB();

    trace("pads");
    emmc_pads_config();

    /* VOLTAGE BEFORE POWER, and both are required.  A host controller with
     * SDBUSPOWER set but VOLTSELECT unprogrammed does not drive the bus, so
     * commands are accepted and never complete -- which is exactly how CMD0
     * presented.  The eMMC's VCCQ on this board is 1.8 V. */
    trace("bus-power");
    SDIO0->HOSTCTRL1_b.VOLTSELECT = SDIO0_HOSTCTRL1_VOLTSELECT_1_8V;
    __DSB();
    SDIO0->HOSTCTRL1_b.SDBUSPOWER = SDIO0_HOSTCTRL1_SDBUSPOWER_POWERON;
    __DSB();
    tiku_cpu_ambiq_delay_us(2000u);

    /* Identification runs at 400 kHz on a 1-bit bus.  Both are mandatory --
     * the card will not answer CMD1 outside them. */
    trace("clock-400k");
    rc = emmc_set_clock(400000u);
    if (rc != TIKU_EMMC_OK) { return rc; }
    SDIO0->HOSTCTRL1_b.XFERWIDTH = 0u;
    SDIO0->HOSTCTRL1_b.DATATRANSFERWIDTH = 0u;
    s_bus_width = 1u;
    __DSB();

    trace("card-reset");
    emmc_card_reset();
    s_up = 1u;

    trace("cmd0-idle");
    rc = emmc_cmd(MMC_GO_IDLE, 0u, RESP_NONE, 0, 0, 0, (uint32_t *)0);
    if (rc != TIKU_EMMC_OK) { s_up = 0u; return rc; }
    tiku_cpu_ambiq_delay_us(2000u);

    /* CMD1 is a POLL, not a command: the card reports busy until its
     * internal init finishes, and the spec allows up to a second. */
    trace("cmd1-opcond");
    {
        uint32_t tries = 1000u;
        for (;;) {
            rc = emmc_cmd(MMC_SEND_OP_COND, MMC_OCR_SECTOR_MODE,
                          RESP_48, 0, 0, 0, resp);   /* R3: no CRC, no index */
            if (rc != TIKU_EMMC_OK) { s_up = 0u; return rc; }
            if ((resp[0] & MMC_OCR_BUSY) != 0u) { break; }
            if (--tries == 0u) { s_up = 0u; return TIKU_EMMC_ERR_TIMEOUT; }
            tiku_cpu_ambiq_delay_us(1000u);
            tiku_hang_checkin();
        }
    }

    trace("cmd2-cid");
    rc = emmc_cmd(MMC_ALL_SEND_CID, 0u, RESP_136, 1, 0, 0, resp);
    if (rc != TIKU_EMMC_OK) { s_up = 0u; return rc; }
    {
        /* A 136-bit response arrives with the CRC byte shifted out, so the
         * CID's fields sit 8 bits low across RESPONSE3..0.  Getting this
         * wrong prints a plausible-looking product name, which is why the
         * gate checks the decoded values rather than merely that bits
         * arrived. */
        uint8_t cid[16];
        int i;
        for (i = 0; i < 4; i++) {
            cid[i * 4 + 0] = (uint8_t)(resp[3 - i] >> 24);
            cid[i * 4 + 1] = (uint8_t)(resp[3 - i] >> 16);
            cid[i * 4 + 2] = (uint8_t)(resp[3 - i] >> 8);
            cid[i * 4 + 3] = (uint8_t)(resp[3 - i]);
        }
        s_id.mfr_id  = cid[1];
        s_id.oem_id  = (uint16_t)cid[2];
        for (i = 0; i < 6; i++) { s_id.product[i] = (char)cid[3 + i]; }
        s_id.product[6] = '\0';
        s_id.rev     = cid[9];
        s_id.serial  = ((uint32_t)cid[10] << 24) | ((uint32_t)cid[11] << 16) |
                       ((uint32_t)cid[12] << 8)  |  (uint32_t)cid[13];
        /* The year's BASE depends on EXT_CSD_REV, which is not read until
         * several rungs later -- so keep the raw nibble now and resolve it
         * at the end.  (Decoding it as 1997+n unconditionally printed "made
         * 11/2001" for a card whose EXT_CSD revision did not exist until
         * years after that: a plausible-looking field that is obviously
         * wrong once you look at it, which is the kind this port does not
         * ship.) */
        s_id.mfg_month = (uint8_t)(cid[14] & 0x0Fu);
        s_id.mfg_year  = (uint16_t)((cid[14] >> 4) & 0x0Fu);   /* raw */
    }

    trace("cmd3-rca");
    s_rca = 1u;
    rc = emmc_cmd(MMC_SET_RELATIVE_ADDR, s_rca << 16, RESP_48, 1, 1, 0, resp);
    if (rc != TIKU_EMMC_OK) { s_up = 0u; return rc; }
    s_id.rca = s_rca;

    trace("cmd9-csd");
    rc = emmc_cmd(MMC_SEND_CSD, s_rca << 16, RESP_136, 1, 0, 0, resp);
    if (rc == TIKU_EMMC_OK) {
        s_id.spec_vers = (uint8_t)((resp[3] >> 18) & 0x0Fu);
    }

    trace("cmd7-select");
    rc = emmc_cmd(MMC_SELECT_CARD, s_rca << 16, RESP_48BUSY, 1, 1, 0, resp);
    if (rc != TIKU_EMMC_OK) { s_up = 0u; return rc; }

    trace("cmd16-blocklen");
    (void)emmc_cmd(MMC_SET_BLOCKLEN, TIKU_EMMC_BLOCK_SIZE,
                   RESP_48, 1, 1, 0, resp);

    /* EXT_CSD is a 512-byte data phase and carries the real capacity: a
     * card above 2 GB reports 0xFFFFFFFF-ish nonsense in the CSD and the
     * truth only in EXT_CSD[215:212]. */
    trace("cmd8-extcsd");
    rc = emmc_read_ext_csd(s_ext);
    if (rc == TIKU_EMMC_OK) {
        s_sec_count = (uint32_t)s_ext[EXT_CSD_SEC_COUNT] |
                      ((uint32_t)s_ext[EXT_CSD_SEC_COUNT + 1] << 8) |
                      ((uint32_t)s_ext[EXT_CSD_SEC_COUNT + 2] << 16) |
                      ((uint32_t)s_ext[EXT_CSD_SEC_COUNT + 3] << 24);
        s_id.ext_csd_rev = s_ext[EXT_CSD_REV];
        s_devtype        = s_ext[EXT_CSD_DEVICE_TYPE];
        s_sa_timeout     = s_ext[EXT_CSD_S_A_TIMEOUT];
    }
    /* Resolve the manufacture year now that EXT_CSD_REV is known: the MMC
     * spec moved the epoch from 1997 to 2013 at EXT_CSD_REV >= 4. */
    s_id.mfg_year = (uint16_t)((s_id.ext_csd_rev >= 4u ? 2013u : 1997u)
                               + s_id.mfg_year);

    /* TRANSFER-READY.  Everything above is the ceremony the MMC spec makes
     * mandatory; everything below is optional speed.  Splitting the clock
     * here is what lets E5 price "wake the card" separately from "make the
     * card fast", which are different decisions with different answers. */
    s_ladder_us = cyc_to_us(cyc_now() - t_start);
    if (rc != TIKU_EMMC_OK) { s_init_us = s_ladder_us; s_up = 0u; return rc; }

    /*-------------------------------------------------------------------*/
    /* TABLE 4: the upgrade, and the card's own account of whether it took */
    /*-------------------------------------------------------------------*/
    if (width > 1u || hz > 400000u) {
        trace("cmd6-width");
        rc = emmc_set_bus_width(width);
        if (rc == TIKU_EMMC_OK) {
            trace("cmd6-hs");
            rc = emmc_set_high_speed(hz);
        }
        /* THE GATE.  Re-reading EXT_CSD is itself a data-phase transfer at
         * the new width and the new clock, so a bus that cannot carry data
         * fails by being unable to produce the evidence; and a card that
         * quietly declined the switch is caught saying so in its own
         * register file.  A pattern test alone would happily pass a host and
         * a card that had BOTH stayed slow. */
        if (rc == TIKU_EMMC_OK) {
            trace("verify-extcsd");
            rc = emmc_read_ext_csd(s_ext);
        }
        if (rc == TIKU_EMMC_OK) {
            uint8_t want_w = (width == 8u) ? 2u : (width == 4u ? 1u : 0u);
            s_id.ext_bus_width = s_ext[EXT_CSD_BUS_WIDTH];
            s_id.ext_hs_timing = s_ext[EXT_CSD_HS_TIMING];
            if (s_id.ext_bus_width != want_w) { rc = TIKU_EMMC_ERR_STATE; }
        }
        if (rc != TIKU_EMMC_OK) {
            /* Slow and proven beats fast and half-configured.  Report the
             * failure, but leave a WORKING device behind. */
            trace("fallback-slow");
            emmc_fallback_slow();
            (void)emmc_read_ext_csd(s_ext);
            s_id.ext_bus_width = s_ext[EXT_CSD_BUS_WIDTH];
            s_id.ext_hs_timing = s_ext[EXT_CSD_HS_TIMING];
        }
    } else {
        s_id.ext_bus_width = s_ext[EXT_CSD_BUS_WIDTH];
        s_id.ext_hs_timing = s_ext[EXT_CSD_HS_TIMING];
    }

    s_init_us      = cyc_to_us(cyc_now() - t_start);
    s_id.sec_count = s_sec_count;
    s_id.bus_width = s_bus_width;
    s_id.clock_hz  = s_clock_hz;
    s_id.device_type = s_devtype;
    return rc;
}

void tiku_emmc_init_time(uint32_t *ladder_us, uint32_t *total_us)
{
    if (ladder_us) { *ladder_us = s_ladder_us; }
    if (total_us)  { *total_us  = s_init_us;   }
}

/*---------------------------------------------------------------------------*/
/* E4 -- LIFECYCLE: the rung between "up" and "gone"                         */
/*---------------------------------------------------------------------------*/
/*
 * E3 measured what the alternative costs: a full bring-up is 49 ms of
 * mandatory identification plus 3 ms of upgrade.  Fifty milliseconds is far
 * too much to pay per access and cheap enough to pay per wake, which is
 * exactly the gap a sleep rung exists to fill -- the same argument the
 * PSRAM's half-sleep won on, with a number instead of an intuition.
 *
 * CMD5 IS NOT ISSUED FROM WHERE THE CARD USUALLY LIVES.  Sleep is only
 * accepted in STANDBY, and normal operation leaves the card in TRANSFER, so
 * the sequence is deselect (CMD7 with RCA 0) -> CMD5 sleep.  Waking is the
 * mirror: CMD5 awake -> CMD7 select.  Getting this wrong does not produce an
 * error; it produces a card that ignores the command and stays awake, which
 * is why the state is tracked and every access path checks it.
 *
 * The busy wait after CMD5 is on DAT0, NOT on CMD13: a sleeping card does
 * not answer SEND_STATUS, so polling it would time out on a card that had
 * done exactly what it was asked.
 */

/** @brief Bound the CMD5 busy wait from EXT_CSD[217]: 100 ns * 2^n. */
static uint32_t emmc_sa_timeout_us(void)
{
    uint32_t us = 1u;
    uint8_t  n  = s_sa_timeout;
    /* 100 ns * 2^n, in microseconds, clamped: the field is 8 bits and a
     * pathological value would otherwise produce an unbounded wait. */
    if (n > 23u) { n = 23u; }
    us = (1u << n) / 10u;
    if (us < 1000u) { us = 1000u; }      /* never wait less than a ms       */
    return us;
}

/** @brief Wait for the card to release DAT0 after an R1b that ends in sleep. */
static tiku_emmc_err_t emmc_wait_dat0(uint32_t budget_us)
{
    uint32_t spins;
    uint32_t t0 = cyc_now();
    uint32_t budget = (uint32_t)(((uint64_t)budget_us *
                                  tiku_cpu_ambiq_clock_get_hz()) / 1000000u);

    for (spins = 0u; ; spins++) {
        if ((SDIO0->PRESENT & SDIO0_PRESENT_CMDINHDAT_Msk) == 0u) {
            return TIKU_EMMC_OK;
        }
        poll_backoff(spins);
        tiku_hang_checkin();
        if ((cyc_now() - t0) > budget ||
            spins > EMMC_SPIN_CEILING) { return TIKU_EMMC_ERR_TIMEOUT; }
    }
}

tiku_emmc_err_t tiku_emmc_sleep(void)
{
    uint32_t resp[4];
    uint32_t t0;
    tiku_emmc_err_t rc;

    s_op_us = 0u;   /* a refused op has no duration; never report a stale one */
    if (!s_up)    { return TIKU_EMMC_ERR_POWER; }
    if (s_asleep) { return TIKU_EMMC_OK; }
    cyc_enable();
    t0 = cyc_now();

    /* Finish anything the card is still programming before asking it to
     * sleep -- a card told to sleep mid-write has a legitimate reason to
     * refuse, and diagnosing that later is not worth the microseconds. */
    rc = emmc_wait_ready();
    if (rc != TIKU_EMMC_OK) { return rc; }

    trace("deselect");
    rc = emmc_cmd(MMC_SELECT_CARD, 0u, RESP_NONE, 0, 0, 0, resp);
    if (rc != TIKU_EMMC_OK) { return rc; }

    trace("cmd5-sleep");
    rc = emmc_cmd(MMC_SLEEP_AWAKE, (s_rca << 16) | (1u << 15),
                  RESP_48BUSY, 1, 1, 0, resp);
    if (rc != TIKU_EMMC_OK) { return rc; }
    rc = emmc_wait_dat0(emmc_sa_timeout_us());
    if (rc != TIKU_EMMC_OK) { return rc; }

    s_asleep = 1u;
    s_op_us  = cyc_to_us(cyc_now() - t0);
    return TIKU_EMMC_OK;
}

tiku_emmc_err_t tiku_emmc_wake(void)
{
    uint32_t resp[4];
    uint32_t t0;
    tiku_emmc_err_t rc;

    s_op_us = 0u;
    if (!s_up)     { return TIKU_EMMC_ERR_POWER; }
    if (!s_asleep) { return TIKU_EMMC_OK; }
    cyc_enable();
    t0 = cyc_now();

    trace("cmd5-awake");
    rc = emmc_cmd(MMC_SLEEP_AWAKE, s_rca << 16, RESP_48BUSY, 1, 1, 0, resp);
    if (rc != TIKU_EMMC_OK) { return rc; }
    rc = emmc_wait_dat0(emmc_sa_timeout_us());
    if (rc != TIKU_EMMC_OK) { return rc; }

    trace("reselect");
    rc = emmc_cmd(MMC_SELECT_CARD, s_rca << 16, RESP_48BUSY, 1, 1, 0, resp);
    if (rc != TIKU_EMMC_OK) { return rc; }

    s_asleep = 0u;
    /* Trust it only once it answers as itself again -- the PSRAM's wake
     * rule, and for the same reason: a device that came back wrong is worse
     * than one that did not come back. */
    rc = emmc_wait_ready();
    s_op_us = cyc_to_us(cyc_now() - t0);
    return rc;
}

int      tiku_emmc_asleep(void)          { return s_asleep ? 1 : 0; }
uint32_t tiku_emmc_last_op_us(void)      { return s_op_us; }
const tiku_emmc_id_t *tiku_emmc_id(void) { return &s_id; }
uint32_t tiku_emmc_capacity_blocks(void) { return s_sec_count; }
uint32_t tiku_emmc_clock_hz(void)        { return s_up ? s_clock_hz : 0u; }
unsigned tiku_emmc_bus_width(void)       { return s_up ? s_bus_width : 0u; }

void tiku_emmc_deinit(void)
{
    if (tiku_emmc_powered()) {
        SDIO0->CLOCKCTRL &= ~(SDIO0_CLOCKCTRL_CLKEN_Msk |
                              SDIO0_CLOCKCTRL_SDCLKEN_Msk);
        SDIO0->HOSTCTRL1_b.SDBUSPOWER = 0u;
    }
    PWRCTRL->DEVPWREN &= ~PWRCTRL_DEVPWREN_PWRENSDIO0_Msk;
    __DSB();
    s_up = 0u; s_clock_hz = 0u; s_asleep = 0u;
}

tiku_emmc_err_t tiku_emmc_read_id(tiku_emmc_id_t *out)
{
    if (!s_up) { return TIKU_EMMC_ERR_POWER; }
    if (out)   { *out = s_id; }
    /* The gate is the DECODED values, not the presence of bits: a capacity
     * that is zero, or absurd for an 8 GB part, means the 136-bit shift is
     * wrong even though every command "succeeded". */
    if (s_sec_count == 0u || s_sec_count > (64u * 1024u * 1024u)) {
        return TIKU_EMMC_ERR_ID;
    }
    return TIKU_EMMC_OK;
}

/*---------------------------------------------------------------------------*/
/* BLOCK I/O                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief One command covering up to 65535 blocks, DMA or PIO.
 *
 * One command per 512 bytes proves correctness and then becomes the bottleneck:
 * at 48 MHz on 8 bits the data phase is 11 us and the per-command ceremony is
 * the measurement.  Hence BLKCNT armed and Auto CMD12, so the HOST closes it.
 */
static tiku_emmc_err_t emmc_xfer_chunk(uint32_t lba, uint32_t n_blk,
                                       uint8_t *buf, int is_write, int use_dma)
{
    uint32_t resp[4], xfer, i;
    tiku_emmc_err_t rc;
    uint8_t cmd;

    SDIO0->BLOCK = ((n_blk << SDIO0_BLOCK_BLKCNT_Pos) & SDIO0_BLOCK_BLKCNT_Msk)
                 | ((uint32_t)EMMC_SDMA_BOUND << SDIO0_BLOCK_HOSTSDMABUFSZ_Pos)
                 | (TIKU_EMMC_BLOCK_SIZE & SDIO0_BLOCK_TRANSFERBLOCKSIZE_Msk);

    xfer = is_write ? 0u : SDIO0_TRANSFER_DXFERDIRSEL_Msk;
    if (n_blk > 1u) {
        xfer |= SDIO0_TRANSFER_BLKSEL_Msk | SDIO0_TRANSFER_BLKCNTEN_Msk
              | ((1u << SDIO0_TRANSFER_ACMDEN_Pos) & SDIO0_TRANSFER_ACMDEN_Msk);
        cmd = is_write ? MMC_WRITE_MULTIPLE : MMC_READ_MULTIPLE;
    } else {
        cmd = is_write ? MMC_WRITE_SINGLE : MMC_READ_SINGLE;
    }

    if (use_dma) {
        SDIO0->HOSTCTRL1_b.DMASELECT = SDIO0_HOSTCTRL1_DMASELECT_SDMA;
        SDIO0->SDMA = (uint32_t)buf;
        xfer |= SDIO0_TRANSFER_DMAEN_Msk;
        __DSB();
    }

    rc = emmc_cmd_x(cmd, lba, RESP_48, 1, 1, 1, xfer, resp);
    if (rc != TIKU_EMMC_OK) { return rc; }

    if (!use_dma) {
        for (i = 0u; i < n_blk; i++) {
            uint8_t *p = buf + (i * TIKU_EMMC_BLOCK_SIZE);
            rc = is_write ? emmc_write_buffer(p) : emmc_read_buffer(p);
            if (rc != TIKU_EMMC_OK) { return rc; }
            tiku_hang_checkin();
        }
    }
    return emmc_wait_xfer(emmc_xfer_budget_cyc(n_blk));
}

/**
 * @brief Block transfer: chunking, DMA selection and cache maintenance.
 *
 * SDMA takes a 32-bit system address and moves whole words, so an unaligned
 * buffer is not a slow case but a WRONG one; such buffers fall back to PIO,
 * which has no alignment requirement.
 *
 * @note Cache maintenance is not optional: clean before (so a dirty line cannot
 *       be written back OVER data the engine delivered) and invalidate after a
 *       read (so the CPU sees the memory the engine wrote).
 */
static tiku_emmc_err_t emmc_xfer(uint32_t lba, uint32_t n_blk, uint8_t *buf,
                                 int is_write)
{
    const int use_dma = (((uint32_t)buf & 3u) == 0u);
    const uint32_t max_chunk = emmc_max_chunk();
    tiku_emmc_err_t rc;

    while (n_blk != 0u) {
        uint32_t chunk = (n_blk > max_chunk) ? max_chunk : n_blk;
        uint32_t bytes = chunk * TIKU_EMMC_BLOCK_SIZE;

        if (use_dma) { tiku_cpu_dcache_clean(buf, bytes); }
        rc = emmc_xfer_chunk(lba, chunk, buf, is_write, use_dma);
        if (rc != TIKU_EMMC_OK) { return rc; }
        if (use_dma && !is_write) { tiku_cpu_dcache_invalidate(buf, bytes); }

        buf   += bytes;
        lba   += chunk;
        n_blk -= chunk;
        tiku_hang_checkin();
    }
    return TIKU_EMMC_OK;
}

tiku_emmc_err_t tiku_emmc_read_blocks(uint32_t lba, uint32_t n_blk, void *buf)
{
    if (!s_up)       { return TIKU_EMMC_ERR_POWER; }
    /* A sleeping card answers nothing; refuse rather than time out. */
    if (s_asleep)    { return TIKU_EMMC_ERR_STATE; }
    if (n_blk == 0u) { return TIKU_EMMC_ERR_ARG; }
    if (s_sec_count && (lba + n_blk) > s_sec_count) {
        return TIKU_EMMC_ERR_ARG;
    }
    return emmc_xfer(lba, n_blk, (uint8_t *)buf, 0);
}

tiku_emmc_err_t tiku_emmc_write_blocks(uint32_t lba, uint32_t n_blk,
                                       const void *buf, int force)
{
    tiku_emmc_err_t rc;

    if (!s_up)       { return TIKU_EMMC_ERR_POWER; }
    if (s_asleep)    { return TIKU_EMMC_ERR_STATE; }
    if (n_blk == 0u) { return TIKU_EMMC_ERR_ARG; }
    if (s_sec_count && (lba + n_blk) > s_sec_count) {
        return TIKU_EMMC_ERR_ARG;
    }
    /* Default-deny outside the scratch region.  The card arrived with
     * contents this driver did not write and cannot restore; an unattended
     * driver has no business touching them. */
    if (!force && lba < tiku_emmc_scratch_lba()) {
        return TIKU_EMMC_ERR_ARG;
    }

    /* The cast drops const because the shared transfer path is one function
     * for both directions; the write leg never writes through it. */
    rc = emmc_xfer(lba, n_blk, (uint8_t *)(uintptr_t)buf, 1);
    if (rc != TIKU_EMMC_OK) { return rc; }
    /* Do not report a write complete while the card is still programming:
     * the next caller would meet a busy device and read a stale block. */
    return emmc_wait_ready();
}

/*---------------------------------------------------------------------------*/
/* E3 BENCH                                                                  */
/*---------------------------------------------------------------------------*/
/*
 * DWT-timed, work-denominated, checksum-gated -- the psrambench pattern.
 * Every leg reports the bytes it moved and a verdict; a leg that cannot
 * prove its bytes were the RIGHT bytes reports FAIL rather than a bandwidth,
 * because a fast wrong answer is the only outcome worse than a slow one.
 *
 * WHAT EACH LEG IS FOR:
 *   seq-wr-*   staging a model onto the card from a host link
 *   seq-rd-*   the warehouse read: pulling a model back into the tier
 *   rand-rd    single-block latency -- the shape a paged weight table makes
 *   unalign    the PIO fallback, exercised through its REAL trigger
 *   dtcm       whether SDMA can reach tightly-coupled memory at all
 *
 * Every write addresses the scratch region and nowhere else, and each write
 * leg carries its OWN pattern seed so its verification read cannot be
 * satisfied by data a previous leg left behind.
 */

#define BENCH_BLOCKS  TIKU_EMMC_SCRATCH_BLOCKS               /* 1024 = 512 KB */
#define BENCH_BYTES   (BENCH_BLOCKS * TIKU_EMMC_BLOCK_SIZE)

/*
 * In SSRAM, not DTCM.  Half a megabyte is most of the 512 KB tightly-coupled
 * bank, and SSRAM is where this part's large DMA-touched buffers already
 * live.  The +4 is deliberate headroom for the unaligned leg below.
 */
static uint8_t s_bench_buf[BENCH_BYTES + 4u]
    __attribute__((section(".ssram"), aligned(32)));

/** A small DTCM buffer, existing only to answer the reachability question. */
static uint8_t s_dtcm_buf[4096] __attribute__((aligned(32)));

/** Pattern byte for scratch-region offset @p a under seed @p s. */
static inline uint8_t bench_pat(uint32_t a, uint32_t s)
{
    return (uint8_t)(a ^ (a >> 8) ^ (a >> 16) ^ (s * 0x9Du) ^ 0xC3u);
}

/**
 * @brief Report a leg -- and quote NO bandwidth when the leg failed.
 *
 * A rate printed beside FAIL is misleading: the numerator counts the whole span
 * while the clock runs only until the transfer breaks, which once produced
 * 0.304 MB/s on a wire that cannot carry 0.047.  A failed leg quotes no rate.
 */
static void bench_report(const char *leg, uint32_t bytes, uint32_t cyc,
                         int exact, tiku_emmc_err_t rc)
{
    static const char *const en[] = { "ok", "POWER", "CLOCK", "TIMEOUT",
                                      "CMD", "ID", "ARG", "STATE" };
    unsigned long hz = tiku_cpu_ambiq_clock_get_hz();
    unsigned long kbps;

    if (cyc == 0u || hz == 0u) { cyc = 1u; }
    if (!exact) {
        uint32_t e = s_last_err;
        /* rc AND intstat, because they answer different questions and the
         * pair is what identifies the culprit.  A TIMEOUT is OURS -- the
         * controller never raised anything, so intstat is legitimately zero
         * and reading that as "no error" is how half a session got spent
         * suspecting the card. */
        SHELL_PRINTF("  %-11s %6lu KB  %8lu us      -- MB/s  FAIL %s  "
                     "intstat %08lx%s%s%s%s%s%s\n", leg,
                     (unsigned long)(bytes / 1024u),
                     (unsigned long)cyc_to_us(cyc),
                     en[(unsigned)rc < 8u ? (unsigned)rc : 0u],
                     (unsigned long)e,
                     (e & (1u << 16)) ? " CMD-TIMEOUT" : "",
                     (e & (1u << 17)) ? " CMD-CRC"     : "",
                     (e & (1u << 19)) ? " CMD-INDEX"   : "",
                     (e & (1u << 20)) ? " DATA-TIMEOUT": "",
                     (e & (1u << 21)) ? " DATA-CRC"    : "",
                     (e & (1u << 24)) ? " AUTOCMD"     : "");
        return;
    }
    kbps = (unsigned long)(((uint64_t)bytes * hz) / ((uint64_t)cyc * 1000u));
    SHELL_PRINTF("  %-11s %6lu KB  %8lu us  %5lu.%03lu MB/s  bit-exact\n", leg,
                 (unsigned long)(bytes / 1024u),
                 (unsigned long)cyc_to_us(cyc), kbps / 1000u, kbps % 1000u);
}

/** Fill @p len bytes of the bench buffer with seed @p s. */
static void bench_fill(uint32_t s, uint32_t len)
{
    uint32_t i;
    for (i = 0u; i < len; i++) { s_bench_buf[i] = bench_pat(i, s); }
}

/** Compare @p len bytes at @p off against seed @p s.  1 = bit-exact. */
static int bench_check(uint32_t s, uint32_t off, uint32_t len)
{
    uint32_t i;
    for (i = 0u; i < len; i++) {
        if (s_bench_buf[off + i] != bench_pat(i, s)) { return 0; }
    }
    return 1;
}

void tiku_emmc_bench_run(void)
{
    static const uint32_t sizes[3] = { 8u, 128u, BENCH_BLOCKS };  /* blocks   */
    static const char *const wr_nm[3] = { "seq-wr-4k", "seq-wr-64k",
                                          "seq-wr-512k" };
    static const char *const rd_nm[3] = { "seq-rd-4k", "seq-rd-64k",
                                          "seq-rd-512k" };
    uint32_t base, t0, i, k, off, span_blk, span_bytes;
    uint32_t seed = 0u;   /* the pattern currently ON THE CARD  */
    tiku_emmc_err_t rc;

    if (!s_up) { SHELL_PRINTF("bench: emmc not up\n"); return; }
    base = tiku_emmc_scratch_lba();
    if (base == 0u) { SHELL_PRINTF("bench: no scratch region\n"); return; }

    /*
     * THE SPAN FOLLOWS THE WIRE.  At the identification setting the bus moves
     * about 50 KB/s, so the 512 KB span used at speed would be ten seconds per
     * leg and the better part of two minutes overall -- long enough that the
     * hang watchdog, not the card, would decide how the run ended.  When the
     * clock is still slow, drop to 64 KB and run only the transfer sizes that
     * fit inside it.  Bandwidth is work-denominated, so the two spans stay
     * directly comparable, which is the entire reason for being able to run at
     * the slow configuration at all: the upgrade gets PRICED against the same
     * code and the same card rather than merely asserted.
     */
    span_blk   = (s_clock_hz < 1000000u) ? 128u : BENCH_BLOCKS;
    span_bytes = span_blk * TIKU_EMMC_BLOCK_SIZE;

    cyc_enable();
    SHELL_PRINTF("emmcbench @ %u-bit, %lu Hz  (scratch LBA %lu, %lu KB span)\n",
                 s_bus_width, (unsigned long)s_clock_hz,
                 (unsigned long)base, (unsigned long)(span_bytes / 1024u));
    SHELL_PRINTF("  init: ladder %lu us (400 kHz 1-bit), total %lu us\n",
                 (unsigned long)s_ladder_us, (unsigned long)s_init_us);

    /* ---- sequential write, three transfer sizes ---------------------- */
    for (k = 0u; k < 3u; k++) {
        if (sizes[k] > span_blk) { continue; }
        seed = k + 1u;      /* leaves THIS leg's pattern on the card, which
                             * is what the read legs below then verify      */
        bench_fill(seed, span_bytes);
        t0 = cyc_now();
        for (i = 0u; i < span_blk; i += sizes[k]) {
            rc = tiku_emmc_write_blocks(base + i, sizes[k],
                     &s_bench_buf[i * TIKU_EMMC_BLOCK_SIZE], 0);
            if (rc != TIKU_EMMC_OK) { break; }
        }
        {
            uint32_t cyc = cyc_now() - t0;
            int ok = 0;
            if (rc == TIKU_EMMC_OK) {
                /* Verify by reading it back in one go (untimed): the seed
                 * differs per leg, so a pass cannot be produced by data an
                 * earlier leg happened to leave in place. */
                for (i = 0u; i < span_bytes; i++) { s_bench_buf[i] = 0u; }
                rc = tiku_emmc_read_blocks(base, span_blk, s_bench_buf);
                ok = (rc == TIKU_EMMC_OK) && bench_check(seed, 0u, span_bytes);
            }
            bench_report(wr_nm[k], span_bytes, cyc, ok, rc);
        }
    }

    /* ---- sequential read, same transfer sizes ------------------------ */
    /* seed is whatever the last write leg left on the card. */
    for (k = 0u; k < 3u; k++) {
        if (sizes[k] > span_blk) { continue; }
        for (i = 0u; i < span_bytes; i++) { s_bench_buf[i] = 0u; }
        tiku_cpu_dcache_clean(s_bench_buf, span_bytes);
        t0 = cyc_now();
        for (i = 0u; i < span_blk; i += sizes[k]) {
            rc = tiku_emmc_read_blocks(base + i, sizes[k],
                     &s_bench_buf[i * TIKU_EMMC_BLOCK_SIZE]);
            if (rc != TIKU_EMMC_OK) { break; }
        }
        {
            uint32_t cyc = cyc_now() - t0;
            bench_report(rd_nm[k], span_bytes, cyc,
                         (rc == TIKU_EMMC_OK) &&
                         bench_check(seed, 0u, span_bytes), rc);
        }
    }

    /* ---- single-block latency ---------------------------------------- */
    /*
     * Two figures, because they answer different questions and only one of
     * them can be checksummed.  Inside the scratch region the data is ours,
     * so the latency comes with a correctness verdict -- but over so small a
     * span the card's own translation layer may well be serving from cache.
     * Across the whole 8 GB the seeks are real, and the contents are opaque:
     * that leg reports TIME ONLY and says so rather than implying a gate it
     * cannot run.
     */
    {
        const uint32_t n = 256u;
        uint32_t lcg = 12345u, bad = 0u, acc = 0u, done = 0u;

        /*
         * THE TIMER BRACKETS THE READ AND NOTHING ELSE.  The first draft of
         * this leg left the 512-byte verification inside the timed region
         * and reported 270 us/blk -- against 147 us/blk for the leg that
         * seeks across the whole 8 GB.  Random access over half a megabyte
         * being SLOWER than random access over seven gigabytes is not a
         * result, it is a denominator with someone else's work in it: the
         * check walks a just-invalidated SSRAM buffer, and that cost about
         * 120 us a block.  Same lesson as the PSRAM leg that once claimed
         * 872 MB/s on a 384 MB/s wire -- when a number is impossible, the
         * measurement is wrong before the hardware is.
         */
        for (i = 0u; i < n; i++) {
            uint32_t t;
            lcg = lcg * 1664525u + 1013904223u;
            off = lcg % span_blk;
            t = cyc_now();
            rc = tiku_emmc_read_blocks(base + off, 1u, s_bench_buf);
            acc += cyc_now() - t;
            done++;
            if (rc != TIKU_EMMC_OK) { bad = 1u; break; }
            for (k = 0u; k < TIKU_EMMC_BLOCK_SIZE; k++) {
                if (s_bench_buf[k] !=
                    bench_pat(off * TIKU_EMMC_BLOCK_SIZE + k, seed)) {
                    bad = 1u; break;
                }
            }
            if (bad) { break; }
            tiku_hang_checkin();
        }
        {
            /* PER COMPLETED READ, not per intended read.  Dividing by n after
             * an early break understates the latency by exactly the fraction
             * of the loop that never ran -- the same unmoved-bytes-in-the-
             * denominator mistake the PSRAM bench made, and it turned an
             * aborted leg into a plausible 767 us/blk. */
            uint32_t d  = done ? done : 1u;
            uint32_t us = cyc_to_us(acc);
            SHELL_PRINTF("  %-11s %4lu/%lu x 512 B in scratch:"
                         " %lu.%02lu us/blk  %s\n", "rand-rd",
                         (unsigned long)done, (unsigned long)n,
                         (unsigned long)(us / d),
                         (unsigned long)((us % d) * 100u / d),
                         bad ? "FAIL" : "bit-exact");
        }

        lcg = 999u;
        done = 0u;
        t0 = cyc_now();
        for (i = 0u; i < n; i++) {
            lcg = lcg * 1664525u + 1013904223u;
            rc = tiku_emmc_read_blocks(lcg % (s_sec_count - 1u), 1u,
                                       s_bench_buf);
            if (rc != TIKU_EMMC_OK) { break; }
            done++;
            tiku_hang_checkin();
        }
        {
            uint32_t d  = done ? done : 1u;
            uint32_t us = cyc_to_us(cyc_now() - t0);
            SHELL_PRINTF("  %-11s %4lu/%lu x 512 B over %lu MB:"
                         " %lu.%02lu us/blk  time only (contents opaque)\n",
                         "rand-far", (unsigned long)done, (unsigned long)n,
                         (unsigned long)(s_sec_count / 2048u),
                         (unsigned long)(us / d),
                         (unsigned long)((us % d) * 100u / d));
        }
    }

    /* ---- the unaligned fallback, through its real trigger ------------- */
    /*
     * Not a synthetic switch: this hands the driver a buffer at offset 1,
     * which is exactly what makes emmc_xfer() choose PIO in production.  So
     * the leg proves the fallback both EXISTS and is correct, and its number
     * is the honest price of an unaligned caller.
     */
    {
        const uint32_t blks = (span_blk < 128u) ? span_blk : 128u;
        uint32_t bytes = blks * TIKU_EMMC_BLOCK_SIZE;
        int ok;
        for (i = 0u; i < bytes + 1u; i++) { s_bench_buf[i] = 0u; }
        t0 = cyc_now();
        rc = tiku_emmc_read_blocks(base, blks, &s_bench_buf[1]);
        {
            uint32_t cyc = cyc_now() - t0;
            ok = (rc == TIKU_EMMC_OK);
            for (i = 0u; ok && i < bytes; i++) {
                if (s_bench_buf[1 + i] != bench_pat(i, seed)) { ok = 0; }
            }
            bench_report("unalign-rd", bytes, cyc, ok, rc);
        }
    }

    /* ---- can SDMA reach tightly-coupled memory? ----------------------- */
    /*
     * Asked LAST, and asked rather than assumed.  MSPI's engine demonstrably
     * reaches DTCM on this part (the PSRAM bench streams into a .bss buffer
     * at 60 MB/s, checksum-gated), but SDIO is a different master and "the
     * other DMA works there" is precisely the kind of inference this port has
     * been punished for.  The answer decides whether E4's staging path may
     * use DTCM bounce buffers or must stay in SSRAM.
     */
    {
        const uint32_t blks = sizeof s_dtcm_buf / TIKU_EMMC_BLOCK_SIZE;
        int ok;
        for (i = 0u; i < sizeof s_dtcm_buf; i++) { s_dtcm_buf[i] = 0u; }
        rc = tiku_emmc_read_blocks(base, blks, s_dtcm_buf);
        ok = (rc == TIKU_EMMC_OK);
        for (i = 0u; ok && i < sizeof s_dtcm_buf; i++) {
            if (s_dtcm_buf[i] != bench_pat(i, seed)) { ok = 0; }
        }
        SHELL_PRINTF("  %-11s %lu KB into DTCM (%p): %s\n", "dtcm-rd",
                     (unsigned long)(sizeof s_dtcm_buf / 1024u),
                     (void *)s_dtcm_buf,
                     ok ? "reachable, bit-exact" : "NOT usable");
    }

    /* ---- negative gate: the CMD6 allow-list must be SEEN to refuse ---- */
    /*
     * A guard nobody has watched fail is a guard nobody has tested, and this
     * particular guard is the one standing between a debugging session and a
     * permanently repartitioned 8 GB card.  So it is exercised: EXT_CSD index
     * 179 is PARTITION_CONFIG, a real index, partly write-once, and exactly
     * the sort of thing a slip of the fingers would reach for.  Running this
     * is safe because the refusal happens BEFORE any register is touched --
     * the command is never issued.  What would not be safe is shipping the
     * allow-list having never once observed it say no.
     */
    {
        tiku_emmc_err_t deny = emmc_switch(179u, 0u);
        SHELL_PRINTF("  %-11s CMD6 -> index 179 (PARTITION_CONFIG): %s\n",
                     "allow-list",
                     (deny == TIKU_EMMC_ERR_ARG) ? "REFUSED (correct)"
                                                 : "ISSUED -- BUG");
    }

    SHELL_PRINTF("  not tested: HS200/HS400, DDR, ADMA2, CMD23 set-block-count,"
                 " cache/reliable-write\n");
}

/*---------------------------------------------------------------------------*/
/* E4 -- THE WAREHOUSE: stage bulk data from the card into the PSRAM tier    */
/*---------------------------------------------------------------------------*/
#if (TIKU_DRV_PSRAM_ENABLE + 0)

#include "tiku_psram_arch.h"

/*
 * The flow this whole part exists for: a model lives on 8 GB of eMMC, and
 * the one being run is pulled into the 64 MB working tier on demand.  Two
 * engines and a bounce buffer -- SDIO's DMA fills SSRAM from the card, the
 * MSPI command queue drains SSRAM into the PSRAM, and the CPU touches the
 * bytes only to check them.
 *
 * WHY NO PATTERN IS WRITTEN FIRST.  The obvious gate would be to write a
 * known pattern and look for it at the far end, but that caps the demo at
 * the 512 KB scratch region and puts writes on a card whose contents are
 * not ours.  Instead the SOURCE checksum is accumulated from the bounce
 * buffer as it passes -- i.e. from the read path E3 proved bit-exact -- and
 * compared against a checksum of what comes back out of the PSRAM.  That
 * verifies the staging path against a known-good reference, needs no writes
 * at all, and works at any size the tier can hold.
 *
 * THE CHECKSUM IS NOT IN THE TRANSFER CLOCK.  Hashing 54 MB costs real time,
 * and folding it into the staging measurement would understate the pipeline
 * exactly the way E3's first random-read leg understated latency.  Transfer
 * time and verification time are accumulated separately and both reported.
 */

#define STAGE_CHUNK   BENCH_BYTES        /* 512 KB: one eMMC command's worth */
#define STAGE_SEG     65536u             /* CQ segment size on the PSRAM side */

/** @brief FNV-1a over whole words -- cheap enough not to distort the clock. */
static uint32_t stage_hash(const uint8_t *p, uint32_t n, uint32_t h)
{
    const uint32_t *w = (const uint32_t *)(const void *)p;
    uint32_t i;
    for (i = 0u; i < (n / 4u); i++) {
        h = (h ^ w[i]) * 16777619u;
    }
    return h;
}

/*---------------------------------------------------------------------------*/
/* F4 -- staging driven by a FILE'S EXTENTS rather than one LBA range         */
/*---------------------------------------------------------------------------*/
/*
 * E4's stage moves one contiguous span because it was handed a raw LBA.  A
 * file is not obliged to be contiguous, so the FAT layer drives this one run
 * at a time and the pipeline below neither knows nor cares how many runs
 * there were -- fragmentation costs shorter chunks, never correctness.
 *
 * The state lives here because the bounce buffer and the PSRAM coupling do.
 */
static uint32_t s_stg_off, s_stg_src, s_stg_rd, s_stg_wr;
static int      s_stg_xip;

void tiku_emmc_stage_open(void)
{
    cyc_enable();
    s_stg_off = 0u;
    s_stg_src = 2166136261u;
    s_stg_rd  = 0u;
    s_stg_wr  = 0u;
    s_stg_xip = tiku_psram_xip_enabled();
    if (s_stg_xip) { (void)tiku_psram_xip_enable(0); }
}

tiku_emmc_err_t tiku_emmc_stage_chunk(uint32_t lba, uint32_t nsec)
{
    uint32_t left = nsec;

    while (left != 0u) {
        uint32_t n = (left > (STAGE_CHUNK / TIKU_EMMC_BLOCK_SIZE))
                     ? (STAGE_CHUNK / TIKU_EMMC_BLOCK_SIZE) : left;
        uint32_t bytes = n * TIKU_EMMC_BLOCK_SIZE;
        uint32_t t0;
        tiku_emmc_err_t rc;

        t0 = cyc_now();
        rc = tiku_emmc_read_blocks(lba, n, s_bench_buf);
        s_stg_rd += cyc_now() - t0;
        if (rc != TIKU_EMMC_OK) { return rc; }

        s_stg_src = stage_hash(s_bench_buf, bytes, s_stg_src);
        tiku_cpu_dcache_clean(s_bench_buf, bytes);

        t0 = cyc_now();
        if (tiku_psram_cq_xfer(s_stg_off, s_bench_buf, bytes,
                               STAGE_SEG, 1) != 0) {
            s_stg_wr += cyc_now() - t0;
            return TIKU_EMMC_ERR_CMD;
        }
        s_stg_wr += cyc_now() - t0;

        s_stg_off += bytes;
        lba       += n;
        left      -= n;
        tiku_hang_checkin();
    }
    return TIKU_EMMC_OK;
}

tiku_emmc_err_t tiku_emmc_stage_close(uint32_t total_bytes, uint32_t *src,
                                      uint32_t *dst, uint32_t *rd_us,
                                      uint32_t *wr_us)
{
    uint32_t h = 2166136261u, off;
    tiku_emmc_err_t rc = TIKU_EMMC_OK;

    /* Read the staged image back OUT of the PSRAM and hash that.  Hashing
     * the bounce buffer on the way in would only prove the card was read;
     * this proves the bytes are where the tier will look for them. */
    for (off = 0u; off < total_bytes; off += STAGE_CHUNK) {
        uint32_t n = ((total_bytes - off) < STAGE_CHUNK)
                     ? (total_bytes - off) : STAGE_CHUNK;
        if (tiku_psram_cq_xfer(off, s_bench_buf, n, STAGE_SEG, 0) != 0) {
            rc = TIKU_EMMC_ERR_CMD;
            break;
        }
        tiku_cpu_dcache_invalidate(s_bench_buf, n);
        h = stage_hash(s_bench_buf, n, h);
        tiku_hang_checkin();
    }
    if (s_stg_xip) { (void)tiku_psram_xip_enable(1); }
    if (src)   { *src   = s_stg_src; }
    if (dst)   { *dst   = h; }
    if (rd_us) { *rd_us = cyc_to_us(s_stg_rd); }
    if (wr_us) { *wr_us = cyc_to_us(s_stg_wr); }
    return rc;
}

void tiku_emmc_stage_run(uint32_t mb, uint32_t src_lba)
{
    uint32_t total, off, t0, t_rd = 0u, t_wr = 0u, t_vfy = 0u;
    uint32_t h_src = 2166136261u, h_dst = 2166136261u;
    unsigned long hz = tiku_cpu_ambiq_clock_get_hz();
    int xip_was;
    tiku_emmc_err_t rc = TIKU_EMMC_OK;

    if (!s_up || s_asleep) { SHELL_PRINTF("stage: emmc not ready\n"); return; }
    if (!tiku_psram_powered() || tiku_psram_asleep()) {
        SHELL_PRINTF("stage: psram not up (run `power psram up` first)\n");
        return;
    }
    if (mb == 0u) { mb = 1u; }
    if (mb > (TIKU_PSRAM_SIZE_BYTES / (1024u * 1024u))) {
        mb = TIKU_PSRAM_SIZE_BYTES / (1024u * 1024u);
    }
    total = mb * 1024u * 1024u;
    if (s_sec_count && ((src_lba + (total / TIKU_EMMC_BLOCK_SIZE)) >
                        s_sec_count)) {
        SHELL_PRINTF("stage: source range past end of card\n");
        return;
    }

    cyc_enable();
    /* The command queue moves bytes the CPU cannot see: XIP has to come down
     * for the transfer and go back up afterwards, because the tier's whole
     * value is that the staged model is addressable when this returns. */
    xip_was = tiku_psram_xip_enabled();
    if (xip_was) { (void)tiku_psram_xip_enable(0); }

    SHELL_PRINTF("stage: %lu MB  emmc LBA %lu -> psram 0  (%lu KB chunks)\n",
                 (unsigned long)mb, (unsigned long)src_lba,
                 (unsigned long)(STAGE_CHUNK / 1024u));

    for (off = 0u; off < total; off += STAGE_CHUNK) {
        uint32_t n = ((total - off) < STAGE_CHUNK) ? (total - off)
                                                   : STAGE_CHUNK;
        t0 = cyc_now();
        rc = tiku_emmc_read_blocks(src_lba + (off / TIKU_EMMC_BLOCK_SIZE),
                                   n / TIKU_EMMC_BLOCK_SIZE, s_bench_buf);
        t_rd += cyc_now() - t0;
        if (rc != TIKU_EMMC_OK) { break; }

        t0 = cyc_now();
        h_src = stage_hash(s_bench_buf, n, h_src);
        t_vfy += cyc_now() - t0;

        tiku_cpu_dcache_clean(s_bench_buf, n);
        t0 = cyc_now();
        if (tiku_psram_cq_xfer(off, s_bench_buf, n, STAGE_SEG, 1) != 0) {
            SHELL_PRINTF("stage: psram write failed at offset %lu\n",
                         (unsigned long)off);
            rc = TIKU_EMMC_ERR_CMD;
            t_wr += cyc_now() - t0;
            break;
        }
        t_wr += cyc_now() - t0;
        tiku_hang_checkin();
    }

    /* Read it back out of the PSRAM and hash that.  Deliberately the same
     * bounce buffer: if the staging had merely left the buffer's last chunk
     * lying around, this would hash it and disagree with the source. */
    if (rc == TIKU_EMMC_OK) {
        for (off = 0u; off < total; off += STAGE_CHUNK) {
            uint32_t n = ((total - off) < STAGE_CHUNK) ? (total - off)
                                                       : STAGE_CHUNK;
            if (tiku_psram_cq_xfer(off, s_bench_buf, n, STAGE_SEG, 0) != 0) {
                SHELL_PRINTF("stage: psram readback failed at %lu\n",
                             (unsigned long)off);
                rc = TIKU_EMMC_ERR_CMD;
                break;
            }
            tiku_cpu_dcache_invalidate(s_bench_buf, n);
            t0 = cyc_now();
            h_dst = stage_hash(s_bench_buf, n, h_dst);
            t_vfy += cyc_now() - t0;
            tiku_hang_checkin();
        }
    }

    if (xip_was) { (void)tiku_psram_xip_enable(1); }

    if (rc != TIKU_EMMC_OK) {
        SHELL_PRINTF("stage: FAILED rc=%d  intstat %08lx\n", (int)rc,
                     (unsigned long)s_last_err);
        return;
    }

    {
        uint32_t t_all = t_rd + t_wr;
        unsigned long rd_kbps = (unsigned long)(((uint64_t)total * hz) /
                                                ((uint64_t)(t_rd ? t_rd : 1u) * 1000u));
        unsigned long wr_kbps = (unsigned long)(((uint64_t)total * hz) /
                                                ((uint64_t)(t_wr ? t_wr : 1u) * 1000u));
        unsigned long al_kbps = (unsigned long)(((uint64_t)total * hz) /
                                                ((uint64_t)(t_all ? t_all : 1u) * 1000u));
        SHELL_PRINTF("  emmc->sram  %8lu us  %5lu.%03lu MB/s\n",
                     (unsigned long)cyc_to_us(t_rd),
                     rd_kbps / 1000u, rd_kbps % 1000u);
        SHELL_PRINTF("  sram->psram %8lu us  %5lu.%03lu MB/s\n",
                     (unsigned long)cyc_to_us(t_wr),
                     wr_kbps / 1000u, wr_kbps % 1000u);
        SHELL_PRINTF("  end-to-end  %8lu us  %5lu.%03lu MB/s  (%lu MB staged)\n",
                     (unsigned long)cyc_to_us(t_all),
                     al_kbps / 1000u, al_kbps % 1000u, (unsigned long)mb);
        SHELL_PRINTF("  verify      %8lu us  (NOT in the rates above)\n",
                     (unsigned long)cyc_to_us(t_vfy));
        SHELL_PRINTF("  checksum src %08lx dst %08lx -- %s\n",
                     (unsigned long)h_src, (unsigned long)h_dst,
                     (h_src == h_dst) ? "bit-exact" : "MISMATCH");
        SHELL_PRINTF("  xip %s; staged image is at psram offset 0\n",
                     xip_was ? "restored" : "was off, left off");
    }
}

#endif /* TIKU_DRV_PSRAM_ENABLE */

/*---------------------------------------------------------------------------*/
/* DIAGNOSTICS                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Untangle a read failure by varying ONE thing at a time.
 *
 * Block count, buffer location and DMA-vs-PIO are entangled in every bench leg,
 * so the bench says THAT a transfer was wrong and never WHICH variable did it.
 * This walks the matrix against a known-good single-block write/read into DTCM.
 */
void tiku_emmc_diag_run(void)
{
    static uint8_t ref[TIKU_EMMC_BLOCK_SIZE * 4u] __attribute__((aligned(32)));
    uint32_t base, i, t0;
    tiku_emmc_err_t rc;

    if (!s_up) { SHELL_PRINTF("diag: emmc not up\n"); return; }
    base = tiku_emmc_scratch_lba();
    if (base == 0u) { SHELL_PRINTF("diag: no scratch region\n"); return; }

    SHELL_PRINTF("emmcdiag @ %u-bit, %lu Hz  (LBA %lu)\n",
                 s_bus_width, (unsigned long)s_clock_hz, (unsigned long)base);
    /*
     * TIMEOUTCNT (CLOCKCTRL[19:16]) sets the data timeout as 2^(13+n) ticks
     * of TMCLK, whose frequency CAPABILITIES0[7:0] reports.  Printed because
     * it is the first suspect whenever a transfer works fast and fails slow:
     * a fixed timeout is a shrinking budget as the wire slows down.
     */
    SHELL_PRINTF("  timeoutcnt %lu  clockctrl %08lx  capabilities0 %08lx\n",
                 (unsigned long)SDIO0->CLOCKCTRL_b.TIMEOUTCNT,
                 (unsigned long)SDIO0->CLOCKCTRL,
                 (unsigned long)SDIO0->CAPABILITIES0);

    /* Lay down four known blocks ONE AT A TIME -- the path the gate proves. */
    for (i = 0u; i < sizeof ref; i++) { ref[i] = bench_pat(i, 42u); }
    for (i = 0u; i < 4u; i++) {
        rc = tiku_emmc_write_blocks(base + i, 1u,
                                    &ref[i * TIKU_EMMC_BLOCK_SIZE], 0);
        if (rc != TIKU_EMMC_OK) {
            SHELL_PRINTF("  setup write blk %lu FAILED rc=%d\n",
                         (unsigned long)i, (int)rc);
            return;
        }
    }
    SHELL_PRINTF("  setup: 4 x single-block write ok\n");

    /*
     * Each case names its three variables and its verdict.  The time column is
     * in microseconds; compare it against the wire: at 1 bit and 375 kHz a
     * 512 B block cannot cross in less than 10900 microseconds, so a faster
     * case is reporting success for data that never arrived.
     */
    {
        struct { const char *name; uint8_t *buf; uint32_t nblk; } cases[] = {
            { "1blk ssram ", &s_bench_buf[0],  1u },
            { "1blk dtcm  ", &s_dtcm_buf[0],   1u },
            { "1blk unalgn", &s_bench_buf[1],  1u },
            { "4blk ssram ", &s_bench_buf[0],  4u },
            { "4blk dtcm  ", &s_dtcm_buf[0],   4u },
            { "4blk unalgn", &s_bench_buf[1],  4u },
        };
        unsigned c;
        for (c = 0u; c < sizeof cases / sizeof cases[0]; c++) {
            uint32_t bytes = cases[c].nblk * TIKU_EMMC_BLOCK_SIZE;
            uint32_t firstbad = 0xFFFFFFFFu;
            for (i = 0u; i < bytes; i++) { cases[c].buf[i] = 0xA5u; }
            tiku_cpu_dcache_clean(cases[c].buf, bytes);
            t0 = cyc_now();
            rc = tiku_emmc_read_blocks(base, cases[c].nblk, cases[c].buf);
            t0 = cyc_now() - t0;
            for (i = 0u; i < bytes; i++) {
                if (cases[c].buf[i] != ref[i]) { firstbad = i; break; }
            }
            SHELL_PRINTF("  %s rc=%d %7lu us  %s", cases[c].name, (int)rc,
                         (unsigned long)cyc_to_us(t0),
                         (firstbad == 0xFFFFFFFFu) ? "bit-exact\n"
                                                   : "MISMATCH");
            if (firstbad != 0xFFFFFFFFu) {
                SHELL_PRINTF(" at byte %lu (got %02x want %02x)\n",
                             (unsigned long)firstbad,
                             cases[c].buf[firstbad], ref[firstbad]);
            }
        }
    }
}

uint32_t tiku_emmc_last_error(void) { return s_last_err; }

void tiku_emmc_regs(uint32_t *out, unsigned n)
{
    unsigned i;

    /* POWER-SAFE.  Reading an unpowered peripheral stalls the APB and hangs
     * the CPU with no fault -- it cost a board wedge on the NOR driver, and
     * that lesson is applied here by construction rather than after. */
    for (i = 0u; i < n; i++) { out[i] = 0xDEADDEADu; }
    if (n > 0u) { out[0] = PWRCTRL->DEVPWRSTATUS; }
    if (!tiku_emmc_powered()) { return; }
    if (n > 1u) { out[1] = SDIO0->PRESENT; }
    if (n > 2u) { out[2] = SDIO0->CLOCKCTRL; }
    if (n > 3u) { out[3] = SDIO0->HOSTCTRL1; }
    if (n > 4u) { out[4] = SDIO0->INTSTAT; }
    if (n > 5u) { out[5] = SDIO0->CAPABILITIES0; }
    if (n > 6u) { out[6] = SDIO0->RESPONSE0; }
    if (n > 7u) { out[7] = SDIO0->TRANSFER; }
}

#endif /* PLATFORM_AMBIQ && TIKU_DRV_EMMC_ENABLE */
