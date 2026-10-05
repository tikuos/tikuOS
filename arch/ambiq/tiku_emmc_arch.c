/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_emmc_arch.c - Apollo510 SDIO0 host and IS21EF08G eMMC driver.
 *
 * Bring-up, block I/O, sleep and wake, HS200, a bench and PSRAM staging.  Init
 * stops at the first failed step of the walk idle -> identify -> standby ->
 * transfer: a command sent in the wrong state fails like a wiring fault.
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
#include <kernel/memory/tiku_mem.h>      /* operation-scoped bounce workspace */
#include <string.h>

#include <kernel/shell/tiku_shell_io.h>  /* SHELL_PRINTF for the bench */

/*---------------------------------------------------------------------------*/
/* PADS                                                                      */
/*---------------------------------------------------------------------------*/

/*
 * The pads and their FNCSEL values come from the board header
 * (TIKU_BOARD_EMMC_*); RSTn is GP13 on the Apollo510 Blue EVB and GP12 on
 * the Apollo510 EVB.
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
 * DAT0-3 + CLK; D4..CMD covers DAT4-7 + CMD), configuring every pad between
 * the ends of each run, so these asserts reject a board whose runs are not
 * contiguous. */
_Static_assert(EMMC_PAD_CLK - EMMC_PAD_D0 == 4,
               "eMMC low run must be DAT0..DAT3,CLK contiguous");
_Static_assert(EMMC_PAD_D3 - EMMC_PAD_D0 == 3,
               "eMMC DAT0..DAT3 must be contiguous");
_Static_assert(EMMC_PAD_CMD - EMMC_PAD_D4 == 4,
               "eMMC high run must be DAT4..DAT7,CMD contiguous");
_Static_assert(EMMC_PAD_D7 - EMMC_PAD_D4 == 3,
               "eMMC DAT4..DAT7 must be contiguous");

/*
 * Function select differs across this bus (am_hal_pin.h):
 *
 *   GP84..GP88  (DAT0-3, CLK)  ->  FNCSEL 2   (AM_HAL_PIN_84_SDIF0_DAT0 = 2)
 *   GP156..GP160 (DAT4-7, CMD) ->  FNCSEL 0   (AM_HAL_PIN_160_SDIF0_CMD = 0)
 *
 * With FNCSEL 0 on DAT0-3 and CLK the card is never clocked, and the first
 * command that expects a response ends in CMD-TIMEOUT.
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
/* MMC COMMANDS                                                              */
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

/** Response encodings for TRANSFER.RESPTYPESEL (tiku_emmc_arch.h, table 3). */
#define RESP_NONE   0u
#define RESP_136    1u
#define RESP_48     2u
#define RESP_48BUSY 3u

/** The only EXT_CSD indexes emmc_switch() writes.  Others include
 *  one-time-programmable fields whose write can disable features for good. */
#define EXT_CSD_BUS_WIDTH   183u
#define EXT_CSD_HS_TIMING   185u

/** EXT_CSD indexes this driver reads; reads need no allow-list. */
#define EXT_CSD_DEVICE_TYPE 196u
#define EXT_CSD_REV         192u
#define EXT_CSD_SEC_COUNT   212u
#define EXT_CSD_S_A_TIMEOUT 217u   /* sleep/awake timeout: 100 ns * 2^n     */

/** EXT_CSD[196] DEVICE_TYPE: the speeds the card says it supports. */
#define DEVTYPE_HS_26MHZ    (1u << 0)
#define DEVTYPE_HS_52MHZ    (1u << 1)

/** CMD6 SWITCH argument: access mode [25:24], index [23:16], value [15:8]. */
#define SWITCH_ACCESS_WRITE_BYTE  3u

/** R1 card-status bits; emmc_wait_ready() reads all but R1_ERROR_MASK. */
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
 * @note A wrong boundary setting corrupts data; the bench checks the data
 *       of every DMA transfer except the rand-far reads against a pattern.
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

/** @brief Report one bring-up step to the trace hook, if one is set. */
static void trace(const char *s) { if (s_trace) { s_trace(s); } }
void tiku_emmc_set_trace(void (*fn)(const char *)) { s_trace = fn; }

/** EXT_CSD image, re-read after each configuration change. */
static uint8_t s_ext[TIKU_EMMC_BLOCK_SIZE] __attribute__((aligned(4)));

/*---------------------------------------------------------------------------*/
/* CYCLE CLOCK                                                               */
/*---------------------------------------------------------------------------*/

/* DWT CYCCNT times the init steps, the data-phase deadlines and the bench. */
extern unsigned long tiku_cpu_ambiq_clock_get_hz(void);

/** @brief Start the DWT cycle counter (TRCENA, CYCCNTENA). */
static void cyc_enable(void)
{
    volatile uint32_t *demcr  = (volatile uint32_t *)0xE000EDFCUL;
    volatile uint32_t *dwtctl = (volatile uint32_t *)0xE0001000UL;
    *demcr  |= (1u << 24);      /* TRCENA                                    */
    *dwtctl |= 1u;              /* CYCCNTENA                                 */
}

/** @brief Read the DWT cycle counter. */
static inline uint32_t cyc_now(void)
{
    return *(volatile uint32_t *)0xE0001004UL;
}

/** @brief Convert cycles to microseconds at the current core clock. */
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

/*
 * Data phases are bounded by time, derived from the work: the wire time of
 * the transfer, bytes / (clock x width), times EMMC_XFER_SLACK, plus
 * EMMC_PROGRAM_US for the card's own programming.  The budget scales with
 * the bus: at 1 bit and 375 kHz 128 blocks take 1.4 s on the wire, at 8 bits
 * and 48 MHz a 512 KB transfer about 11 ms.  A timeout here is this driver's
 * own deadline: INTSTAT holds no error bit, and s_last_err is not written.
 */
