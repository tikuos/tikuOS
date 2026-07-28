/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_emmc_arch.c - Apollo510 SDIO0 + IS21EF08G eMMC bring-up.
 *
 * Implements tables 0-3 of tiku_emmc_arch.h.  Read that header first: the
 * pin table there records a BSP that contradicts itself, and the command
 * table records which EXT_CSD indexes may EVER be written.
 *
 * LESSONS INHERITED, NOT RE-LEARNED.  This is the fourth external-memory
 * driver in this port and it starts from what the first three paid for:
 *
 *   - every register field through its CMSIS enum NAME or a position from
 *     the header; never a value inferred from vendor-source ordering
 *   - every wait bounded, every failure a DISTINCT error, fail closed
 *   - status polls BACK OFF: a tight spin is bus traffic into the very
 *     controller doing the work (it was 20 % of the PSRAM's plateau)
 *   - a register dump is POWER-SAFE: reading an unpowered peripheral stalls
 *     the APB and hangs the CPU with no fault (it cost a board wedge)
 *   - a step tracer on every risky rung, because a wedged bring-up prints
 *     nothing and the last line becomes the diagnosis
 *   - prove the instrument before believing its verdict
 *
 * WHAT IS NEW HERE: a card with its own firmware and a STATE MACHINE.  The
 * device must be walked idle -> identify -> standby -> transfer, and a
 * command issued in the wrong state fails in ways that look like wiring
 * faults.  Hence the ladder in tiku_emmc_init() is linear, traced, and
 * refuses to continue past a rung that did not do what it claimed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#if defined(PLATFORM_AMBIQ) && (TIKU_DRV_EMMC_ENABLE + 0)

#include "tiku_emmc_arch.h"
#include "tiku_gpio_arch.h"
#include "tiku_cpu_common.h"
#include "apollo510.h"
#include <kernel/cpu/tiku_hang.h>

/*---------------------------------------------------------------------------*/
/* PADS (table 0)                                                            */
/*---------------------------------------------------------------------------*/

#define EMMC_PAD_D0     84u
#define EMMC_PAD_D3     87u
#define EMMC_PAD_CLK    88u
#define EMMC_PAD_D4    156u
#define EMMC_PAD_D7    159u
#define EMMC_PAD_CMD   160u
/* GP13, not the BSP's 12 -- the BSP gives GP12 to the console UART as well,
 * and one pad cannot be both.  See table 0 for the three-way resolution. */
#define EMMC_PAD_RST    13u

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
#define PAD_FNCSEL_SDIO_LOW   2u   /* GP84..GP88   */
#define PAD_FNCSEL_SDIO_HIGH  0u   /* GP156..GP160 */
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

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

static uint8_t  s_up;
static uint32_t s_rca;
static uint32_t s_sec_count;
static uint32_t s_clock_hz;
static uint8_t  s_bus_width = 1u;
static uint8_t  s_base_mhz;
static uint32_t s_last_err;    /**< INTSTAT at the last command failure     */
static tiku_emmc_id_t s_id;
static void   (*s_trace)(const char *step);

static void trace(const char *s) { if (s_trace) { s_trace(s); } }
void tiku_emmc_set_trace(void (*fn)(const char *)) { s_trace = fn; }

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

#define EMMC_CMD_SPINS   20000u   /* x 10 us = 200 ms per command            */
#define EMMC_DATA_SPINS  50000u   /* x 10 us = 500 ms per block phase        */

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
 * TRANSFER is one 32-bit register holding both the transfer-mode fields
 * (direction, block-count enable, DMA) in its low half and the command
 * fields in its high half, and writing it is what STARTS the command.  So
 * the mode bits cannot be programmed in a separate earlier write -- the
 * command write erases them.  That is exactly what happened here: the
 * direction bit was set, then zeroed a microsecond later by the command,
 * so the host sat waiting to be given write data while this driver sat
 * waiting to be handed read data.  Neither timed out on an error, because
 * nothing was wrong -- they were simply facing opposite ways.
 *
 * @param xfer_mode extra low-half bits (DXFERDIRSEL, BLKCNTEN, ...)
 */
static tiku_emmc_err_t emmc_cmd_x(uint8_t idx, uint32_t arg, unsigned resp_type,
                                  int crc, int idxchk, int data,
                                  uint32_t xfer_mode, uint32_t *resp)
{
    uint32_t xfer = xfer_mode;
    uint32_t spins;

    /* The card owns the bus until it says otherwise: never issue a command
     * while CMD (or, for data commands, DAT) is inhibited. */
    spins = EMMC_CMD_SPINS;
    while (SDIO0->PRESENT_b.CMDINHCMD != 0u && --spins != 0u) {
        tiku_cpu_ambiq_delay_us(10u);
    }
    if (spins == 0u) { return TIKU_EMMC_ERR_TIMEOUT; }

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
    spins = EMMC_CMD_SPINS;
    while (--spins != 0u) {
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
        tiku_cpu_ambiq_delay_us(10u);
    }
    if (spins == 0u) { return TIKU_EMMC_ERR_TIMEOUT; }

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
    uint32_t spins = EMMC_DATA_SPINS;
    uint32_t i;

    while (--spins != 0u) {
        uint32_t st = SDIO0->INTSTAT;
        if ((st & SDIO0_INTSTAT_ERRORINTERRUPT_Msk) != 0u) {
            return TIKU_EMMC_ERR_CMD;
        }
        if ((st & SDIO0_INTSTAT_BUFFERREADREADY_Msk) != 0u) { break; }
        tiku_cpu_ambiq_delay_us(10u);
    }
    if (spins == 0u) { return TIKU_EMMC_ERR_TIMEOUT; }
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
    uint32_t spins = EMMC_DATA_SPINS;
    uint32_t i;

    while (--spins != 0u) {
        uint32_t st = SDIO0->INTSTAT;
        if ((st & SDIO0_INTSTAT_ERRORINTERRUPT_Msk) != 0u) {
            return TIKU_EMMC_ERR_CMD;
        }
        if ((st & SDIO0_INTSTAT_BUFFERWRITEREADY_Msk) != 0u) { break; }
        tiku_cpu_ambiq_delay_us(10u);
    }
    if (spins == 0u) { return TIKU_EMMC_ERR_TIMEOUT; }
    SDIO0->INTSTAT = SDIO0_INTSTAT_BUFFERWRITEREADY_Msk;

    for (i = 0u; i < TIKU_EMMC_BLOCK_SIZE; i += 4u) {
        SDIO0->BUFFER = (uint32_t)src[i]
                      | ((uint32_t)src[i + 1] << 8)
                      | ((uint32_t)src[i + 2] << 16)
                      | ((uint32_t)src[i + 3] << 24);
    }
    return TIKU_EMMC_OK;
}

/** @brief Wait for TRANSFERCOMPLETE after a data phase. */
static tiku_emmc_err_t emmc_wait_xfer(void)
{
    uint32_t spins = EMMC_DATA_SPINS;
    while (--spins != 0u) {
        uint32_t st = SDIO0->INTSTAT;
        if ((st & SDIO0_INTSTAT_ERRORINTERRUPT_Msk) != 0u) {
            return TIKU_EMMC_ERR_CMD;
        }
        if ((st & SDIO0_INTSTAT_TRANSFERCOMPLETE_Msk) != 0u) {
            SDIO0->INTSTAT = SDIO0_INTSTAT_TRANSFERCOMPLETE_Msk;
            return TIKU_EMMC_OK;
        }
        tiku_cpu_ambiq_delay_us(10u);
        tiku_hang_checkin();
    }
    return TIKU_EMMC_ERR_TIMEOUT;
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
 * SDCLKEN.  Skipping the stability poll is how a card gets clocked before
 * the host's PLL has settled, which presents as a card that answers
 * intermittently -- the worst failure mode to debug.
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
/* THE LADDER (table 2)                                                      */
/*---------------------------------------------------------------------------*/

tiku_emmc_err_t tiku_emmc_init(void)
{
    tiku_emmc_err_t rc;
    uint32_t resp[4];
    uint32_t spins;

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
         * gate checks the decoded values rather than merely "we got bits". */
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
    {
        static uint8_t ext[TIKU_EMMC_BLOCK_SIZE];
        SDIO0->BLOCK = TIKU_EMMC_BLOCK_SIZE;   /* one block of 512          */
        rc = emmc_cmd_x(MMC_SEND_EXT_CSD, 0u, RESP_48, 1, 1, 1,
                        SDIO0_TRANSFER_DXFERDIRSEL_Msk, resp);
        if (rc == TIKU_EMMC_OK) { rc = emmc_read_buffer(ext); }
        if (rc == TIKU_EMMC_OK) { rc = emmc_wait_xfer(); }
        if (rc == TIKU_EMMC_OK) {
            s_sec_count = (uint32_t)ext[212] | ((uint32_t)ext[213] << 8) |
                          ((uint32_t)ext[214] << 16) |
                          ((uint32_t)ext[215] << 24);
            s_id.ext_csd_rev = ext[192];
        }
    }
    /* Resolve the manufacture year now that EXT_CSD_REV is known: the MMC
     * spec moved the epoch from 1997 to 2013 at EXT_CSD_REV >= 4. */
    s_id.mfg_year = (uint16_t)((s_id.ext_csd_rev >= 4u ? 2013u : 1997u)
                               + s_id.mfg_year);

    s_id.sec_count = s_sec_count;
    s_id.bus_width = s_bus_width;
    s_id.clock_hz  = s_clock_hz;
    return (rc == TIKU_EMMC_OK) ? TIKU_EMMC_OK : rc;
}

void tiku_emmc_deinit(void)
{
    if (tiku_emmc_powered()) {
        SDIO0->CLOCKCTRL &= ~(SDIO0_CLOCKCTRL_CLKEN_Msk |
                              SDIO0_CLOCKCTRL_SDCLKEN_Msk);
        SDIO0->HOSTCTRL1_b.SDBUSPOWER = 0u;
    }
    PWRCTRL->DEVPWREN &= ~PWRCTRL_DEVPWREN_PWRENSDIO0_Msk;
    __DSB();
    s_up = 0u; s_clock_hz = 0u;
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

tiku_emmc_err_t tiku_emmc_read_blocks(uint32_t lba, uint32_t n_blk, void *buf)
{
    uint8_t *dst = (uint8_t *)buf;
    uint32_t resp[4];
    tiku_emmc_err_t rc;

    if (!s_up)      { return TIKU_EMMC_ERR_POWER; }
    if (n_blk == 0u) { return TIKU_EMMC_ERR_ARG; }
    if (s_sec_count && (lba + n_blk) > s_sec_count) {
        return TIKU_EMMC_ERR_ARG;
    }

    /* Single-block loop for the first cut: multi-block adds CMD12 and a
     * second error surface, and E2's job is a bit-exact block device, not a
     * fast one.  E3 adds the multi-block and DMA paths. */
    while (n_blk-- != 0u) {
        SDIO0->BLOCK = TIKU_EMMC_BLOCK_SIZE;
        rc = emmc_cmd_x(MMC_READ_SINGLE, lba, RESP_48, 1, 1, 1,
                        SDIO0_TRANSFER_DXFERDIRSEL_Msk, resp);
        if (rc != TIKU_EMMC_OK) { return rc; }
        rc = emmc_read_buffer(dst);
        if (rc != TIKU_EMMC_OK) { return rc; }
        rc = emmc_wait_xfer();
        if (rc != TIKU_EMMC_OK) { return rc; }
        dst += TIKU_EMMC_BLOCK_SIZE;
        lba++;
        tiku_hang_checkin();
    }
    return TIKU_EMMC_OK;
}

tiku_emmc_err_t tiku_emmc_write_blocks(uint32_t lba, uint32_t n_blk,
                                       const void *buf, int force)
{
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t resp[4];
    tiku_emmc_err_t rc;

    if (!s_up)       { return TIKU_EMMC_ERR_POWER; }
    if (n_blk == 0u) { return TIKU_EMMC_ERR_ARG; }
    if (s_sec_count && (lba + n_blk) > s_sec_count) {
        return TIKU_EMMC_ERR_ARG;
    }
    /* Default-deny outside the scratch region.  The card arrived with
     * contents we did not write and cannot restore; an unattended driver
     * has no business touching them. */
    if (!force && lba < tiku_emmc_scratch_lba()) {
        return TIKU_EMMC_ERR_ARG;
    }

    while (n_blk-- != 0u) {
        SDIO0->BLOCK = TIKU_EMMC_BLOCK_SIZE;
        rc = emmc_cmd_x(MMC_WRITE_SINGLE, lba, RESP_48, 1, 1, 1,
                        0u /* DXFERDIRSEL clear = host -> card */, resp);
        if (rc != TIKU_EMMC_OK) { return rc; }
        rc = emmc_write_buffer(src);
        if (rc != TIKU_EMMC_OK) { return rc; }
        rc = emmc_wait_xfer();
        if (rc != TIKU_EMMC_OK) { return rc; }
        src += TIKU_EMMC_BLOCK_SIZE;
        lba++;
        tiku_hang_checkin();
    }
    return TIKU_EMMC_OK;
}

/*---------------------------------------------------------------------------*/
/* DIAGNOSTICS                                                               */
/*---------------------------------------------------------------------------*/

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