#define EMMC_XFER_SLACK      4u          /* x expected wire time             */
#define EMMC_PROGRAM_US 500000u          /* + card programming allowance     */
#define EMMC_READY_US  5000000u          /* CMD13 busy poll after a write    */
#define EMMC_CHUNK_US  2000000u          /* max wire time in one command     */

/*
 * Spin ceiling behind the time budgets: a loop bounded only by DWT CYCCNT
 * never ends if the counter stops, and a debug probe detaching or a
 * low-power transition can stop it.  The largest budget, 8 s, is about 800k
 * iterations at the backoff's 10 us floor, so four million is above any real
 * wait and still finite.
 */
#define EMMC_SPIN_CEILING 4000000u

/**
 * @brief Escalating poll backoff: no delay for the first 64 polls, 2 us each
 *        until poll 512, then 10 us.
 *
 * The undelayed first stage keeps a fast operation, such as an 11 us
 * single-block read at 48 MHz, from waiting on a delay; the later stages keep
 * a long wait from loading the bus the controller is using.
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
 * @brief Blocks per command: at most EMMC_MAX_BLKCNT, and at most
 *        EMMC_CHUNK_US of wire time at the current clock and width.
 *
 * This keeps one command's deadline short at a slow setting: 65535 blocks
 * at 375 kHz on one bit take about twelve minutes on the wire.
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
 * @param resp_type  RESP_*
 * @param crc        check the response CRC (off for R3 -- OCR has none)
 * @param idxchk     check the echoed index (off for R3 and R2)
 * @param data       non-zero if a data phase follows
 * @param resp       out: RESPONSE0..3 (may be NULL); [0] alone for 48-bit
 * @return TIKU_EMMC_OK, TIKU_EMMC_ERR_TIMEOUT, or TIKU_EMMC_ERR_CMD with the
 *         INTSTAT error bits in s_last_err
 */
static tiku_emmc_err_t emmc_cmd(uint8_t idx, uint32_t arg, unsigned resp_type,
                                int crc, int idxchk, int data, uint32_t *resp)
{
    return emmc_cmd_x(idx, arg, resp_type, crc, idxchk, data, 0u, resp);
}

/**
 * @brief emmc_cmd() with transfer-mode bits, which travel with the command.
 *
 * TRANSFER holds the transfer-mode fields (direction, block-count enable, DMA)
 * in its low half and the command in its high half, and writing it starts the
 * command, so the mode cannot be written ahead of the command.
 *
 * @param xfer_mode extra low-half bits (DXFERDIRSEL, BLKCNTEN, ...)
 */
static tiku_emmc_err_t emmc_cmd_x(uint8_t idx, uint32_t arg, unsigned resp_type,
                                  int crc, int idxchk, int data,
                                  uint32_t xfer_mode, uint32_t *resp)
{
    uint32_t xfer = xfer_mode;
    uint32_t spins;
    /* A data command also needs the DAT lines free: after a multi-block
     * write the card holds DAT0 low while it programs, and a command issued
     * then fails. */
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
            s_last_err = st;      /* tiku_emmc_last_error() reports it      */
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
 * At each boundary set in BLOCK.HOSTSDMABUFSZ the engine stops, raises
 * DMAINTERRUPT and leaves the resume address in SDMA; writing that address
 * back restarts it, and an unserviced stop never completes.
 *
 * @note Boundary service does not back off: the bus is idle until it runs.
 * @return TIKU_EMMC_OK, TIKU_EMMC_ERR_CMD, or TIKU_EMMC_ERR_TIMEOUT after
 *         @p budget_cyc cycles with no progress
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
 * @brief Poll CMD13 until the card is ready for data and back in TRAN.
 *
 * CMD6 and every write leave the card programming its flash after the host's
 * TRANSFERCOMPLETE, which means only that the bus is free; a SWITCH issued in
 * that window is dropped without an error.
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
        /* SWITCH_ERROR appears in the status after a CMD6, not in the CMD6
         * response: a card that refused the switch answers the CMD6 normally.
         * emmc_set_bus_width() and emmc_set_high_speed() change the host only
         * after this check passes. */
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
/* HOST BRING-UP                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Power SDIO0, force HFRC on and enable the host's clocks. */
static tiku_emmc_err_t emmc_power_on(void)
{
    uint32_t spins = 100000u;

    /* Three steps.  The power domain alone gives the block no clock, and a
     * host with no clock accepts a software-reset request and never
     * completes it (SWRSTALL stays set):
     *
     *   1. PWRCTRL.DEVPWREN.PWRENSDIO0    the power domain
     *   2. CLKGEN.MISC.FRCHFRC            HFRC forced on (the clock manager
     *                                     does this for the first user)
     *   3. MCUCTRL.SDIO0CTRL.SDIO0SYSCLKEN + SDIO0XINCLKEN
     *                                     the host's system and card clocks
     *
     * The vendor HAL does (2) with am_hal_clkmgr_clock_request(HFRC) and (3)
     * in am_hal_sdhc_power_control().  tiku_emmc_deinit() does not undo (2)
     * or (3). */
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
 * @brief Set the bus clock to the highest base / 2^n (n <= 8) not above
 *        @p target_hz.
 *
 * FREQSEL takes divider >> 1, and the order is CLKEN, wait for CLKSTABLE,
 * SDCLKEN: a card clocked before CLKSTABLE answers only intermittently.
 *
 * @return TIKU_EMMC_OK, or TIKU_EMMC_ERR_CLOCK with no base clock or if
 *         CLKSTABLE never sets
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

/**
 * @brief Claim the ten SDIO pads one at a time, each traced first.
 *
 * Each pad's number goes to the trace hook before the pad is claimed, so if
 * claiming one hangs the board, the last trace line names it.
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

/** @brief Pulse the card's reset line (EMMC_PAD_RST, from the board). */
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
/* BUS WIDTH AND SPEED                                                       */
/*---------------------------------------------------------------------------*/

/** @brief Read the 512-byte EXT_CSD register into @p out (PIO, one block). */
static tiku_emmc_err_t emmc_read_ext_csd(uint8_t *out)
{
    uint32_t resp[4];
    tiku_emmc_err_t rc;

    /* PIO: this runs once per configuration change, before any DMA
     * transfer, and a fault in the DMA path cannot corrupt what it reads. */
    SDIO0->BLOCK = TIKU_EMMC_BLOCK_SIZE;
    rc = emmc_cmd_x(MMC_SEND_EXT_CSD, 0u, RESP_48, 1, 1, 1,
                    SDIO0_TRANSFER_DXFERDIRSEL_Msk, resp);
    if (rc == TIKU_EMMC_OK) { rc = emmc_read_buffer(out); }
    if (rc == TIKU_EMMC_OK) { rc = emmc_wait_xfer(emmc_xfer_budget_cyc(1u)); }
    return rc;
}

/**
 * @brief CMD6 SWITCH of one EXT_CSD byte, for index 183 (BUS_WIDTH) or 185
 *        (HS_TIMING) only, with no override.
 *
 * EXT_CSD is partly one-time-programmable, and a write to a wrong index raises
 * no error: it can disable a feature, repartition the device or lock a boot
 * configuration for good.
 *
 * @return TIKU_EMMC_ERR_ARG for any other index, else the command or the
 *         status-poll result
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
    /* R1b: the card holds DAT0 low while it applies the change.  The status
     * poll waits that out and reports a refused switch (SWITCH_ERROR). */
    return emmc_wait_ready();
}

/**
 * @brief Set the bus width (1, 4 or 8 bits): the card first, then the host.
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

    /* Then the host.  Between the card's switch and these writes the two
     * ends disagree about the bus width, so no data command may run in
     * that gap. */
    SDIO0->HOSTCTRL1_b.XFERWIDTH = (bits == 8u) ? 1u : 0u;
    SDIO0->HOSTCTRL1_b.DATATRANSFERWIDTH = (bits == 4u) ? 1u : 0u;
    __DSB();
    s_bus_width = (uint8_t)bits;
    return TIKU_EMMC_OK;
}

/**
 * @brief Switch to high-speed timing and set the clock, capped at what
 *        EXT_CSD[196] DEVICE_TYPE allows (52 or 26 MHz).
 *
 * A card that reports no high-speed mode stays at legacy timing, and only the
 * clock is set.
 */
static tiku_emmc_err_t emmc_set_high_speed(uint32_t target_hz)
{
    uint32_t ceiling;
    tiku_emmc_err_t rc;

    if ((s_devtype & DEVTYPE_HS_52MHZ) != 0u)      { ceiling = 52000000u; }
    else if ((s_devtype & DEVTYPE_HS_26MHZ) != 0u) { ceiling = 26000000u; }
    else {
        /* The card claims no high-speed mode at all.  Stay at legacy timing
         * and set the requested clock with no cap. */
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
/* FALLBACK                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Put both ends back at the identification setting (1 bit, 400 kHz,
 *        legacy timing) after a failed upgrade.
 *
 * The host goes first, the reverse of the upgrade order: a failed upgrade
 * leaves the card at its old setting, so the host must match it before the
 * card can be reached.  The CMD6s that follow are best-effort.
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

/*---------------------------------------------------------------------------*/
/* HS200                                                                     */
/*---------------------------------------------------------------------------*/

/*
 * HS200 at the silicon's 96 MHz ceiling (the vendor header caps the mode's
 * nominal 200 MHz at the SDHC base clock), per JEDEC and the vendor HAL:
 * CMD6 HS_TIMING=2, host UHSMODESEL=SDR104, divider 1, then the RX sample
 * point.
 *
 * Tuning does not use the CMD21 loop: this host samples through MCUCTRL delay
 * taps (OTAPDLYSEL for TX, ITAPDLYSEL for RX, changed inside an ITAPCHGWIN
 * window).  tiku_emmc_hs200() writes a pattern to the scratch region at the
 * current mode, raises the clock, reads the pattern back at every RX tap and
 * takes the centre of the widest passing run.  From the HS_TIMING=2 switch
 * on, a failure returns to HS at 48 MHz; a failure before it changes no
 * setting.
 */
#define EMMC_ITAP_MAX 32u

/** @brief Set the TX and RX delay taps inside an ITAPCHGWIN window. */
static void emmc_set_taps(uint32_t otap, uint32_t itap, int ena)
{
    MCUCTRL->SDIO0CTRL_b.SDIO0ITAPCHGWIN = 1u;
    MCUCTRL->SDIO0CTRL_b.SDIO0OTAPDLYSEL = otap & 15u;
    MCUCTRL->SDIO0CTRL_b.SDIO0OTAPDLYENA = ena ? 1u : 0u;
    MCUCTRL->SDIO0CTRL_b.SDIO0ITAPDLYSEL = itap & 31u;
    MCUCTRL->SDIO0CTRL_b.SDIO0ITAPDLYENA = ena ? 1u : 0u;
    MCUCTRL->SDIO0CTRL_b.SDIO0ITAPCHGWIN = 0u;
    __DSB();
}

/** @brief Reset the host CMD and DAT state machines after a failed read. */
static void emmc_recover_lines(void)
{
    uint32_t g;
    SDIO0->CLOCKCTRL_b.SWRSTCMD = 1u;
    g = 1000u;
    while (SDIO0->CLOCKCTRL_b.SWRSTCMD != 0u && --g != 0u) { }
    SDIO0->CLOCKCTRL_b.SWRSTDAT = 1u;
    g = 1000u;
    while (SDIO0->CLOCKCTRL_b.SWRSTDAT != 0u && --g != 0u) { }
    SDIO0->INTSTAT = 0xFFFFFFFFu;
}

tiku_emmc_err_t tiku_emmc_hs200(void)
{
    static uint8_t pat[512], rd[512];
    uint32_t lba = tiku_emmc_scratch_lba();
    uint32_t i, itap;
    uint8_t ok[EMMC_ITAP_MAX];
    uint32_t best_len = 0u, best_start = 0u, run = 0u, run_start = 0u;
    tiku_emmc_err_t rc;

    if (!s_up)    { return TIKU_EMMC_ERR_POWER; }
    if (s_asleep) { return TIKU_EMMC_ERR_STATE; }

    /* Known bytes on the card, written and verified at the current mode. */
    for (i = 0u; i < 512u; i++) { pat[i] = (uint8_t)(i * 7u + 0x35u); }
    rc = tiku_emmc_write_blocks(lba, 1u, pat, 0);
    if (rc != TIKU_EMMC_OK) { return rc; }
    rc = tiku_emmc_read_blocks(lba, 1u, rd);
    if (rc != TIKU_EMMC_OK || memcmp(pat, rd, 512u) != 0) {
        return TIKU_EMMC_ERR_CMD;
    }

    /* Card first, then host: HS_TIMING=2 is valid at the current clock. */
    rc = emmc_switch(EXT_CSD_HS_TIMING, 2u);
    if (rc != TIKU_EMMC_OK) { goto fallback; }
    SDIO0->AUTO_b.UHSMODESEL = 3u;               /* SDR104 sampling       */
    __DSB();
    rc = emmc_set_clock(96000000u);
    if (rc != TIKU_EMMC_OK) { goto fallback; }

    /* RX tap scan.  A failing point can leave error status set and the data
     * state machine stuck, so after a failing point both lines are reset
     * before the next tap is tried. */
    for (itap = 0u; itap < EMMC_ITAP_MAX; itap++) {
        emmc_set_taps(0u, itap, 1);
        rc = tiku_emmc_read_blocks(lba, 1u, rd);
        ok[itap] = (rc == TIKU_EMMC_OK && memcmp(pat, rd, 512u) == 0)
                   ? 1u : 0u;
        if (!ok[itap]) { emmc_recover_lines(); }
    }
    for (itap = 0u; itap < EMMC_ITAP_MAX; itap++) {
        if (ok[itap]) {
            if (run == 0u) { run_start = itap; }
            run++;
            if (run > best_len) { best_len = run; best_start = run_start; }
        } else {
            run = 0u;
        }
    }
    SHELL_PRINTF("  hs200 itap scan:");
    for (itap = 0u; itap < EMMC_ITAP_MAX; itap++) {
        SHELL_PRINTF("%c", ok[itap] ? '1' : '.');
    }
    SHELL_PRINTF("\n");
    if (best_len < 3u) { goto fallback; }        /* no trustworthy window */

    emmc_set_taps(0u, best_start + best_len / 2u, 1);
    rc = tiku_emmc_read_blocks(lba, 1u, rd);
    if (rc != TIKU_EMMC_OK || memcmp(pat, rd, 512u) != 0) { goto fallback; }

    SHELL_PRINTF("  hs200: %lu MHz, itap %lu (window %lu wide)\n",
                 (unsigned long)(s_clock_hz / 1000000u),
                 (unsigned long)(best_start + best_len / 2u),
                 (unsigned long)best_len);
    return TIKU_EMMC_OK;

fallback:
    emmc_set_taps(0u, 0u, 0);
    SDIO0->AUTO_b.UHSMODESEL = 0u;
    (void)emmc_switch(EXT_CSD_HS_TIMING, 1u);
    (void)emmc_set_clock(48000000u);
    emmc_recover_lines();
    {   /* read the pattern back to check the fallback */
        tiku_emmc_err_t v = tiku_emmc_read_blocks(lba, 1u, rd);
        SHELL_PRINTF("  hs200: failed, back at HS 48 (%s)\n",
                     (v == TIKU_EMMC_OK && memcmp(pat, rd, 512u) == 0)
                     ? "verified" : "AND THE FALLBACK READ FAILED");
    }
    return TIKU_EMMC_ERR_CLOCK;
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

    /* Reset the whole host first: a previous boot may have left it mid-
     * transaction, and SWRSTALL clears that. */
    trace("host-reset");
    SDIO0->CLOCKCTRL_b.SWRSTALL = 1u;
    spins = 10000u;
    while (SDIO0->CLOCKCTRL_b.SWRSTALL != 0u && --spins != 0u) {
        tiku_cpu_ambiq_delay_us(10u);
    }
    if (spins == 0u) { return TIKU_EMMC_ERR_TIMEOUT; }

    s_base_mhz = (uint8_t)SDIO0->CAPABILITIES0_b.SDCLKFREQ;
    if (s_base_mhz == 0u) { return TIKU_EMMC_ERR_CLOCK; }

    /* SDHCI splits interrupt control in two: INTENABLE selects which events
     * appear in INTSTAT at all, and INTSIG which of those also interrupt the
     * CPU.  A polling driver still needs INTENABLE set, or commands complete
     * with INTSTAT left at zero.  INTSIG stays 0: this driver polls. */
    SDIO0->INTENABLE = 0xFFFFFFFFu;
    SDIO0->INTSIG    = 0u;
    __DSB();

    /*
     * Data timeout.  CLOCKCTRL.TIMEOUTCNT sets it as 2^(13+n) ticks of TMCLK,
     * whose rate CAPABILITIES0[7:0] reports (1 MHz here).  A software reset
     * leaves n = 0, 8.192 ms, and an eMMC may hold DAT0 low for hundreds of
     * milliseconds while it programs, as on the first write after power-up,
     * so n is set to the maximum.
     */
    SDIO0->CLOCKCTRL_b.TIMEOUTCNT = 0xEu;   /* 2^27 TMCLK -- the maximum     */
    __DSB();

    trace("pads");
    emmc_pads_config();

    /* Voltage before power, and both are needed: with SDBUSPOWER set but
     * VOLTSELECT unprogrammed the host does not drive the bus, and commands
     * are accepted but never complete.  The eMMC's VCCQ on the EVBs is
     * 1.8 V. */
    trace("bus-power");
    SDIO0->HOSTCTRL1_b.VOLTSELECT = SDIO0_HOSTCTRL1_VOLTSELECT_1_8V;
    __DSB();
    SDIO0->HOSTCTRL1_b.SDBUSPOWER = SDIO0_HOSTCTRL1_SDBUSPOWER_POWERON;
    __DSB();
    tiku_cpu_ambiq_delay_us(2000u);

    /* Identification runs at 400 kHz on a 1-bit bus; the card does not
     * answer CMD1 otherwise. */
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

    /* CMD1 is polled: the card reports busy until its internal init
     * finishes, which the spec allows to take a second. */
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
         * CID's fields sit 8 bits low across RESPONSE3..0.  A wrong shift
         * still yields a plausible-looking product name. */
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
        /* The year's base depends on EXT_CSD_REV, read later, so the raw
         * nibble is kept here and resolved once EXT_CSD is read. */
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

    /* EXT_CSD, a 512-byte data phase, carries the capacity: a card above
     * 2 GB reports a placeholder in the CSD and its sector count only in
     * EXT_CSD[215:212]. */
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

    /* Transfer-ready.  The steps above are what the MMC spec requires; the
     * steps below only add speed.  s_ladder_us times the required part
     * alone (tiku_emmc_init_time()). */
    s_ladder_us = cyc_to_us(cyc_now() - t_start);
    if (rc != TIKU_EMMC_OK) { s_init_us = s_ladder_us; s_up = 0u; return rc; }

    /* The upgrade, then EXT_CSD read back to see whether the card took it. */
    if (width > 1u || hz > 400000u) {
        trace("cmd6-width");
        rc = emmc_set_bus_width(width);
        if (rc == TIKU_EMMC_OK) {
            trace("cmd6-hs");
            rc = emmc_set_high_speed(hz);
        }
        /* Re-reading EXT_CSD is a data transfer at the new width and clock,
         * so a bus that cannot carry data fails here, and a card that
         * declined the switch shows its old width in its own register. */
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
            /* Return the failure, but leave the card working at the
             * identification setting. */
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
/* SLEEP AND WAKE                                                            */
/*---------------------------------------------------------------------------*/
/*
 * Sleep (CMD5) is accepted only in STANDBY, and normal operation leaves the
 * card in TRANSFER, so sleeping is deselect (CMD7 with RCA 0) then CMD5
 * sleep; waking is CMD5 awake then CMD7 select.  A card sent CMD5 in another
 * state ignores it without an error and stays awake, so the driver tracks the
 * state in s_asleep and every access path checks it.
 *
 * The busy wait after CMD5 is on DAT0, not CMD13: a sleeping card does not
 * answer SEND_STATUS.
 */

/** @brief Bound the CMD5 busy wait from EXT_CSD[217]: 100 ns * 2^n. */
static uint32_t emmc_sa_timeout_us(void)
{
    uint32_t us = 1u;
    uint8_t  n  = s_sa_timeout;
    /* 100 ns * 2^n, in microseconds, with n clamped at 23 (about 0.84 s) so
     * an out-of-range field cannot make the wait unbounded. */
    if (n > 23u) { n = 23u; }
    us = (1u << n) / 10u;
    if (us < 1000u) { us = 1000u; }      /* never wait less than a ms       */
    return us;
}

/** @brief Wait for the card to release DAT0 after a CMD5 sleep or awake. */
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

    /* Wait out any programming first: a card told to sleep mid-write may
     * refuse. */
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
    /* The wake returns TIKU_EMMC_OK only once CMD13 reports the card ready
     * in TRAN. */
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
    /* The check is on decoded values: a capacity of zero, or of more than
     * 64M sectors (32 GB), means the identification data was misread even
     * though every command succeeded. */
    if (s_sec_count == 0u || s_sec_count > (64u * 1024u * 1024u)) {
        return TIKU_EMMC_ERR_ID;
    }
    return TIKU_EMMC_OK;
}

/*---------------------------------------------------------------------------*/
/* BLOCK I/O                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief One read or write command of up to 65535 blocks, by DMA or PIO.
 *
 * A multi-block command arms BLKCNT and Auto CMD12, so the host ends the
 * transfer.  At 48 MHz on 8 bits one block is about 11 us on the wire, less
 * than the overhead of a command per block.
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
 * @brief Block transfer: chunking, DMA or PIO selection, cache maintenance.
 *
 * SDMA moves whole words from a 32-bit address and transfers wrong data for
 * a buffer that is not 4-byte aligned, so such a buffer goes by PIO.
 *
 * @note For DMA the buffer is cleaned first, so no dirty line is written back
 *       over data the engine delivered, and invalidated after a read, so the
 *       CPU sees what the engine wrote.
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

/*
 * Split read: tiku_emmc_read_start() arms a DMA read and returns, and
 * tiku_emmc_read_wait() collects it, so the caller can compute meanwhile.
 * One read may be outstanding, and it must fit one chunk (emmc_max_chunk()).
 * The engine stops at each 512 KB SDMA boundary until read_wait services it.
 */
static uint32_t s_rd_busy, s_rd_bytes, s_rd_budget, s_rd_t0;
static uint8_t *s_rd_buf;

tiku_emmc_err_t tiku_emmc_read_start(uint32_t lba, uint32_t n_blk, void *buf)
{
    uint32_t resp[4], xfer;
    tiku_emmc_err_t rc;
    uint8_t cmd;

    if (!s_up)                        { return TIKU_EMMC_ERR_POWER; }
    if (s_asleep)                     { return TIKU_EMMC_ERR_STATE; }
    if (s_rd_busy)                    { return TIKU_EMMC_ERR_STATE; }
    if (n_blk == 0u || n_blk > emmc_max_chunk() ||
        (((uint32_t)(uintptr_t)buf & 3u) != 0u)) {
        return TIKU_EMMC_ERR_ARG;
    }
    if (s_sec_count && (lba + n_blk) > s_sec_count) {
        return TIKU_EMMC_ERR_ARG;
    }

    s_rd_bytes = n_blk * TIKU_EMMC_BLOCK_SIZE;
    tiku_cpu_dcache_clean(buf, s_rd_bytes);

    SDIO0->BLOCK = ((n_blk << SDIO0_BLOCK_BLKCNT_Pos) & SDIO0_BLOCK_BLKCNT_Msk)
                 | ((uint32_t)EMMC_SDMA_BOUND << SDIO0_BLOCK_HOSTSDMABUFSZ_Pos)
                 | (TIKU_EMMC_BLOCK_SIZE & SDIO0_BLOCK_TRANSFERBLOCKSIZE_Msk);
    xfer = SDIO0_TRANSFER_DXFERDIRSEL_Msk | SDIO0_TRANSFER_DMAEN_Msk;
    if (n_blk > 1u) {
        xfer |= SDIO0_TRANSFER_BLKSEL_Msk | SDIO0_TRANSFER_BLKCNTEN_Msk
              | ((1u << SDIO0_TRANSFER_ACMDEN_Pos) & SDIO0_TRANSFER_ACMDEN_Msk);
        cmd = MMC_READ_MULTIPLE;
    } else {
        cmd = MMC_READ_SINGLE;
    }
    SDIO0->HOSTCTRL1_b.DMASELECT = SDIO0_HOSTCTRL1_DMASELECT_SDMA;
    SDIO0->SDMA = (uint32_t)(uintptr_t)buf;
    __DSB();

    rc = emmc_cmd_x(cmd, lba, RESP_48, 1, 1, 1, xfer, resp);
    if (rc != TIKU_EMMC_OK) { return rc; }

    s_rd_buf    = (uint8_t *)buf;
    s_rd_budget = emmc_xfer_budget_cyc(n_blk);
    s_rd_t0     = cyc_now();
    s_rd_busy   = 1u;
    return TIKU_EMMC_OK;
}

tiku_emmc_err_t tiku_emmc_read_wait(void)
{
    tiku_emmc_err_t rc;

    if (!s_rd_busy) { return TIKU_EMMC_OK; }
    rc = emmc_wait_xfer(s_rd_budget);
    if (rc == TIKU_EMMC_OK) {
        tiku_cpu_dcache_invalidate(s_rd_buf, s_rd_bytes);
    }
    s_rd_busy = 0u;
    return rc;
}

tiku_emmc_err_t tiku_emmc_read_blocks(uint32_t lba, uint32_t n_blk, void *buf)
{
    if (!s_up)       { return TIKU_EMMC_ERR_POWER; }
    /* A sleeping card answers nothing, so the read is refused. */
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
    /* Writes below the scratch region need @p force: the card holds data
     * this driver did not write and cannot restore. */
    if (!force && lba < tiku_emmc_scratch_lba()) {
        return TIKU_EMMC_ERR_ARG;
    }

    /* The cast drops const because the shared transfer path is one function
     * for both directions; the write leg never writes through it. */
    rc = emmc_xfer(lba, n_blk, (uint8_t *)(uintptr_t)buf, 1);
    if (rc != TIKU_EMMC_OK) { return rc; }
    /* A write returns only once CMD13 reports the card ready in TRAN, so a
     * write that returns TIKU_EMMC_OK has finished programming. */
    return emmc_wait_ready();
}

/*---------------------------------------------------------------------------*/
/* BENCH                                                                     */
/*---------------------------------------------------------------------------*/
/*
 * The legs, in the order they run:
 *   seq-wr-*    writes of 4 KB, 64 KB and 512 KB per call, read back
 *   seq-rd-*    reads of the same sizes, checked against the last write
 *   rand-rd     single-block reads at random offsets in the scratch region
 *   rand-far    single-block reads across the whole card, timed only
 *   unalign-rd  a read into an unaligned buffer, which takes the PIO path
 *   dtcm-rd     an untimed read into DTCM: whether SDMA reaches DTCM
 *   allow-list  a CMD6 to index 179, which emmc_switch() must refuse
 * Every leg but dtcm-rd and allow-list is timed with DWT; a leg whose data
 * does not match its pattern reports FAIL and no bandwidth.
 *
 * Writes go only to the scratch region, and each write leg uses its own
 * pattern seed, so its check cannot pass on data an earlier leg left.
 */

#define BENCH_BLOCKS  TIKU_EMMC_SCRATCH_BLOCKS               /* 1024 = 512 KB */
#define BENCH_BYTES   (BENCH_BLOCKS * TIKU_EMMC_BLOCK_SIZE)

/*
 * Bounce buffer, taken from SRAM tier span 0 (SSRAM, where this part's large
 * DMA buffers live) for one operation and returned after it.  The +4 is
 * headroom for the unaligned leg.
 */
#define BENCH_WORKSPACE_BYTES (BENCH_BYTES + 4u)
static uint8_t *s_bench_buf;
static tiku_mem_workspace_t s_bench_workspace;

/** DTCM buffer for the dtcm-rd leg and the diagnostics. */
static uint8_t s_dtcm_buf[4096] __attribute__((aligned(32)));

/** @brief Reserve exclusive staging capacity for one operation. */
static int bench_workspace_open(void)
{
    if (s_bench_buf != NULL) return 0;
    if (tiku_tier_arena_create_span(&s_bench_workspace, TIKU_MEM_SRAM, 0,
            BENCH_WORKSPACE_BYTES, 32u, 0) != TIKU_MEM_OK) return 0;
    s_bench_buf = tiku_arena_alloc(&s_bench_workspace, BENCH_WORKSPACE_BYTES);
    if (s_bench_buf == NULL) (void)tiku_mem_workspace_close(&s_bench_workspace);
    return s_bench_buf != NULL;
}

/** @brief Give the bounce buffer back to the tier. */
static void bench_workspace_close(void)
{
    if (s_bench_buf != NULL) {
        if (tiku_mem_workspace_close(&s_bench_workspace) == TIKU_MEM_OK)
            s_bench_buf = NULL;
    }
}

/** @brief Report a refused workspace and the free space in SRAM span 0. */
static void bench_buf_refused(const char *who)
{
    tiku_mem_space_t space;
    unsigned long room = 0ul;

    if (tiku_tier_span_space(TIKU_MEM_SRAM, 0, &space) == TIKU_MEM_OK) {
        room = (unsigned long)space.free_bytes;
    }
    SHELL_PRINTF("%s: staging workspace busy or unavailable: %lu KB (%lu KB free in span 0)\n", who,
                 (unsigned long)(BENCH_WORKSPACE_BYTES / 1024u), room / 1024u);
}

/** @brief Pattern byte for scratch-region offset @p a under seed @p s. */
static inline uint8_t bench_pat(uint32_t a, uint32_t s)
{
    return (uint8_t)(a ^ (a >> 8) ^ (a >> 16) ^ (s * 0x9Du) ^ 0xC3u);
}

/**
 * @brief Print one leg's result; a failed leg gets no bandwidth figure.
 *
 * A failed leg's time stops where the transfer broke, but its byte count is
 * the whole span.
 */
static void bench_report(const char *leg, uint32_t bytes, uint32_t cyc,
                         int exact, tiku_emmc_err_t rc)
{
    static const char *const en[] = { "ok", "POWER", "CLOCK", "TIMEOUT",
                                      "CMD", "ID", "ARG", "STATE", "NOMEM" };
    const unsigned n_en = (unsigned)(sizeof en / sizeof en[0]);
    unsigned long hz = tiku_cpu_ambiq_clock_get_hz();
    unsigned long kbps;

    if (cyc == 0u || hz == 0u) { cyc = 1u; }
    if (!exact) {
        uint32_t e = s_last_err;
        /* Both rc and intstat: a TIMEOUT comes from this driver's deadline
         * with intstat zero, since the controller raised nothing, so the pair
         * tells a driver timeout from a controller error. */
        SHELL_PRINTF("  %-11s %6lu KB  %8lu us      -- MB/s  FAIL %s  "
                     "intstat %08lx%s%s%s%s%s%s\n", leg,
                     (unsigned long)(bytes / 1024u),
                     (unsigned long)cyc_to_us(cyc),
                     en[(unsigned)rc < n_en ? (unsigned)rc : 0u],
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

/** @brief Fill @p len bytes of the bench buffer with seed @p s. */
static void bench_fill(uint32_t s, uint32_t len)
{
    uint32_t i;
    for (i = 0u; i < len; i++) { s_bench_buf[i] = bench_pat(i, s); }
}

/** @brief Compare @p len bytes at @p off against seed @p s; 1 if equal. */
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
    uint32_t seed = 0u;   /* the pattern now on the card        */
    tiku_emmc_err_t rc;

    if (!s_up) { SHELL_PRINTF("bench: emmc not up\n"); return; }
    base = tiku_emmc_scratch_lba();
    if (base == 0u) { SHELL_PRINTF("bench: no scratch region\n"); return; }
    if (!bench_workspace_open()) { bench_buf_refused("bench"); return; }

    /*
     * The span follows the bus speed.  Below 1 MHz (the identification
     * setting moves about 50 KB/s) a 512 KB span takes about ten seconds
     * per leg, so the span drops to 64 KB and only the transfer sizes that
     * fit it run.  Rates are per byte moved, so the two spans compare
     * directly.
     */
    span_blk   = (s_clock_hz < 1000000u) ? 128u : BENCH_BLOCKS;
    span_bytes = span_blk * TIKU_EMMC_BLOCK_SIZE;

    cyc_enable();
    SHELL_PRINTF("emmcbench @ %u-bit, %lu Hz  (scratch LBA %lu, %lu KB span)\n",
                 s_bus_width, (unsigned long)s_clock_hz,
                 (unsigned long)base, (unsigned long)(span_bytes / 1024u));
    SHELL_PRINTF("  init: ladder %lu us (400 kHz 1-bit), total %lu us\n",
                 (unsigned long)s_ladder_us, (unsigned long)s_init_us);

    /* Sequential write, three transfer sizes. */
    for (k = 0u; k < 3u; k++) {
        if (sizes[k] > span_blk) { continue; }
        seed = k + 1u;      /* leaves this leg's pattern on the card, which
                             * the read legs below then check               */
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

    /* Sequential read, the same transfer sizes; seed is the pattern the
     * last write leg left on the card. */
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

    /*
     * Single-block latency, two figures.  Inside the scratch region the data
     * is known, so the latency comes with a check, though over so small a
     * span the card's translation layer may serve from its cache.  Across the
     * whole card the contents are unknown, so that leg reports time only.
     */
    {
        const uint32_t n = 256u;
        uint32_t lcg = 12345u, bad = 0u, acc = 0u, done = 0u;

        /*
         * The timer brackets the read alone, not the check, which walks the
         * just-invalidated SSRAM buffer.
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
            /* The average is over the completed reads, fewer than n after
             * an early break. */
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

    /*
     * The unaligned fallback: a buffer at offset 1 makes emmc_xfer() use
     * PIO, so this leg checks and times that path.
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

    /*
     * SDIO is a bus master of its own, separate from MSPI; this case reads
     * into a DTCM buffer to check that SDIO's DMA reaches DTCM.
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

    /*
     * Negative check of the CMD6 allow-list: index 179 is PARTITION_CONFIG,
     * partly write-once.  emmc_switch() refuses it before touching any
     * register, so the command is never issued.
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
    bench_workspace_close();
}

/*---------------------------------------------------------------------------*/
/* STAGING INTO PSRAM                                                        */
/*---------------------------------------------------------------------------*/
#if (TIKU_DRV_PSRAM_ENABLE + 0)

#include "tiku_psram_arch.h"

/*
 * Staging copies data from the eMMC into the PSRAM tier: SDIO's DMA fills an
 * SSRAM bounce buffer from the card, the MSPI command queue drains it into
 * the PSRAM, and the CPU touches the bytes only to hash them.
 *
 * Nothing is written to the card.  The source hash is taken from the bounce
 * buffer as it passes and compared with a hash of what is read back out of
 * the PSRAM.  tiku_emmc_stage_run() times the hashing apart from the
 * transfers and reports both times.
 */

#define STAGE_CHUNK   BENCH_BYTES        /* 512 KB: one eMMC command's worth */
#define STAGE_SEG     65536u             /* CQ segment size on the PSRAM side */

/** @brief FNV-1a over whole 32-bit words, skipping a trailing partial word. */
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
/* STAGING A FILE BY ITS EXTENTS                                             */
/*---------------------------------------------------------------------------*/
/*
 * tiku_emmc_stage_open(), _chunk() and _close() stage a file that need not be
 * contiguous on the card: the FAT layer passes one run of sectors per
 * tiku_emmc_stage_chunk() call, and the chunks are written one after another
 * into PSRAM from offset 0.
 */
static uint32_t s_stg_off, s_stg_src, s_stg_rd, s_stg_wr;
static int      s_stg_xip;

tiku_emmc_err_t tiku_emmc_stage_open(void)
{
    s_stg_xip = 0;
    if (!bench_workspace_open()) { return TIKU_EMMC_ERR_NOMEM; }
    cyc_enable();
    s_stg_off = 0u;
    s_stg_src = 2166136261u;
    s_stg_rd  = 0u;
    s_stg_wr  = 0u;
    s_stg_xip = tiku_psram_xip_enabled();
    if (s_stg_xip) { (void)tiku_psram_xip_enable(0); }
    return TIKU_EMMC_OK;
}

tiku_emmc_err_t tiku_emmc_stage_chunk(uint32_t lba, uint32_t nsec)
{
    uint32_t left = nsec;

    if (s_bench_buf == NULL) { return TIKU_EMMC_ERR_NOMEM; }
    while (left != 0u) {
        uint32_t n = (left > (STAGE_CHUNK / TIKU_EMMC_BLOCK_SIZE))
                     ? (STAGE_CHUNK / TIKU_EMMC_BLOCK_SIZE) : left;
        uint32_t bytes = n * TIKU_EMMC_BLOCK_SIZE;
        uint32_t seg;
        uint32_t t0;
        tiku_emmc_err_t rc;

        t0 = cyc_now();
        rc = tiku_emmc_read_blocks(lba, n, s_bench_buf);
        s_stg_rd += cyc_now() - t0;
        if (rc != TIKU_EMMC_OK) { return rc; }

        s_stg_src = stage_hash(s_bench_buf, bytes, s_stg_src);
        tiku_cpu_dcache_clean(s_bench_buf, bytes);

        /*
         * The command queue needs total % seg == 0, and the last chunk of a
         * file is whatever is left over, so a chunk that 64 KB does not
         * divide goes as one segment of its own length.
         */
        seg = ((bytes % STAGE_SEG) == 0u) ? STAGE_SEG : bytes;

        t0 = cyc_now();
        if (tiku_psram_cq_xfer(s_stg_off, s_bench_buf, bytes,
                               seg, 1) != 0) {
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
    tiku_emmc_err_t rc = (s_bench_buf != NULL) ? TIKU_EMMC_OK
                                               : TIKU_EMMC_ERR_NOMEM;

    /* Hash the staged image as read back out of the PSRAM, where the tier
     * reads it. */
    for (off = 0u; rc == TIKU_EMMC_OK && off < total_bytes;
         off += STAGE_CHUNK) {
        uint32_t n = ((total_bytes - off) < STAGE_CHUNK)
                     ? (total_bytes - off) : STAGE_CHUNK;
        /* Same short-chunk rule as the write path: the queue needs
         * total % seg == 0, and the last chunk is whatever is left. */
        uint32_t seg = ((n % STAGE_SEG) == 0u) ? STAGE_SEG : n;
        if (tiku_psram_cq_xfer(off, s_bench_buf, n, seg, 0) != 0) {
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
    bench_workspace_close();
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
    if (!bench_workspace_open()) { bench_buf_refused("stage"); return; }

    cyc_enable();
    /* XIP is turned off for the command-queue transfers and turned back on
     * after if it was on, which makes the staged image addressable. */
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

    /* Hash the image as read back out of the PSRAM, through the same bounce
     * buffer: a read-back that leaves the buffer's old contents in place
     * hashes differently from the source. */
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
    bench_workspace_close();

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
 * @brief Read known blocks back with each combination of block count (1 or 4)
 *        and buffer (SSRAM, DTCM, unaligned), to tell which one breaks a read.
 *
 * Four blocks are written first by single-block writes; each case prints its
 * time, whether the data matched, and where it first differs.
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
     * of TMCLK, whose frequency CAPABILITIES0[7:0] reports.  It is printed
     * because the timeout does not scale with the bus clock, while a block
     * takes longer on the wire at a slower clock.
     */
    SHELL_PRINTF("  timeoutcnt %lu  clockctrl %08lx  capabilities0 %08lx\n",
                 (unsigned long)SDIO0->CLOCKCTRL_b.TIMEOUTCNT,
                 (unsigned long)SDIO0->CLOCKCTRL,
                 (unsigned long)SDIO0->CAPABILITIES0);

    /* Write four known blocks one at a time, by single-block writes. */
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
    if (!bench_workspace_open()) { bench_buf_refused("diag"); return; }

    /*
     * Each case names its variables and its result.  The time column is in
     * microseconds: at 1 bit and 375 kHz a 512 B block needs at least
     * 10900 us on the wire, so a faster case reports data that never arrived.
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
    bench_workspace_close();
}

uint32_t tiku_emmc_last_error(void) { return s_last_err; }

void tiku_emmc_regs(uint32_t *out, unsigned n)
{
    unsigned i;

    /* Reading an unpowered peripheral stalls the APB and hangs the CPU with
     * no fault, so the SDIO0 registers are read only while SDIO0 is powered;
     * slots not read hold 0xDEADDEAD. */
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
