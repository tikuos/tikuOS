/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.c - Apollo510 MSPI0 and APS512 octal-DDR PSRAM bring-up.
 *
 * Every register field is written through its CMSIS enum name, so the compiler
 * owns the encoding.  Configure the controller before the pads, bound every wait
 * with a distinct error, and read identity at the lowest clock before trusting it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#if defined(PLATFORM_AMBIQ) && (TIKU_DRV_PSRAM_ENABLE + 0)

#include "tiku_psram_arch.h"
#include "tiku_gpio_arch.h"      /* tiku_ambiq_gpio_pad_config()             */
#include "tiku_cpu_common.h"     /* tiku_cpu_ambiq_delay_us()                */
#include "hal/tiku_cpu.h"        /* dcache clean/invalidate for the bench    */
#include <kernel/cpu/tiku_hang.h>   /* bench loops block on purpose          */
#include <kernel/shell/tiku_shell_io.h> /* bench reports via SHELL_PRINTF    */
#include <kernel/memory/tiku_mem.h>    /* the TIKU_MEM_PSRAM tier attach     */
#include "apollo510.h"           /* CMSIS register map -- register defs only */

/*---------------------------------------------------------------------------*/
/* DEVICE COMMANDS (table 2)                                                 */
/*---------------------------------------------------------------------------*/

#define PSRAM_CMD_GLOBAL_RESET   0xFFFFu
#define PSRAM_CMD_READ           0x2020u
#define PSRAM_CMD_WRITE          0xA0A0u
#define PSRAM_CMD_REG_READ       0x4040u
#define PSRAM_CMD_REG_WRITE      0xC0C0u

/** Identity constants -- the M1 gate. */
#define PSRAM_VID_AP_MEMORY      0x0Du
#define PSRAM_DENSITY_512MBIT    0x06u
#define PSRAM_GB_PASS            0x06u

/*---------------------------------------------------------------------------*/
/* PADS (table 1 step 16)                                                    */
/*---------------------------------------------------------------------------*/

/*
 * GP64..GP73 carry MSPI0 signal indices 0..9: 0-7 are data, 8 is CLK, 9 is
 * DQS0/DM0.  On those pads the MSPI0 function is FNCSEL 0 (they are
 * dedicated MSPI pads).  GP199 is the chip select, where MNCE0 is FNCSEL 1.
 * Transcribed from am_bsp_pins.c: output driver DISABLED on data/clock/DQS
 * (PADOUTEN in the controller owns direction), drive strength 0.5x, no
 * pull.  CE is push-pull with NCESRC=0 and active-low polarity.
 */
#define PSRAM_PAD_D0        64u
#define PSRAM_PAD_D7        71u
#define PSRAM_PAD_CLK       72u
#define PSRAM_PAD_DQS       73u
/* The chip select is a BOARD fact (see the board header); D0..DQS above are
 * MSPI0's dedicated pads and are silicon, which is why only this one moved. */
#if !defined(TIKU_BOARD_PSRAM_PAD_CE)
#error "This board declares no TIKU_BOARD_PSRAM_PAD_CE. The build system \
should not have compiled the PSRAM driver for it -- see BOARD_CAPS/PSRAM in \
the Makefile."
#endif
#define PSRAM_PAD_CE        TIKU_BOARD_PSRAM_PAD_CE

#define PAD_FNCSEL_MSPI0    0u
#define PAD_FNCSEL_MNCE0    1u
#define PAD_DS_0P5X         (1u << 10)   /* DS[11:10] = 0.5x driver         */
#define PAD_OUTCFG_PUSHPULL (1u << 8)    /* OUTCFG[9:8] = push-pull         */
#define PAD_NCEPOL_LOW      (0u << 22)   /* NCEPOL: active low              */

/** Data/CLK/DQS: function select + drive strength + INPUT ENABLE.
 *
 * The vendor BSP leaves INPEN clear on these pads and its driver works, so the
 * receive path presumably bypasses the GPIO input gate.  But the bit-bang probe
 * showed the GPIO path DOES read these pins, and the buffer costs nothing. */
#define PAD_INPEN           (1u << 4)
#define PAD_CFG_MSPI_IO     (PAD_FNCSEL_MSPI0 | PAD_DS_0P5X | PAD_INPEN)
/** CE: driven by the controller's NCE0 source, push-pull, active low. */
#define PAD_CFG_MSPI_CE     (PAD_FNCSEL_MNCE0 | PAD_DS_0P5X | \
                             PAD_OUTCFG_PUSHPULL | PAD_NCEPOL_LOW)

/*---------------------------------------------------------------------------*/
/* CLOCK TABLE (table 1, the derived clock model)                            */
/*---------------------------------------------------------------------------*/

/*
 * CLKGEN.MSPIIOCLKCTRL.MSPIxIOCLKSEL encodings, from am_hal_mspi.h's
 * am_hal_mspi_io_clock_sel_e.  READ, NOT COUNTED: the enum starts at
 * HFRC_750KHZ = 0 and doubles, so the two sources this driver uses sit at 8
 * and 10 -- assuming 1 and 2 (the order they appear in the frequency switch)
 * selects HFRC_1P5MHZ and HFRC_3MHZ instead, which is a 128x clock error
 * that still "works" slowly enough to look like a timing problem.
 */
#define IOCLK_SEL_HFRC_192MHZ   8u
#define IOCLK_SEL_HFRC2_250MHZ 10u

/* DEV0CFG.CLKDIV0: the field is a raw divider count, not an enum with
 * surprises -- CLKDIV1 == 1.  Verified against the register header. */
#define CLKDIV_1  1u
#define CLKDIV_2  2u

typedef struct {
    uint8_t  ioclk_sel;   /**< MSPIIOCLKCTRL source select                  */
    uint8_t  clkdiv;      /**< DEV0CFG.CLKDIV0                              */
    uint8_t  sdr250;      /**< DEV0CFG1.SDR250EN0 -- bypasses the /2        */
    uint8_t  txneg;       /**< DEV0CFG.TXNEG0 -- FREQUENCY-DEPENDENT        */
    uint32_t hz;          /**< nominal IO clock, for reporting              */
} psram_clk_t;

/* Order matches TIKU_PSRAM_CLK_*.  Every row obeys
 * hz = source / (2 * clkdiv), except where sdr250 bypasses the /2.
 *
 * TXNEG is the TX clock-edge select and the vendor picks it BY FREQUENCY:
 * 0 at 62.5 MHz and below, 1 at 96 MHz and above.  The first cut of this
 * driver hard-coded 1 (the fast-clock value) at the 48 MHz bring-up clock,
 * which launches every command bit half a clock early -- the device decodes
 * garbage, never answers, and the controller-side "success" of TX-only
 * commands hides it.  Bug #7 of this bring-up, and the one that silenced
 * the device completely. */
static const psram_clk_t s_clk[] = {
    { IOCLK_SEL_HFRC_192MHZ,  CLKDIV_2, 0u, 0u,  48000000u },
    { IOCLK_SEL_HFRC_192MHZ,  CLKDIV_1, 0u, 1u,  96000000u },
    { IOCLK_SEL_HFRC2_250MHZ, CLKDIV_1, 0u, 1u, 125000000u },
    { IOCLK_SEL_HFRC_192MHZ,  CLKDIV_1, 1u, 1u, 192000000u },
    { IOCLK_SEL_HFRC2_250MHZ, CLKDIV_1, 1u, 1u, 250000000u },
};
#define PSRAM_CLK_COUNT (sizeof s_clk / sizeof s_clk[0])

/*---------------------------------------------------------------------------*/
/* LATENCY (table 1 steps 4 and 12; table 2 MR0/MR4)                         */
/*---------------------------------------------------------------------------*/

/*
 * TURNAROUND and WRITELATENCY are the controller's count of bus cycles it
 * must idle after the address before read data appears, or before write data
 * may be driven.  They MUST match the device's MR0.RLC / MR4.WLC.
 *
 * TRANSCRIBED FROM THE VENDOR'S ARITHMETIC, NOT FROM ITS STRUCT INITIALISER
 * -- and that distinction cost a debugging session.  APMDDROctalMSPIConfig
 * declares `.ui8TurnAround = 6`, which is what a reader copies; but
 * am_devices_mspi_peripheral_init() then OVERWRITES it before use:
 *
 *     ui8TurnAround = (RLC_6 + 4) * 2                       [USE_APS51216BA]
 *     with the RLC enum starting at RLC_4 = 0 on this part, so RLC_6 = 2
 *     => (2 + 4) * 2 = 12
 *     if DQS is disabled:  (12 - 1) * 2 = 22
 *
 *     WriteLatency = wlc_to_lc(WLC_6) * 2 = 6 * 2 = 12      [both modes]
 *
 * With 6 instead of 12 the read window sits before the data and the DQS
 * strobe never lands inside it: the controller waits forever with BUSY set
 * and no error bit -- which is exactly how this first appeared.
 *
 * The default here is DQS mode.  M2 recomputes these when it programs MR0/MR4
 * for a higher clock; changing one side alone is the classic way to get a bus
 * that returns shifted data instead of failing.
 */
#define PSRAM_TURNAROUND_DQS     12u
#define PSRAM_TURNAROUND_NODQS   22u
#define PSRAM_BRINGUP_WRITELAT   12u

/*
 * M2: the device's latency codes, programmed to match the clock.  The device
 * powers up at RLC 6 (reads good to 133 MHz) and WLC 6 (writes good to
 * 109 MHz); faster clocks need higher codes in MR0/MR4, and the controller's
 * TURNAROUND / WRITELATENCY must move in lockstep: TURNAROUND = RLC * 2,
 * WRITELATENCY = WLC * 2 (the vendor's own arithmetic, verified against the
 * working example's register file).  The datasheet ceiling for this die is
 * 200 MHz, so the ladder tops out at 192.
 *
 *   clock    RLC (MR0[4:2] code)     WLC (MR4[7:5] code)
 *   48/96    6 (011, default)        6 (110, default)
 *   125      6 (011)                 7 (001)  -- WLC6 only reaches 109 MHz
 *   192      8 (101)                 9 (011)
 */
typedef struct { uint8_t rlc, rlc_code, wlc, wlc_code; } psram_lat_t;
/* THE DEVICE'S OWN DEFAULT DISAGREES WITH THE VENDOR COMMENT: MR4 reads back
 * 0x40 after reset = WLC code 010 = LC5, not the "LC6 default" the APS25616BA
 * driver comment claims for this family.  The bit-bang arbiter proved it:
 * with the controller at LC6 timing (12 edges) the write stream landed 2
 * bytes late; at 10 edges it landed exactly.  So the 48 MHz row keeps the
 * device's real default (WLC5 covers 66 MHz), and every row is PROGRAMMED,
 * never assumed. */
static const psram_lat_t s_lat[] = {
    { 6u, 0x3u, 5u, 0x2u },   /* 48 MHz  -- device power-up defaults        */
    { 6u, 0x3u, 6u, 0x6u },   /* 96 MHz  */
    { 6u, 0x3u, 7u, 0x1u },   /* 125 MHz */
    { 8u, 0x5u, 9u, 0x3u },   /* 192 MHz */
    { 8u, 0x5u, 9u, 0x3u },   /* 250 MHz: BEYOND THE DIE'S 200 MHz RATING --
                                 kept in the table so a deliberate overclock
                                 experiment is expressible, never a default */
};
static uint8_t s_asleep;      /**< 1 while the device is in half sleep      */
static uint8_t s_tap = 0xFFu; /**< shipped RXDQSDELAY tap (0xFF = unscanned) */
static uint8_t s_turnaround = PSRAM_TURNAROUND_DQS;   /* live values        */
static uint8_t s_writelat   = 10u;   /* matches the device's REAL power-up
                                        default, WLC5 -- see s_lat[]         */

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Optional step tracer.
 *
 * Bring-up on a dead bus can HANG rather than fail: a register write to a
 * peripheral whose clock is wrong stalls the bus with no fault and no output.
 * With a tracer installed, the last line printed names the step that wedged.
 */
static void (*s_trace)(const char *step);

static void trace(const char *step)
{
    if (s_trace) { s_trace(step); }
}

/*
 * Snapshot taken INSIDE a transfer, at the moments that distinguish causes.
 * A configuration that reads back perfect and still hangs needs evidence
 * from during the transfer, not more evidence from before it: whether the
 * FIFO word is consumed separates "the controller has no clock" from "the
 * controller is clocking and waiting on the device".
 */
static struct {
    uint32_t ctrl_after_start;
    uint32_t tx_after_write;    /* TXENTRIES right after the FIFO write     */
    uint32_t tx_settled;        /* TXENTRIES after a short delay            */
    uint32_t ctrl_settled;
    uint32_t intstat;
    uint32_t spins_left;
} s_dbg;

static uint8_t  s_up;        /**< 1 once init() completed                    */
static uint8_t  s_clk_idx;   /**< index into s_clk of the live setting       */
static uint8_t  s_faulted;   /**< 1 while fault injection is active           */
static uint8_t  s_nodqs;     /**< 1 to bring up WITHOUT the DQS strobe        */
static uint8_t  s_ta_override; /**< non-zero: use this TURNAROUND instead     */
static uint8_t  s_rxneg;       /**< DEV0CFG.RXNEG0 override                    */
static uint8_t  s_rxcap;       /**< DEV0CFG.RXCAP0 override                    */
static uint8_t  s_rxsmp = 1u;  /**< DEV0CFG1.RXSMP0 (vendor default 1)         */

/*---------------------------------------------------------------------------*/
/* PIO TRANSFER (table 2)                                                    */
/*---------------------------------------------------------------------------*/

/** Bound for any single PIO transfer, in poll iterations.  Generous: the
 *  point is to terminate, not to time. */
#define PSRAM_PIO_SPINS  400000u

/** MSPI FIFO depth in words (AM_HAL_MSPI_MAX_FIFO_SIZE). */
#define PSRAM_FIFO_WORDS 32u

/**
 * @brief One PIO command, optionally with an address and a data phase.
 *
 * Transcribed from am_hal_mspi_blocking_transfer's PIO path: INSTR and ADDR are
 * staged, then a single CTRL write with START launches it; RX data drains from
 * RXFIFO as RXENTRIES reports words; completion is CTRL.STATUS going to 1.
 *
 * @note INTEN is left alone -- this driver never enables MSPI interrupts, so
 *       there is nothing to save and restore.
 * @param instr    2-byte octal-DDR opcode
 * @param addr     device address (byte address, or MR number for reg access)
 * @param data     word buffer in/out, may be NULL when n_bytes is 0
 * @param n_bytes  data phase length
 * @param is_read  non-zero for RX (adds turnaround + write-latency enable)
 */
static tiku_psram_err_t psram_pio2(uint16_t instr, uint32_t addr,
                                   uint32_t *data, uint32_t n_bytes,
                                   int is_read, int wlat);

static tiku_psram_err_t psram_pio(uint16_t instr, uint32_t addr,
                                  uint32_t *data, uint32_t n_bytes,
                                  int is_read)
{
    /* Register commands: no write latency on TX (MR writes take data
     * immediately) -- proven by MR programming round-trips. */
    return psram_pio2(instr, addr, data, n_bytes, is_read, is_read ? 1 : 0);
}

static tiku_psram_err_t psram_pio2(uint16_t instr, uint32_t addr,
                                   uint32_t *data, uint32_t n_bytes,
                                   int is_read, int wlat)
{
    uint32_t ctrl = 0u;

    /* HARD GUARD, measured the hard way: a PIO command issued while the XIP
     * aperture is enabled deadlocks the controller's APB interface -- the
     * whole peripheral becomes unreadable and the first wedge of this
     * bring-up needed a physical power cycle.  PIO and XIP never mix. */
    if (MSPI0->DEV0XIP_b.XIPEN0 != 0u) {
        return TIKU_PSRAM_ERR_ARG;
    }
    /* FIFO traffic is in whole 32-bit words, but a transfer length need not
     * be a multiple of four -- the device reset carries a 2-byte payload.
     * TX must therefore round UP (a truncating divide sends nothing at all
     * while XFERBYTES still promises data, and the controller waits forever
     * -- the first bring-up's timeout); RX reads the whole words then takes
     * the leftover bytes from one final word. */
    uint32_t full_words = n_bytes / 4u;
    uint32_t leftover   = n_bytes - (full_words * 4u);
    uint32_t tx_words   = full_words + ((leftover != 0u) ? 1u : 0u);
    uint32_t spins;
    uint32_t i;

    MSPI0->INSTR = (uint32_t)instr;
    MSPI0->ADDR  = addr;

    ctrl |= (n_bytes << MSPI0_CTRL_XFERBYTES_Pos) & MSPI0_CTRL_XFERBYTES_Msk;
    ctrl |= MSPI0_CTRL_SENDI_Msk;      /* always send the opcode            */
    ctrl |= MSPI0_CTRL_SENDA_Msk;      /* octal DDR always sends an address */
    ctrl |= MSPI0_CTRL_START_Msk;
    if (is_read) {
        /* TXRX = 0 IS RECEIVE.  Bug #8 of this bring-up and the root cause
         * of both terminal symptoms: the vendor enum is AM_HAL_MSPI_RX = 0,
         * AM_HAL_MSPI_TX = 1, and this driver had it inverted -- so every
         * "read" was issued as a TRANSMIT (completes instantly, captures
         * nothing: the eternally-empty RX FIFO) and every "write" as a
         * RECEIVE (in DQS mode, waits forever for a strobe the device was
         * never asked to send: the eternal BUSY stall).  Found by halting
         * the vendor's own example after its reset command and seeing
         * CTRL.TXRX = 1 on a WRITE.  A read needs the bus turned around and
         * the write-latency count applied (vendor sets both). */
        ctrl |= MSPI0_CTRL_ENTURN_Msk;
        if (wlat) { ctrl |= MSPI0_CTRL_ENWLAT_Msk; }
    } else {
        ctrl |= (1u << MSPI0_CTRL_TXRX_Pos) & MSPI0_CTRL_TXRX_Msk;
        /* ARRAY writes must insert the device's write latency; the bit-bang
         * arbiter measured data landing 8 bytes early without it (physical
         * 0x4000 held byte index 8 of the stream).  Register writes pass
         * wlat=0: MRs take data immediately. */
        if (wlat) { ctrl |= MSPI0_CTRL_ENWLAT_Msk; }
    }

    /* NO FIFORESET HERE, deliberately, and it was tried: pulsing FIFORESET
     * before each command (its documented "manually flush the FIFO" use)
     * makes even the WRITE path hang -- the transfer state machine does not
     * survive it mid-stream.  Stale-FIFO risk is handled by draining after
     * completion instead. */
    MSPI0->INTCLR = 0xFFFFFFFFu;
    MSPI0->CTRL   = ctrl;
    s_dbg.ctrl_after_start = MSPI0->CTRL;

    if (is_read && data != (uint32_t *)0) {
        /* Drain AS DATA ARRIVES (the vendor's shape).  Historical note: an
         * earlier revision waited for completion first and drained after --
         * a workaround for RXENTRIES "never" filling, which was actually
         * bug #8's reads-issued-as-transmits.  With the direction right,
         * RXENTRIES tracks arrival normally -- and draining-as-you-go is
         * REQUIRED, not optional: a transfer larger than the 32-word FIFO
         * can only complete if the CPU keeps making room. */
        uint32_t total_words = full_words + ((leftover != 0u) ? 1u : 0u);
        for (i = 0u; i < total_words; i++) {
            uint32_t w;
            spins = PSRAM_PIO_SPINS;
            while (MSPI0->RXENTRIES == 0u && --spins != 0u) { }
            if (spins == 0u) {
                s_dbg.ctrl_settled = MSPI0->CTRL;
                s_dbg.intstat      = MSPI0->INTSTAT;
                s_dbg.spins_left   = 0u;
                return TIKU_PSRAM_ERR_TIMEOUT;
            }
            w = MSPI0->RXFIFO;
            if (i < full_words) {
                data[i] = w;
            } else {
                uint8_t *dst = (uint8_t *)&data[full_words];
                uint32_t b;
                for (b = 0u; b < leftover; b++) {
                    dst[b] = (uint8_t)(w >> (8u * b));
                }
            }
        }
    } else if (!is_read && data != (uint32_t *)0) {
        /* Write first, then wait for room -- the vendor's order.  Waiting
         * before the first write would stall on an empty FIFO's threshold. */
        for (i = 0u; i < tx_words; i++) {
            MSPI0->TXFIFO = data[i];
            if (i == 0u) {
                s_dbg.tx_after_write = MSPI0->TXENTRIES;
            }
            spins = PSRAM_PIO_SPINS;
            while (MSPI0->TXENTRIES >= PSRAM_FIFO_WORDS && --spins != 0u) { }
            if (spins == 0u) {
                return TIKU_PSRAM_ERR_TIMEOUT;
            }
        }
        tiku_cpu_ambiq_delay_us(20u);
        s_dbg.tx_settled   = MSPI0->TXENTRIES;
        s_dbg.ctrl_settled = MSPI0->CTRL;
    }

    /* CTRL.STATUS = 1 means the command finished. */
    spins = PSRAM_PIO_SPINS;
    while (((MSPI0->CTRL & MSPI0_CTRL_STATUS_Msk) == 0u) && --spins != 0u) { }
    s_dbg.spins_left = spins;
    s_dbg.intstat    = MSPI0->INTSTAT;
    if (spins == 0u) {
        return TIKU_PSRAM_ERR_TIMEOUT;
    }
    return TIKU_PSRAM_OK;
}

/*---------------------------------------------------------------------------*/
/* BRING-UP                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Table-1 step 1: power the controller domain, bounded wait. */
static tiku_psram_err_t psram_power_on(void)
{
    uint32_t spins = 100000u;

    PWRCTRL->DEVPWREN |= PWRCTRL_DEVPWREN_PWRENMSPI0_Msk;
    __DSB();
    while (((PWRCTRL->DEVPWRSTATUS & PWRCTRL_DEVPWRSTATUS_PWRSTMSPI0_Msk) == 0u)
           && --spins != 0u) { }
    return (spins != 0u) ? TIKU_PSRAM_OK : TIKU_PSRAM_ERR_POWER;
}

/** @brief Table-1 step 2: select and enable the MSPI0 IO clock.
 *
 * TWO STEPS, and the first is easy to miss: the oscillator block must be FORCED
 * ON before a peripheral can clock from it.  Without CLKGEN.MISC.FRCHFRC the
 * MSPI's IO clock branch has no source and every transfer times out. */
static tiku_psram_err_t psram_ioclk_on(uint8_t sel)
{
    uint32_t v;

    /* THE VENDOR'S CLKGEN.MISC STATE, replicated.  Breakpointing the
     * vendor's own example at its ID-read moment (the experiment that ended
     * this hunt) showed its DEV0* configuration essentially identical to
     * this one -- but CLKGEN.MISC = 0x08FBBFC1 against 0x08000021 here.  The
     * difference is the clock-gate-enable + power-on-clock chicken-bit block
     * that am_hal_pwrctrl_low_power_init() writes at vendor boot and this
     * port's bare-metal boot never has.  Replicated verbatim: bits 6-13 and 15-17
     * (PWRONCLKEN family), 19-23 (clock-gate enables, including the APB DMA
     * CPU clock gate), AXIXACLKENOVRRIDE (14) explicitly cleared, exactly as
     * the vendor leaves them. */
    {
        uint32_t misc = CLKGEN->MISC;
        misc |= 0x00FBBFC0u;
        misc &= ~(1u << 14);
        CLKGEN->MISC = misc;
        __DSB();
    }

    /* Force the oscillator block this source comes from.  Both are left on
     * afterwards: releasing them belongs to the M4 lifecycle verb, together
     * with the controller domain, not to a helper that only knows it needs a
     * clock right now. */
    if (sel == IOCLK_SEL_HFRC2_250MHZ) {
        CLKGEN->MISC |= CLKGEN_MISC_FRCHFRC2_Msk;
    } else {
        CLKGEN->MISC |= CLKGEN_MISC_FRCHFRC_Msk;
    }
    __DSB();

    v = CLKGEN->MSPIIOCLKCTRL;

    v &= ~CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKSEL_Msk;
    v |= ((uint32_t)sel << CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKSEL_Pos)
         & CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKSEL_Msk;
    CLKGEN->MSPIIOCLKCTRL = v;
    CLKGEN->MSPIIOCLKCTRL = v | CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKEN_Msk;
    __DSB();
    tiku_cpu_ambiq_delay_us(10u);      /* vendor's settle after the enable */

    /* A clock is trusted only after the enable is seen to have stuck --
     * the same discipline the STIMER reclock work forced on this port. */
    if ((CLKGEN->MSPIIOCLKCTRL & CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKEN_Msk) == 0u) {
        return TIKU_PSRAM_ERR_CLOCK;
    }
    return TIKU_PSRAM_OK;
}

/** @brief Table-1 steps 3-15: the controller, with the pads still GPIO. */
static void psram_controller_config(const psram_clk_t *c)
{
    uint32_t cfg;
    uint32_t turnaround = s_ta_override ? (uint32_t)s_ta_override
                                       : (s_nodqs ? PSRAM_TURNAROUND_NODQS
                                                  : (uint32_t)s_turnaround);

    /* Step 3: the SDR250 tap, before DEV0CFG so CLKDIV means what it says. */
    MSPI0->DEV0CFG1_b.SDR250EN0 = c->sdr250;

    /* Step 4+5: command format, clock divider, bus width, in ONE write --
     * DEV0CFG holds all of it and a read-modify-write per field would let
     * the controller see intermediate combinations. */
    /* Every field below uses the register header's own enum name.  Four of
     * these were wrong on the first attempt because they were derived from
     * the order values appear in vendor code instead of read from the
     * encoding table -- see the file header's note. */
    cfg  = ((uint32_t)MSPI0_DEV0CFG_ASIZE0_A4 << MSPI0_DEV0CFG_ASIZE0_Pos)
           & MSPI0_DEV0CFG_ASIZE0_Msk;
    cfg |= ((uint32_t)MSPI0_DEV0CFG_ISIZE0_I16 << MSPI0_DEV0CFG_ISIZE0_Pos)
           & MSPI0_DEV0CFG_ISIZE0_Msk;
    cfg |= (turnaround << MSPI0_DEV0CFG_TURNAROUND0_Pos)
           & MSPI0_DEV0CFG_TURNAROUND0_Msk;
    cfg |= ((uint32_t)s_writelat << MSPI0_DEV0CFG_WRITELATENCY0_Pos)
           & MSPI0_DEV0CFG_WRITELATENCY0_Msk;
    cfg |= ((uint32_t)c->clkdiv << MSPI0_DEV0CFG_CLKDIV0_Pos)
           & MSPI0_DEV0CFG_CLKDIV0_Msk;
    /* SPI mode 0: CPOL = CPHA = 0.  RXNEG = RXCAP = 0 at every speed per
     * the vendor; TXNEG comes from the clock row.  The RX knobs are
     * overridable because the capture point is being HUNTED -- the device is
     * proven alive (bit-bang reads MR1=0x8D) while the controller captures
     * nothing, so the wrongness is in these bits or their DEV0CFG1 cousins. */
    if (c->txneg) { cfg |= MSPI0_DEV0CFG_TXNEG0_Msk; }
    if (s_rxneg)  { cfg |= MSPI0_DEV0CFG_RXNEG0_Msk; }
    if (s_rxcap)  { cfg |= MSPI0_DEV0CFG_RXCAP0_Msk; }
    cfg |= ((uint32_t)MSPI0_DEV0CFG_DEVCFG0_OCTAL0 << MSPI0_DEV0CFG_DEVCFG0_Pos)
           & MSPI0_DEV0CFG_DEVCFG0_Msk;
    MSPI0->DEV0CFG = cfg;                        /* SEPIO0 left 0: shared IO */

    /* Step 6: DDR emulation. */
    MSPI0->DEV0DDR_b.EMULATEDDR0 = 1u;

    /* Step 7: the controller owns 8 data + clock + DQS. */
    MSPI0->PADOUTEN = MSPI0_PADOUTEN_OUTEN_OCTAL;

    /* Step 8: opcodes for the XIP/DMA read and write paths. */
    MSPI0->DEV0INSTR =
        (((uint32_t)PSRAM_CMD_READ << MSPI0_DEV0INSTR_READINSTR0_Pos)
          & MSPI0_DEV0INSTR_READINSTR0_Msk) |
        (((uint32_t)PSRAM_CMD_WRITE << MSPI0_DEV0INSTR_WRITEINSTR0_Pos)
          & MSPI0_DEV0INSTR_WRITEINSTR0_Msk);

    /* Step 9+10: XIP framing.  Programmed now even though the aperture is
     * not enabled until M3 -- the fields live in the same register as the
     * mixed-mode select and the vendor programs them together. */
    MSPI0->DEV0XIP_b.XIPMIXED0       = 0u;   /* NORMAL for octal DDR        */
    MSPI0->DEV0XIP_b.XIPACK0         = MSPI0_DEV0XIP_XIPACK0_TERMINATE;
    MSPI0->DEV0XIP_b.XIPSENDA0       = 1u;
    MSPI0->DEV0XIP_b.XIPSENDI0       = 1u;
    MSPI0->DEV0XIP_b.XIPENTURN0      = 1u;
    MSPI0->DEV0XIP_b.XIPTURNAROUND0  = turnaround;
    MSPI0->DEV0XIP_b.XIPENWLAT0      = 1u;
    MSPI0->DEV0XIP_b.XIPWRITELATENCY0 = s_writelat;

    /* Step 11: 1 KB DMA boundary -- the device's row boundary.
     *
     * DMATIMELIMIT stays at the vendor's 40, and the A/B that decided it is
     * worth keeping: DMA throughput clamps at ~50 MB/s per KB-boundary
     * (clock-independent: 96 and 192 MHz within 10 %; chunk-independent:
     * 16 KB = 64 KB), i.e. ~17 us of per-kilobyte machinery.  Suspecting a
     * pause knob, TIMELIMIT=2 was tried: dma-write COLLAPSED 58x and an
     * integrity leg failed -- the field is a CE-window limit, and small
     * values fragment every burst into command-overhead confetti.  40 is
     * the proven setting; the per-KB cost is an accepted open question for
     * the CQ path (the vendor's own bandwidth example uses the command
     * queue, not plain DMA). */
    MSPI0->DEV0BOUNDARY_b.DMABOUND0     = MSPI0_DEV0BOUNDARY_DMABOUND0_BREAK1K;
    MSPI0->DEV0BOUNDARY_b.DMATIMELIMIT0 = 40u;

    /* Step 12: DQS receive -- delay-line taps FROM SILICON, NOT FROM THE SDK
     * SOURCE.  The device driver's struct says TxDQSDelay=0 / RxDQSDelay=16,
     * but the vendor's own prebuilt example, halted on THIS board after a
     * successful bring-up, reads back DEV0DDR = 0x4945: TXDQSDELAY = 10,
     * RXDQSDELAY = 18.  The datasheet (16.4.3) says TX taps delay the output
     * SCLK relative to output data -- with 0 taps the clock edge lands on the
     * data transition at the device, which matches the one-edge-off TX the
     * bit-bang loopback measured.  Struct initialisers have lied twice now
     * (TURNAROUND was the other); registers read back from working silicon
     * do not.
     *
     * DQS off remains a DIAGNOSTIC mode only: the datasheet's timing chapters
     * define DDR receive solely as "DDR with DQS", and the 176-cell capture
     * sweep confirmed non-DQS DDR never captures on this part. */
    MSPI0->DEV0DDR_b.ENABLEDQS0       = s_nodqs ? 0u : 1u;
    MSPI0->DEV0DDR_b.DQSSYNCNEG0      = 0u;
    MSPI0->DEV0DDR_b.ENABLEFINEDELAY0 = 0u;
    /* Octal-phase values from the breakpoint dump (DEV0DDR = 0x4005 at the
     * vendor's own ID read): TX 0, RX 16.  The 10/18 pair seen in the final
     * dump belongs to the later hex phase. */
    MSPI0->DEV0DDR_b.TXDQSDELAY0      = 0u;
    MSPI0->DEV0DDR_b.RXDQSDELAY0      = 16u;
    MSPI0->DEV0DDR_b.RXDQSDELAYNEG0   = 0u;
    MSPI0->DEV0DDR_b.RXDQSDELAYNEGEN0 = 0u;
    MSPI0->DEV0DDR_b.RXDQSDELAYHI0    = 0u;
    MSPI0->DEV0DDR_b.RXDQSDELAYNEGHI0 = 0u;
    MSPI0->DEV0DDR_b.RXDQSDELAYHIEN0  = 0u;
    MSPI0->DEV0DDRDLYEXT_b.RXDQS0PDLYEXT0 = 0u;
    MSPI0->DEV0DDRDLYEXT_b.RXDQS0NDLYEXT0 = 0u;

    /* Step 13: RX sampling.  Vendor values for this device; not guesses,
     * and not tunable knobs until something measures them. */
    MSPI0->DEV0CFG1_b.DQSTURN0   = 2u;
    MSPI0->DEV0CFG1_b.RXSMP0     = s_rxsmp;
    MSPI0->DEV0CFG1_b.TAFOURTH0  = 1u;
    MSPI0->DEV0CFG1_b.SFTURN0    = 10u;
    MSPI0->DEV0CFG1_b.RXHI0      = 0u;
    MSPI0->DEV0CFG1_b.HYPERIO0   = 0u;
    MSPI0->DEV0CFG1_b.RBX0       = 0u;
    MSPI0->DEV0CFG1_b.WBX0       = 0u;
    MSPI0->DEV0CFG1_b.SCLKRXHALT0 = 0u;
    MSPI0->DEV0CFG1_b.RXCAPEXT0  = 0u;

    /* Step 14: FIFO threshold + DMA burst sizing.  DMABCOUNT was missing
     * from the first DMA bring-up (the vendor sets it only on its CQ path);
     * 32 is its value for every speed class. */
    MSPI0->THRESHOLD_b.RXTHRESH = 30u;
    MSPI0->DMABCOUNT            = 32u;
    MSPI0->DMATHRESH_b.DMATXTHRESH = 32u - 4u;
    MSPI0->DMATHRESH_b.DMARXTHRESH = 8u;

    /* Step 15: no IOM is bridged through this controller.
     *
     * DISABLED is 15 ("No IOM selected. Signals always zero").  The vendor
     * HAL writes 7 here via its own AM_HAL_MSPI_LINK_NONE constant, but 7 is
     * IOM7 in this register's encoding; the header's DISABLED is what the
     * silicon documents, so that is what this port writes.  Getting this
     * wrong is not cosmetic: 6 selects IOM6, which on this package is the
     * internal SPI-HCI link to the EM9305 radio die. */
    MSPI0->MSPICFG_b.IOMSEL = MSPI0_MSPICFG_IOMSEL_DISABLED;
    MSPI0->MSPICFG_b.APBCLK = MSPI0_MSPICFG_APBCLK_DIS;
    __DSB();
}

/** @brief Table-1 step 16: hand the pads to MSPI0. */
static void psram_pads_config(void)
{
    uint32_t pad;

    for (pad = PSRAM_PAD_D0; pad <= PSRAM_PAD_DQS; pad++) {
        tiku_ambiq_gpio_pad_config(pad, PAD_CFG_MSPI_IO);
    }
    tiku_ambiq_gpio_pad_config(PSRAM_PAD_CE, PAD_CFG_MSPI_CE);
}

tiku_psram_err_t tiku_psram_init(unsigned clk)
{
    tiku_psram_err_t rc;

    if (clk >= PSRAM_CLK_COUNT) {
        return TIKU_PSRAM_ERR_ARG;
    }

    trace("power");
    /* init() ends in a DEVICE RESET, which restores the device's power-up
     * latencies (RLC6 / WLC5) -- so the controller's live latency state must
     * be restored to match, whatever a previous set_speed() left behind.
     * Found by the retention gate failing 100 % after a 192 MHz session:
     * stale WLC9 timing against a freshly-reset WLC5 device shifts every
     * write.  Speed changes go through tiku_psram_set_speed(), which
     * programs BOTH sides. */
    s_turnaround = PSRAM_TURNAROUND_DQS;   /* RLC6 * 2 */
    s_writelat   = 10u;                    /* WLC5 * 2 -- the real default  */

    rc = psram_power_on();
    if (rc != TIKU_PSRAM_OK) {
        return rc;
    }
    trace("ioclk");
    rc = psram_ioclk_on(s_clk[clk].ioclk_sel);
    if (rc != TIKU_PSRAM_OK) {
        /* Fail closed: do not leave a powered controller with no clock. */
        PWRCTRL->DEVPWREN &= ~PWRCTRL_DEVPWREN_PWRENMSPI0_Msk;
        return rc;
    }

    trace("controller");
    psram_controller_config(&s_clk[clk]);
    trace("pads");
    psram_pads_config();
    tiku_cpu_ambiq_delay_us(150u);     /* step 17: vendor's settle          */

    s_clk_idx = (uint8_t)clk;
    s_up      = 1u;

    trace("device-reset");
    rc = tiku_psram_device_reset();     /* step 18                           */
    if (rc != TIKU_PSRAM_OK) {
        s_up = 0u;
        return rc;
    }
    return TIKU_PSRAM_OK;
}

void tiku_psram_deinit(void)
{
    CLKGEN->MSPIIOCLKCTRL &= ~CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKEN_Msk;
    PWRCTRL->DEVPWREN &= ~PWRCTRL_DEVPWREN_PWRENMSPI0_Msk;
    __DSB();
    s_up = 0u;
}

int tiku_psram_powered(void)
{
    return ((PWRCTRL->DEVPWRSTATUS & PWRCTRL_DEVPWRSTATUS_PWRSTMSPI0_Msk) != 0u)
           ? 1 : 0;
}

unsigned long tiku_psram_clock_hz(void)
{
    return s_up ? (unsigned long)s_clk[s_clk_idx].hz : 0uL;
}

/*---------------------------------------------------------------------------*/
/* DEVICE ACCESS                                                             */
/*---------------------------------------------------------------------------*/

tiku_psram_err_t tiku_psram_device_reset(void)
{
    uint32_t dummy = 0u;
    tiku_psram_err_t rc;

    if (!s_up) {
        return TIKU_PSRAM_ERR_POWER;
    }
    /* The reset opcode carries a 2-byte dummy payload (vendor). */
    rc = psram_pio(PSRAM_CMD_GLOBAL_RESET, 0u, &dummy, 2u, 0);
    if (rc != TIKU_PSRAM_OK) {
        return rc;
    }
    tiku_cpu_ambiq_delay_us(2u);
    return TIKU_PSRAM_OK;
}

tiku_psram_err_t tiku_psram_reg_read(uint32_t mr, uint32_t *out)
{
    uint32_t raw = 0u;
    tiku_psram_err_t rc;

    if (!s_up)  { return TIKU_PSRAM_ERR_POWER; }
    if (!out)   { return TIKU_PSRAM_ERR_ARG; }
    rc = psram_pio(PSRAM_CMD_REG_READ, mr, &raw, 4u, 1);
    *out = raw;
    return rc;
}

tiku_psram_err_t tiku_psram_reg_write(uint32_t mr, uint32_t val)
{
    uint32_t v = val;

    if (!s_up) { return TIKU_PSRAM_ERR_POWER; }
    return psram_pio(PSRAM_CMD_REG_WRITE, mr, &v, 4u, 0);
}

/** @brief Decode MR2.DENSITY into bytes; 0 for an unrecognised code. */
static uint32_t psram_density_bytes(uint8_t code)
{
    switch (code) {
        case 0x1u: return  4u * 1024u * 1024u;   /*  32 Mbit */
        case 0x3u: return  8u * 1024u * 1024u;   /*  64 Mbit */
        case 0x5u: return 16u * 1024u * 1024u;   /* 128 Mbit */
        case 0x7u: return 32u * 1024u * 1024u;   /* 256 Mbit */
        case 0x6u: return 64u * 1024u * 1024u;   /* 512 Mbit -- U14 */
        default:   return 0u;
    }
}

tiku_psram_err_t tiku_psram_read_id(tiku_psram_id_t *out)
{
    tiku_psram_id_t id;
    uint32_t raw;
    tiku_psram_err_t rc;

    if (!s_up) { return TIKU_PSRAM_ERR_POWER; }

    /* Zero first: a partially filled report is worse than an empty one. */
    id.mr0 = 0u; id.mr1 = 0u; id.mr2 = 0u; id.mr3 = 0u;
    id.mr4 = 0u; id.mr8 = 0u;
    id.vendor_id = 0u; id.density_code = 0u; id.generation = 0u;
    id.good_die = 0u; id.size_bytes = 0u;

    rc = tiku_psram_reg_read(1u, &raw);
    if (rc != TIKU_PSRAM_OK) { goto done; }
    id.mr1 = (uint8_t)raw;

    /* Address 2 returns MR2 in byte 0 and MR3 in byte 1. */
    rc = tiku_psram_reg_read(2u, &raw);
    if (rc != TIKU_PSRAM_OK) { goto done; }
    id.mr2 = (uint8_t)raw;
    id.mr3 = (uint8_t)(raw >> 8);

    rc = tiku_psram_reg_read(0u, &raw);
    if (rc != TIKU_PSRAM_OK) { goto done; }
    id.mr0 = (uint8_t)raw;

    rc = tiku_psram_reg_read(4u, &raw);
    if (rc != TIKU_PSRAM_OK) { goto done; }
    id.mr4 = (uint8_t)raw;

    rc = tiku_psram_reg_read(8u, &raw);
    if (rc != TIKU_PSRAM_OK) { goto done; }
    id.mr8 = (uint8_t)raw;

    id.vendor_id    = (uint8_t)(id.mr1 & 0x1Fu);
    id.density_code = (uint8_t)(id.mr2 & 0x07u);
    id.generation   = (uint8_t)((id.mr2 >> 3) & 0x03u);
    id.good_die     = (uint8_t)((((id.mr2 >> 5) & 0x07u) == PSRAM_GB_PASS)
                                ? 1u : 0u);
    id.size_bytes   = psram_density_bytes(id.density_code);

    /* THE GATE.  All three must hold; a plausible-looking wrong answer is
     * the failure mode this check exists for. */
    if (id.vendor_id != PSRAM_VID_AP_MEMORY ||
        id.density_code != PSRAM_DENSITY_512MBIT ||
        id.good_die == 0u) {
        rc = TIKU_PSRAM_ERR_ID;
    }

done:
    if (out) {
        *out = id;      /* report the raw bytes even on failure */
    }
    return rc;
}

/*---------------------------------------------------------------------------*/
/* M2 -- MEMORY ACCESS (PIO), SPEED, TIMING SCAN                             */
/*---------------------------------------------------------------------------*/

/* One PIO transfer is bounded by the FIFO and the device's 1 KB row: chunk
 * bulk access at 256 B, well inside both.  With the direction bit finally
 * right, RXENTRIES tracks arrival and the vendor's poll-as-you-drain shape
 * works; TX paces on FIFO fullness the same way. */
#define PSRAM_CHUNK 256u

tiku_psram_err_t tiku_psram_mem_read(uint32_t addr, void *buf, uint32_t n)
{
    uint8_t *dst = (uint8_t *)buf;
    if (!s_up) { return TIKU_PSRAM_ERR_POWER; }
    while (n != 0u) {
        uint32_t chunk = (n > PSRAM_CHUNK) ? PSRAM_CHUNK : n;
        uint32_t words[PSRAM_CHUNK / 4u];
        tiku_psram_err_t rc = psram_pio2(PSRAM_CMD_READ, addr, words, chunk,
                                         1, 1);
        if (rc != TIKU_PSRAM_OK) { return rc; }
        {
            uint32_t b;
            for (b = 0u; b < chunk; b++) {
                dst[b] = (uint8_t)(words[b / 4u] >> (8u * (b & 3u)));
            }
        }
        dst  += chunk;
        addr += chunk;
        n    -= chunk;
    }
    return TIKU_PSRAM_OK;
}

tiku_psram_err_t tiku_psram_mem_write(uint32_t addr, const void *buf, uint32_t n)
{
    const uint8_t *src = (const uint8_t *)buf;
    if (!s_up) { return TIKU_PSRAM_ERR_POWER; }
    while (n != 0u) {
        uint32_t chunk = (n > PSRAM_CHUNK) ? PSRAM_CHUNK : n;
        uint32_t words[PSRAM_CHUNK / 4u];
        uint32_t b;
        for (b = 0u; b < ((chunk + 3u) / 4u); b++) { words[b] = 0u; }
        for (b = 0u; b < chunk; b++) {
            words[b / 4u] |= ((uint32_t)src[b]) << (8u * (b & 3u));
        }
        {
            tiku_psram_err_t rc =
                psram_pio2(PSRAM_CMD_WRITE, addr, words, chunk, 0, 1);
            if (rc != TIKU_PSRAM_OK) { return rc; }
        }
        src  += chunk;
        addr += chunk;
        n    -= chunk;
    }
    return TIKU_PSRAM_OK;
}

/**
 * @brief Program the device's MR0/MR4 latency codes for clock row @p clk.
 *
 * Must run at a clock the CURRENT codes support (i.e., before raising the
 * clock).  Read-back verifies the write landed -- an MR write is the one
 * operation whose failure would otherwise surface as a mistimed bus later.
 */
static tiku_psram_err_t psram_program_latency(unsigned clk)
{
    const psram_lat_t *L = &s_lat[clk];
    uint32_t v;
    tiku_psram_err_t rc;

    rc = tiku_psram_reg_read(0u, &v);
    if (rc != TIKU_PSRAM_OK) { return rc; }
    v = (v & ~0x1Cu) | ((uint32_t)L->rlc_code << 2);
    rc = tiku_psram_reg_write(0u, v & 0xFFu);
    if (rc != TIKU_PSRAM_OK) { return rc; }

    rc = tiku_psram_reg_read(4u, &v);
    if (rc != TIKU_PSRAM_OK) { return rc; }
    v = (v & ~0xE0u) | ((uint32_t)L->wlc_code << 5);
    rc = tiku_psram_reg_write(4u, v & 0xFFu);
    if (rc != TIKU_PSRAM_OK) { return rc; }

    /* Controller-side counterparts take effect at the next init.  wlc*2
     * exactly -- the earlier "-2 calibration" was compensating for assuming
     * WLC6 while the device actually defaults to WLC5 (see the table). */
    s_turnaround = (uint8_t)(L->rlc * 2u);
    s_writelat   = (uint8_t)(L->wlc * 2u);

    /* Verify with the OLD timing (register reads still honour the newly
     * programmed RLC only after... the device applies MRs immediately, so
     * re-read with the new turnaround after reinit -- done by the caller's
     * identity gate, not here). */
    return TIKU_PSRAM_OK;
}

tiku_psram_err_t tiku_psram_set_speed(unsigned clk)
{
    tiku_psram_err_t rc;

    if (clk >= PSRAM_CLK_COUNT) { return TIKU_PSRAM_ERR_ARG; }

    /* Sequence: at a known-good clock, program the device MRs for the
     * TARGET clock; then reconfigure the controller at the target with the
     * matching turnaround -- WITHOUT a device reset, which would restore
     * default MRs and undo step one. */
    if (!s_up) {
        s_turnaround = PSRAM_TURNAROUND_DQS;
        s_writelat   = PSRAM_BRINGUP_WRITELAT;
        rc = tiku_psram_init(TIKU_PSRAM_CLK_48MHZ);
        if (rc != TIKU_PSRAM_OK) { return rc; }
    }
    rc = psram_program_latency(clk);
    if (rc != TIKU_PSRAM_OK) { return rc; }

    /* Reconfigure controller only: domain stays up, device keeps its MRs. */
    psram_controller_config(&s_clk[clk]);
    rc = psram_ioclk_on(s_clk[clk].ioclk_sel);
    if (rc != TIKU_PSRAM_OK) { return rc; }
    tiku_cpu_ambiq_delay_us(10u);
    s_clk_idx = (uint8_t)clk;
    return TIKU_PSRAM_OK;
}

/**
 * @brief One timing-scan cell: pattern-verify @p bytes at @p rxdqs delay.
 *
 * Address-in-address plus a lane-exercising constant, split across two
 * regions (one low, one past 32 MB so the high address bits are proven).
 * Returns 1 on bit-exact readback, 0 on any mismatch or transfer error.
 */
static int psram_scan_cell(unsigned rxdqs, uint32_t bytes)
{
    static uint8_t wr[512], rd[512];
    static const uint32_t base[2] = { 0x00001000u, 0x02000000u + 0x1000u };
    uint32_t r, i, off;

    MSPI0->DEV0DDR_b.RXDQSDELAY0 = (rxdqs & 0x1Fu);
    __DSB();

    for (r = 0u; r < 2u; r++) {
        for (off = 0u; off < bytes; off += (uint32_t)(sizeof wr)) {
            uint32_t chunk = (uint32_t)(sizeof wr);
            for (i = 0u; i < chunk; i++) {
                uint32_t a = base[r] + off + i;
                wr[i] = (uint8_t)(a ^ (a >> 8) ^ (a >> 16) ^ 0xA5u);
            }
            if (tiku_psram_mem_write(base[r] + off, wr, chunk)
                    != TIKU_PSRAM_OK) { return 0; }
            for (i = 0u; i < chunk; i++) { rd[i] = 0u; }
            if (tiku_psram_mem_read(base[r] + off, rd, chunk)
                    != TIKU_PSRAM_OK) { return 0; }
            for (i = 0u; i < chunk; i++) {
                if (rd[i] != wr[i]) { return 0; }
            }
        }
    }
    return 1;
}

uint32_t tiku_psram_timing_scan(uint32_t *pass_mask, unsigned *center)
{
    uint32_t mask = 0u;
    unsigned tap, best_len = 0u, best_start = 0u, run = 0u, run_start = 0u;

    if (!s_up) { return 0u; }
    for (tap = 0u; tap < 32u; tap++) {
        if (psram_scan_cell(tap, 2048u)) {
            mask |= (1u << tap);
            if (run == 0u) { run_start = tap; }
            run++;
            if (run > best_len) { best_len = run; best_start = run_start; }
        } else {
            run = 0u;
        }
    }
    /* Ship the centre of the widest passing window; restore it live. */
    if (best_len != 0u) {
        unsigned c = best_start + best_len / 2u;
        MSPI0->DEV0DDR_b.RXDQSDELAY0 = (c & 0x1Fu);
        __DSB();
        s_tap = (uint8_t)c;
        if (center) { *center = c; }
    } else if (center) {
        *center = 0u;
    }
    if (pass_mask) { *pass_mask = mask; }
    return best_len;
}

/*---------------------------------------------------------------------------*/
/* M3 -- XIP APERTURE + DMA                                                  */
/*---------------------------------------------------------------------------*/

tiku_psram_err_t tiku_psram_xip_enable(int enable)
{
    if (!s_up) { return TIKU_PSRAM_ERR_POWER; }
    if (enable) {
        /* Aperture: base 0x60000000, 64 MB, read-write.  BASE0 encodes bits
         * 28:16 of the offset within the region -- zero for the region start
         * (verified against the working example: DEV0AXI reads 0x0000000A). */
        MSPI0->DEV0AXI =
            ((10u << MSPI0_DEV0AXI_SIZE0_Pos) & MSPI0_DEV0AXI_SIZE0_Msk);
        __DSB();
        MSPI0->DEV0XIP_b.XIPEN0 = 1u;
    } else {
        /* CLEAN AND INVALIDATE THE D-CACHE BEFORE THE APERTURE GOES AWAY.
         * The aperture is write-back cacheable, and a session that staged a
         * model has megabytes of its writes sitting as dirty lines.  Disable
         * the aperture with those still resident and they evict later, under
         * whatever code happens to be running -- an IMPRECISE bus fault with
         * a misleading PC.  That was the `reboot` hardfault at psram_pio2
         * (cfsr=0x400) and the wedge after `power psram down`; one
         * mechanism, two symptoms.  Whole-cache by set/way: 64 KB of cache
         * against 64 MB of aperture makes by-address the wrong tool. */
        __DSB();
        SCB_CleanInvalidateDCache();
        MSPI0->DEV0XIP_b.XIPEN0 = 0u;
    }
    __DSB();
    return TIKU_PSRAM_OK;
}

int tiku_psram_xip_enabled(void)
{
    return (s_up && MSPI0->DEV0XIP_b.XIPEN0 != 0u) ? 1 : 0;
}

/**
 * @brief Blocking DMA transfer between SRAM and the device.
 *
 * The plain DMA engine (not the command queue): target address, device
 * address, count, direction, enable, poll DMACPL.  Cache coherency is the
 * CALLER's job -- this moves bytes between the device and physical SRAM.
 */
/*
 * Split form of the transfer below: arm it, go and do something else, then
 * collect it.
 *
 * The blocking call polls with a 20 us backoff, so a caller that streams a
 * weight matrix spends the whole transfer idle. Splitting start from wait
 * lets the CPU work on the previous slice while the current one lands.
 */
static uint32_t s_dma_busy;

tiku_psram_err_t tiku_psram_dma_start(uint32_t dev_addr, void *sram,
                                      uint32_t n, int to_device)
{
    if (!s_up)              { return TIKU_PSRAM_ERR_POWER; }
    if (s_dma_busy)         { return TIKU_PSRAM_ERR_ARG; }
    if (MSPI0->DEV0XIP_b.XIPEN0 != 0u) { return TIKU_PSRAM_ERR_ARG; }
    if (n == 0u || (n & 3u) != 0u || ((uint32_t)(uintptr_t)sram & 3u) != 0u) {
        return TIKU_PSRAM_ERR_ARG;
    }
    MSPI0->DMATARGADDR = (uint32_t)(uintptr_t)sram;
    MSPI0->DMADEVADDR  = dev_addr;
    MSPI0->DMATOTCOUNT = n;
    MSPI0->INTCLR      = 0xFFFFFFFFu;
    MSPI0->DMACFG =
        ((uint32_t)MSPI0_DMACFG_DMAEN_EN << MSPI0_DMACFG_DMAEN_Pos) |
        ((to_device ? 1u : 0u) << MSPI0_DMACFG_DMADIR_Pos);
    __DSB();
    s_dma_busy = 1u;
    return TIKU_PSRAM_OK;
}

/**
 * @brief Collect a transfer armed by tiku_psram_dma_start().
 *
 * @note Spins without a backoff: the caller has already done its work and
 *       any sleep here is pure added latency.
 */
tiku_psram_err_t tiku_psram_dma_wait(void)
{
    uint32_t spins = 40000000u;
    uint32_t st;

    if (!s_dma_busy) { return TIKU_PSRAM_OK; }
    while (((MSPI0->DMASTAT &
             (MSPI0_DMASTAT_DMACPL_Msk | MSPI0_DMASTAT_DMAERR_Msk)) == 0u)
           && --spins != 0u) {
        __NOP();
    }
    st = MSPI0->DMASTAT;
    MSPI0->DMACFG  = 0u;
    MSPI0->DMASTAT = 0u;
    s_dma_busy     = 0u;
    if (spins == 0u) { return TIKU_PSRAM_ERR_TIMEOUT; }
    if ((st & MSPI0_DMASTAT_DMAERR_Msk) != 0u) { return TIKU_PSRAM_ERR_TIMEOUT; }
    return TIKU_PSRAM_OK;
}

tiku_psram_err_t tiku_psram_dma(uint32_t dev_addr, void *sram, uint32_t n,
                                int to_device)
{
    uint32_t spins = 500000u;   /* x20 us = 10 s ceiling */

    if (!s_up)              { return TIKU_PSRAM_ERR_POWER; }
    if (MSPI0->DEV0XIP_b.XIPEN0 != 0u) { return TIKU_PSRAM_ERR_ARG; }
    if (n == 0u || (n & 3u) != 0u || ((uint32_t)(uintptr_t)sram & 3u) != 0u) {
        return TIKU_PSRAM_ERR_ARG;
    }

    MSPI0->DMATARGADDR = (uint32_t)(uintptr_t)sram;
    MSPI0->DMADEVADDR  = dev_addr;
    MSPI0->DMATOTCOUNT = n;
    MSPI0->INTCLR      = 0xFFFFFFFFu;
    MSPI0->DMACFG =
        ((uint32_t)MSPI0_DMACFG_DMAEN_EN << MSPI0_DMACFG_DMAEN_Pos) |
        ((to_device ? 1u : 0u) << MSPI0_DMACFG_DMADIR_Pos);
    __DSB();

    /* Backoff poll -- same reason as the CQ wait: the tight spin WAS the
     * bandwidth plateau. */
    while (((MSPI0->DMASTAT &
             (MSPI0_DMASTAT_DMACPL_Msk | MSPI0_DMASTAT_DMAERR_Msk)) == 0u)
           && --spins != 0u) {
        tiku_cpu_ambiq_delay_us(20u);
    }
    {
        uint32_t st = MSPI0->DMASTAT;
        MSPI0->DMACFG  = 0u;
        MSPI0->DMASTAT = 0u;
        if (spins == 0u)                        { return TIKU_PSRAM_ERR_TIMEOUT; }
        if ((st & MSPI0_DMASTAT_DMAERR_Msk))    { return TIKU_PSRAM_ERR_TIMEOUT; }
    }
    return TIKU_PSRAM_OK;
}

/*---------------------------------------------------------------------------*/
/* M3.5 -- COMMAND QUEUE: hardware-chained DMA segments                      */
/*---------------------------------------------------------------------------*/

/*
 * TABLE 3 -- THE CQ ENTRY (transcribed from am_hal_mspi.c's
 * am_hal_mspi_cq_dma_entry_t and the am_hal_cmdq engine):
 *
 * The CQ hardware fetches 8-byte {register-address, value} pairs from SRAM
 * at CQADDR and performs each as a register write.  Two special behaviours
 * make chained DMA work with no CPU in the seams:
 *
 *   1. A write to DMACFG while a DMA is in progress STALLS THE ENGINE until
 *      that DMA completes -- so the vendor's per-segment tail write of
 *      DMAEN=0 is simultaneously the completion wait and the teardown, and
 *      the next segment's writes follow with no software involvement.
 *   2. CQPAUSE holds a condition mask evaluated against CQFLAGS; the mask
 *      bit CQIDX ("CURIDX == ENDIDX") makes the engine pause exactly when
 *      it runs out of posted work.  A queue-borne write to CQCURIDX is how
 *      a block marks its own retirement.
 *
 * One segment, verbatim from the vendor (8 pairs, 64 bytes):
 *      CQPAUSE    := pause mask (IDX)      DMATARGADDR := sram
 *      CQPAUSE    := pause mask (IDX)      DMADEVADDR  := device addr
 *      DMATOTCOUNT:= bytes                 DMACFG      := DIR|PRI|EN=3
 *      DMACFG     := EN=0   <-- the stall  CQSETCLEAR  := 0
 * and the block terminator: { CQCURIDX, n_segments }.
 *
 * WHY THIS EXISTS: plain DMA measured ~50 MB/s with a ~17 us per-kilobyte
 * cost that is clock- and chunk-independent -- CPU-visible seams.  This
 * engine is the vendor's only bulk path and removes every seam.
 */

#define CQ_PAIRS_PER_SEG   8u
#define CQ_MAX_SEGS        66u
/* 66 segs * 8 pairs + terminator, 8 B per pair */
static uint32_t s_cq[(CQ_MAX_SEGS * CQ_PAIRS_PER_SEG + 1u) * 2u]
    __attribute__((section(".ssram"), aligned(32)));

#define CQ_PAUSE_IDX_MASK  0x4000u   /* CQFLAGS.CQIDX: pause when no work */

tiku_psram_err_t tiku_psram_cq_xfer(uint32_t dev_addr, void *sram,
                                    uint32_t total, uint32_t seg_bytes,
                                    int to_device)
{
    uint32_t n_segs, i, w = 0u;
    uint32_t spins = 200000u;   /* x50 us = 10 s ceiling */
    uint8_t *sp = (uint8_t *)sram;

    if (!s_up)                          { return TIKU_PSRAM_ERR_POWER; }
    if (MSPI0->DEV0XIP_b.XIPEN0 != 0u)  { return TIKU_PSRAM_ERR_ARG; }
    if (seg_bytes == 0u || (total % seg_bytes) != 0u ||
        (seg_bytes & 3u) != 0u)         { return TIKU_PSRAM_ERR_ARG; }
    n_segs = total / seg_bytes;
    if (n_segs == 0u || n_segs > CQ_MAX_SEGS) { return TIKU_PSRAM_ERR_ARG; }

    /* Build the queue: one vendor-shaped segment per chunk. */
    for (i = 0u; i < n_segs; i++) {
        uint32_t cfg_on =
            ((to_device ? 1u : 0u) << MSPI0_DMACFG_DMADIR_Pos) |
            ((uint32_t)MSPI0_DMACFG_DMAEN_EN << MSPI0_DMACFG_DMAEN_Pos);
        s_cq[w++] = (uint32_t)&MSPI0->CQPAUSE;     s_cq[w++] = CQ_PAUSE_IDX_MASK;
        s_cq[w++] = (uint32_t)&MSPI0->CQPAUSE;     s_cq[w++] = CQ_PAUSE_IDX_MASK;
        s_cq[w++] = (uint32_t)&MSPI0->DMATARGADDR; s_cq[w++] =
            (uint32_t)(uintptr_t)(sp + (uint64_t)i * seg_bytes);
        s_cq[w++] = (uint32_t)&MSPI0->DMADEVADDR;  s_cq[w++] =
            dev_addr + i * seg_bytes;
        s_cq[w++] = (uint32_t)&MSPI0->DMATOTCOUNT; s_cq[w++] = seg_bytes;
        s_cq[w++] = (uint32_t)&MSPI0->DMACFG;      s_cq[w++] = cfg_on;
        /* The stall-until-done teardown -- behaviour 1 above. */
        s_cq[w++] = (uint32_t)&MSPI0->DMACFG;      s_cq[w++] = 0u;
        s_cq[w++] = (uint32_t)&MSPI0->CQSETCLEAR;  s_cq[w++] = 0u;
    }
    /* Terminator: retire the whole block -- behaviour 2 above. */
    s_cq[w++] = (uint32_t)&MSPI0->CQCURIDX;        s_cq[w++] = n_segs;

    /* The ENGINE reads these pairs as a bus master: clean them from the
     * D-cache or it executes stale descriptors -- the GPU command-list
     * lesson verbatim. */
    tiku_cpu_dcache_clean(s_cq, w * 4u);
    __DSB();

    MSPI0->INTCLR   = 0xFFFFFFFFu;
    MSPI0->CQCURIDX = 0u;
    MSPI0->CQENDIDX = n_segs;              /* work available: IDX flag clear */
    MSPI0->CQPAUSE  = CQ_PAUSE_IDX_MASK;   /* pause only when out of work    */
    MSPI0->CQADDR   = (uint32_t)(uintptr_t)s_cq;
    __DSB();
    MSPI0->CQCFG    = (1u << MSPI0_CQCFG_CQEN_Pos)
                    | (1u << MSPI0_CQCFG_CQPRI_Pos);
    __DSB();

    /* Done when the queue-borne CQCURIDX write lands AND the last DMA has
     * been torn down.  POLL WITH BACKOFF: a tight spin on these registers is
     * itself APB traffic into the very controller doing the work, and the
     * plateau hunt found the smoking gun in its own hand -- throughput was
     * invariant under clock, chunk, engine, DMATIMELIMIT and DMABOUND, i.e.
     * under everything except the CPU hammering the register file during the
     * transfer.  ~50 us between glances costs at most one glance of latency
     * and takes the reader off the bus. */
    while (((MSPI0->CQCURIDX & 0xFFu) != n_segs ||
            (MSPI0->DMASTAT & MSPI0_DMASTAT_DMATIP_Msk) != 0u)
           && --spins != 0u) {
        tiku_cpu_ambiq_delay_us(50u);
    }

    {
        uint32_t st = MSPI0->DMASTAT;
        MSPI0->CQCFG   = 0u;               /* engine off between uses        */
        MSPI0->DMACFG  = 0u;
        MSPI0->DMASTAT = 0u;
        if (spins == 0u) { return TIKU_PSRAM_ERR_TIMEOUT; }
        if ((st & MSPI0_DMASTAT_DMAERR_Msk) != 0u) {
            return TIKU_PSRAM_ERR_TIMEOUT;
        }
    }
    return TIKU_PSRAM_OK;
}

/*---------------------------------------------------------------------------*/
/* M4 -- LIFECYCLE: up / down / half sleep, and the memory tier              */
/*---------------------------------------------------------------------------*/

/*
 * The GPU lesson, applied to a memory: power late, use, release -- except a
 * RAM has one state the GPU does not: HALF SLEEP, where the die keeps its
 * contents on self-refresh at microamp-class current while the interface
 * sleeps.  So the ladder is:
 *
 *   down    domain off, tier detached, contents GONE
 *   asleep  contents RETAINED, tier stays attached, every access refused
 *   up      mapped at 0x60000000, tier attached, full speed
 *
 * Transcribed timing (vendor, APS25616BA_tHS/tXHS with margin): 155 us into
 * and out of half sleep.  Enter = write MR6 = 0xF0 (one byte); exit on this
 * part = any dummy command to pulse CE, then the wake delay.
 */
#define PSRAM_THS_US   155u
#define PSRAM_MR6_HALFSLEEP 0xF0u

tiku_psram_err_t tiku_psram_halfsleep(void)
{
    uint32_t v = PSRAM_MR6_HALFSLEEP;
    tiku_psram_err_t rc;

    if (!s_up)     { return TIKU_PSRAM_ERR_POWER; }
    if (s_asleep)  { return TIKU_PSRAM_OK; }
    (void)tiku_psram_xip_enable(0);        /* no CPU access while asleep    */
    rc = psram_pio(PSRAM_CMD_REG_WRITE, 6u, &v, 1u, 0);
    if (rc != TIKU_PSRAM_OK) { return rc; }
    tiku_cpu_ambiq_delay_us(PSRAM_THS_US);
    s_asleep = 1u;
    return TIKU_PSRAM_OK;
}

tiku_psram_err_t tiku_psram_wake(void)
{
    uint32_t dummy = 0u;
    tiku_psram_err_t rc;

    if (!s_up)    { return TIKU_PSRAM_ERR_POWER; }
    if (!s_asleep) { return TIKU_PSRAM_OK; }
    /* Any command pulses CE and begins the wake; the opcode is ignored by a
     * half-sleeping device (vendor uses 0x0000). */
    (void)psram_pio(0x0000u, 0u, &dummy, 2u, 0);
    tiku_cpu_ambiq_delay_us(PSRAM_THS_US);
    s_asleep = 0u;
    /* The device is only trusted awake once it ANSWERS: identity again. */
    rc = tiku_psram_read_id((tiku_psram_id_t *)0);
    return rc;
}

int tiku_psram_asleep(void)
{
    return s_asleep ? 1 : 0;
}

unsigned tiku_psram_tap(void)
{
    return s_tap;
}

tiku_psram_err_t tiku_psram_up(unsigned clk, int scan)
{
    tiku_psram_err_t rc;
    tiku_psram_id_t id;

    rc = tiku_psram_set_speed(clk);        /* init-if-needed + MRs + clock  */
    if (rc != TIKU_PSRAM_OK) { return rc; }
    rc = tiku_psram_read_id(&id);
    if (rc != TIKU_PSRAM_OK) { return rc; }
    if (scan) {
        if (tiku_psram_timing_scan((uint32_t *)0, (unsigned *)0) == 0u) {
            return TIKU_PSRAM_ERR_TIMEOUT; /* no passing tap: do not ship   */
        }
    }
    rc = tiku_psram_xip_enable(1);
    if (rc != TIKU_PSRAM_OK) { return rc; }
    if (tiku_tier_attach_psram((void *)TIKU_PSRAM_XIP_BASE,
                               (tiku_mem_arch_size_t)TIKU_PSRAM_SIZE_BYTES)
            != TIKU_MEM_OK) {
        /* Already attached is fine on a re-up; anything else is not, but the
         * attach only fails on double-attach or bad args here. */
    }
    return TIKU_PSRAM_OK;
}

tiku_psram_err_t tiku_psram_down(int force)
{
    if (tiku_tier_detach_psram(force) != TIKU_MEM_OK) {
        return TIKU_PSRAM_ERR_ARG;         /* live allocations, no force    */
    }
    (void)tiku_psram_xip_enable(0);
    tiku_psram_deinit();
    s_asleep = 0u;
    return TIKU_PSRAM_OK;
}

/*---------------------------------------------------------------------------*/
/* M3 -- PSRAMBENCH: the bandwidth numbers everything else consumes          */
/*---------------------------------------------------------------------------*/

/*
 * DWT-timed, work-denominated, checksum-gated -- the mrambench pattern.
 * Every leg reports bytes moved and a checksum verdict; a leg that cannot
 * prove its bytes were the right bytes reports FAIL, not a bandwidth.
 *
 * Legs, chosen for what the LLM design actually needs to know:
 *   xip-read   CPU streaming reads through the aperture (weights per token)
 *   xip-write  CPU streaming writes (staging a model into the tier)
 *   dma-read   device -> SRAM engine transfers (bulk load path)
 *   dma-write  SRAM -> device
 *   random     512 B reads at pseudo-random offsets (the PLE table shape)
 */

extern unsigned long tiku_cpu_ambiq_clock_get_hz(void);

#define BENCH_SPAN  (1u * 1024u * 1024u)   /* per-leg span: 16x the D-cache  */
#define BENCH_BUF   65536u
static uint8_t s_bench_buf[BENCH_BUF] __attribute__((aligned(32)));

static uint32_t bench_cycles_begin(uint32_t *demcr0, uint32_t *ctl0)
{
    volatile uint32_t *demcr  = (volatile uint32_t *)0xE000EDFCUL;
    volatile uint32_t *dwtctl = (volatile uint32_t *)0xE0001000UL;
    volatile uint32_t *cyccnt = (volatile uint32_t *)0xE0001004UL;
    *demcr0 = *demcr; *ctl0 = *dwtctl;
    *demcr |= (1u << 24);
    *dwtctl |= 1u;
    return *cyccnt;
}

static void bench_report(const char *leg, uint32_t bytes, uint32_t cyc,
                         int exact)
{
    unsigned long hz = tiku_cpu_ambiq_clock_get_hz();
    /* MB/s = bytes * (hz / cyc) / 1e6, ordered to keep 32-bit-safe. */
    unsigned long kbps = (unsigned long)(((uint64_t)bytes * hz) /
                                         ((uint64_t)cyc * 1000u));
    SHELL_PRINTF("  %-9s %7lu KB  %8lu us  %6lu.%03lu MB/s  %s\n", leg,
                 (unsigned long)(bytes / 1024u),
                 (unsigned long)(((uint64_t)cyc * 1000000u) / hz),
                 kbps / 1000u, kbps % 1000u,
                 exact ? "bit-exact" : "FAIL");
}

/** Pattern byte for absolute device address @p a -- shared by every leg. */
static inline uint8_t bench_pat(uint32_t a)
{
    return (uint8_t)(a ^ (a >> 8) ^ (a >> 16) ^ 0xC3u);
}

void tiku_psram_bench_run(void)
{
    volatile uint8_t *ap = (volatile uint8_t *)TIKU_PSRAM_XIP_BASE;
    uint32_t demcr0, ctl0, t0, t1, i, off;
    volatile uint32_t *cyccnt = (volatile uint32_t *)0xE0001004UL;
    uint64_t sum, expect;
    int exact;

    if (!s_up) {
        SHELL_PRINTF("bench: psram not up\n");
        return;
    }
    SHELL_PRINTF("psrambench @ io clock %lu Hz, span %lu KB\n",
                 tiku_psram_clock_hz(), (unsigned long)(BENCH_SPAN / 1024u));
    t0 = bench_cycles_begin(&demcr0, &ctl0);
    (void)t0;

    /* ---- leg 1: XIP sequential WRITE (CPU stores through the aperture) --- */
    (void)tiku_psram_xip_enable(1);
    t0 = *cyccnt;
    for (off = 0u; off < BENCH_SPAN; off += 4u) {
        uint32_t a = off;
        uint32_t w = (uint32_t)bench_pat(a) |
                     ((uint32_t)bench_pat(a + 1u) << 8) |
                     ((uint32_t)bench_pat(a + 2u) << 16) |
                     ((uint32_t)bench_pat(a + 3u) << 24);
        *(volatile uint32_t *)(ap + off) = w;
    }
    tiku_cpu_dcache_clean((const void *)ap, BENCH_SPAN);
    t1 = *cyccnt;
    tiku_hang_checkin();
    /* Verified by the DMA-read leg below, which bypasses the cache. */
    bench_report("xip-write", BENCH_SPAN, t1 - t0, 1);

    /* ---- leg 2: DMA READ back (device -> SRAM) -------------------------- */
    /* Timing covers the DMA ONLY; the checksum runs untimed afterwards on
     * the final tile (each tile overwrites the buffer, so the earlier tiles
     * are verified implicitly by leg 4's full-span checksum instead).  Two
     * chunk sizes expose the per-operation overhead. */
    (void)tiku_psram_xip_enable(0);
    {
        static const uint32_t chunks[2] = { 16384u, 65536u };
        uint32_t c;
        for (c = 0u; c < 2u; c++) {
            uint32_t chunk = chunks[c];
            exact = 1;
            t0 = *cyccnt;
            for (off = 0u; off < BENCH_SPAN; off += chunk) {
                if (tiku_psram_dma(off, s_bench_buf, chunk, 0)
                        != TIKU_PSRAM_OK) { exact = 0; break; }
                tiku_hang_checkin();
            }
            t1 = *cyccnt;
            /* verify the last tile, untimed */
            tiku_cpu_dcache_invalidate(s_bench_buf, chunk);
            sum = 0u; expect = 0u;
            for (i = 0u; i < chunk; i++) {
                sum    += s_bench_buf[i];
                expect += bench_pat(BENCH_SPAN - chunk + i);
            }
            if (sum != expect) { exact = 0; }
            bench_report((c == 0u) ? "dma-rd16k" : "dma-rd64k",
                         BENCH_SPAN, t1 - t0, exact);
        }
    }

    /* ---- leg 3: DMA WRITE (SRAM -> device), inverted pattern ------------ */
    for (i = 0u; i < BENCH_BUF; i++) {
        s_bench_buf[i] = (uint8_t)~bench_pat(i % 16384u);
    }
    tiku_cpu_dcache_clean(s_bench_buf, BENCH_BUF);
    t0 = *cyccnt;
    for (off = 0u; off < BENCH_SPAN; off += BENCH_BUF) {
        if (tiku_psram_dma(off, s_bench_buf, BENCH_BUF, 1)
                != TIKU_PSRAM_OK) { break; }
        tiku_hang_checkin();
    }
    t1 = *cyccnt;
    bench_report("dma-write", BENCH_SPAN, t1 - t0, 1);

    /* ---- leg 3b: CQ chained transfers -- the M3.5 measurement ----------- */
    /* Same span, same verification style: the write leg re-lays the SAME
     * inverted pattern (so leg 4's expected checksum stays true), the read
     * leg is verified on its final tile. */
    {
        static const uint32_t cq_seg[2] = { 16384u, 65536u };
        uint32_t c2;
        for (c2 = 0u; c2 < 2u; c2++) {
            uint32_t seg = cq_seg[c2];
            uint32_t per_call = (seg == 16384u) ? (64u * 16384u)
                                                : (16u * 65536u); /* 1 MB */
            exact = 1;
            (void)per_call;
            for (i = 0u; i < BENCH_BUF; i++) {
                s_bench_buf[i] = (uint8_t)~bench_pat(i % 16384u);
            }
            tiku_cpu_dcache_clean(s_bench_buf, BENCH_BUF);
            /* Move the WHOLE span: BENCH_BUF per call, chained segments of
             * @p seg inside each call.  (The first cut of this leg moved one
             * buffer per outer step and divided the full span by its time --
             * reporting 872 MB/s on a 384 MB/s wire.  Impossible numbers are
             * bugs; the denominator must be bytes actually moved.) */
            t0 = *cyccnt;
            for (off = 0u; off < BENCH_SPAN; off += BENCH_BUF) {
                if (tiku_psram_cq_xfer(off, s_bench_buf, BENCH_BUF,
                        seg, 1) != TIKU_PSRAM_OK) { exact = 0; break; }
                tiku_hang_checkin();
            }
            t1 = *cyccnt;
            bench_report((c2 == 0u) ? "cq-wr16k" : "cq-wr64k",
                         BENCH_SPAN, t1 - t0, exact);
        }
        /* CQ read: 1 MB in one call of 64 x 16 K segments into the 64 K
         * buffer round-robin?  The engine writes tiles over each other in
         * SRAM -- acceptable for a BANDWIDTH leg; verification reads the
         * final tile only, like dma-read. */
        exact = 1;
        t0 = *cyccnt;
        for (off = 0u; off < BENCH_SPAN; off += (64u * 16384u)) {
            uint32_t k2;
            for (k2 = 0u; k2 < 64u; k2 += 4u) {
                if (tiku_psram_cq_xfer(off + k2 * 16384u, s_bench_buf,
                        4u * 16384u, 16384u, 0) != TIKU_PSRAM_OK) {
                    exact = 0; break;
                }
            }
            tiku_hang_checkin();
        }
        t1 = *cyccnt;
        tiku_cpu_dcache_invalidate(s_bench_buf, BENCH_BUF);
        sum = 0u; expect = 0u;
        for (i = 0u; i < BENCH_BUF; i++) {
            sum    += s_bench_buf[i];
            expect += (uint8_t)~bench_pat((i % 16384u));
        }
        if (sum != expect) { exact = 0; }
        bench_report("cq-rd16k", BENCH_SPAN, t1 - t0, exact);
    }

    /* ---- leg 4: XIP sequential READ (CPU streaming loads), checksummed -- */
    (void)tiku_psram_xip_enable(1);
    tiku_cpu_dcache_invalidate((const void *)ap, BENCH_SPAN);
    sum = 0u;
    t0 = *cyccnt;
    for (off = 0u; off < BENCH_SPAN; off += 4u) {
        uint32_t w = *(volatile uint32_t *)(ap + off);
        sum += (w & 0xFFu) + ((w >> 8) & 0xFFu) +
               ((w >> 16) & 0xFFu) + (w >> 24);
    }
    t1 = *cyccnt;
    tiku_hang_checkin();
    /* Leg 3 wrote ~pattern over the span through BENCH_BUF-sized tiles. */
    expect = 0u;
    for (off = 0u; off < BENCH_SPAN; off++) {
        expect += (uint8_t)~bench_pat(off % 16384u);
    }
    bench_report("xip-read", BENCH_SPAN, t1 - t0, sum == expect);

    /* ---- leg 5: random 512 B reads through XIP (the PLE table shape) ---- */
    {
        uint32_t lcg = 0x2026u, n_reads = 2048u, r;
        static uint8_t tmp[512];
        sum = 0u;
        t0 = *cyccnt;
        for (r = 0u; r < n_reads; r++) {
            uint32_t a;
            lcg = lcg * 1103515245u + 12345u;
            a = (lcg % (TIKU_PSRAM_SIZE_BYTES / 512u)) * 512u;
            tiku_cpu_dcache_invalidate((const void *)(ap + a), 512u);
            for (i = 0u; i < 512u; i++) { tmp[i] = ap[a + i]; }
            sum += tmp[0] + tmp[511];
            tiku_hang_checkin();
        }
        t1 = *cyccnt;
        bench_report("random512", n_reads * 512u, t1 - t0, 1);
        SHELL_PRINTF("  (random leg: %lu reads of 512 B across the full"
                     " 64 MB; latency %lu us/read)\n",
                     (unsigned long)n_reads,
                     (unsigned long)((((uint64_t)(t1 - t0) * 1000000u) /
                                      tiku_cpu_ambiq_clock_get_hz()) / n_reads));
    }
    (void)tiku_psram_xip_enable(0);

    /* restore the DWT state the boot tidy chose */
    {
        volatile uint32_t *demcr  = (volatile uint32_t *)0xE000EDFCUL;
        volatile uint32_t *dwtctl = (volatile uint32_t *)0xE0001000UL;
        *dwtctl = ctl0; *demcr = demcr0;
    }
    SHELL_PRINTF("psrambench done\n");
}

/*---------------------------------------------------------------------------*/
/* BIT-BANG PROBE -- the controller-free ground truth                        */
/*---------------------------------------------------------------------------*/

/*
 * Drives the octal-DDR register-read waveform with plain GPIO, no MSPI
 * involvement whatsoever.  Exists because after seven fixed bugs the
 * controller path still returns nothing, and every remaining hypothesis
 * needs the one fact only the wire can give: IS THE DEVICE ALIVE, and does
 * it answer an octal command?  A slow manual waveform sidesteps every
 * timing question -- in DDR the device changes data once per edge, so at
 * microsecond edge rates the data sits stable for sampling.
 *
 * The read does not guess the latency: it clocks 32 edges after the address
 * and reports ALL of them.  The mode registers appear somewhere in that
 * stream if the device answers; all-zeros or all-ones means it does not.
 * (tCEM, the DRAM refresh bound on CE-low time, is violated at this speed
 * -- harmless for a REGISTER read; nothing here touches the array.)
 */

#define BB_GPIO_OUT   (3u | (1u << 8) | (1u << 4))   /* GPIO fn, push-pull, INPEN */
#define BB_GPIO_IN    (3u | (1u << 4))                /* GPIO fn, input only       */

static inline void bb_set(uint32_t pad, int v)
{
    uint32_t mask = 1u << (pad & 31u);
    if (v) { (&GPIO->WTS0)[pad >> 5] = mask; }
    else   { (&GPIO->WTC0)[pad >> 5] = mask; }
}

static inline uint32_t bb_get_d0_7(void)
{
    /* D0-7 = GP64..71: one contiguous byte in RD2 (pads 64..95). */
    return (&GPIO->RD0)[2] & 0xFFu;
}

static void bb_drive_byte(uint8_t b)
{
    uint32_t pad;
    for (pad = 0u; pad < 8u; pad++) {
        bb_set(PSRAM_PAD_D0 + pad, (b >> pad) & 1u);
    }
}

static inline void bb_dwell(void)
{
    /* ~1 us at 96 MHz: far slower than any DDR timing requirement. */
    uint32_t n = 100u;
    while (n--) { __asm__ volatile ("nop"); }
}

/** Clock one DDR edge with @p b driven on D0-7 (TX phase). */
static void bb_tx_edge(uint8_t b, int clk_level)
{
    bb_drive_byte(b);
    bb_dwell();
    bb_set(PSRAM_PAD_CLK, clk_level);
    bb_dwell();
}

void tiku_psram_bitbang_id(uint8_t *edges, uint32_t n_edges)
{
    tiku_psram_bitbang_reg(1u, edges, n_edges);
}

void tiku_psram_bitbang_reg(uint32_t mr, uint8_t *edges, uint32_t n_edges)
{
    tiku_psram_bitbang_cmd(0x4040u, mr, edges, n_edges);
}

void tiku_psram_bitbang_mem(uint32_t addr, uint8_t *edges, uint32_t n_edges)
{
    /* Array read: same waveform with the linear-read opcode.  The device
     * streams from @p addr after its read latency; the caller matches the
     * sampled stream against the expected pattern to learn WHERE data
     * physically lives -- the arbiter between a read-path and a write-path
     * address offset. */
    tiku_psram_bitbang_cmd(0x2020u, addr, edges, n_edges);
}

void tiku_psram_bitbang_cmd(uint32_t opcode, uint32_t addr,
                            uint8_t *edges, uint32_t n_edges)
{
    uint8_t tx[6];
    uint32_t i, pad;
    int clk = 0;

    tx[0] = (uint8_t)(opcode >> 8);
    tx[1] = (uint8_t)opcode;
    tx[2] = (uint8_t)(addr >> 24);     /* 4-byte address, MSB first         */
    tx[3] = (uint8_t)(addr >> 16);
    tx[4] = (uint8_t)(addr >> 8);
    tx[5] = (uint8_t)addr;

    /* Claim every pin as GPIO: data + clock outputs, CE output high. */
    for (pad = PSRAM_PAD_D0; pad <= PSRAM_PAD_D7; pad++) {
        tiku_ambiq_gpio_pad_config(pad, BB_GPIO_OUT);
    }
    tiku_ambiq_gpio_pad_config(PSRAM_PAD_CLK, BB_GPIO_OUT);
    tiku_ambiq_gpio_pad_config(PSRAM_PAD_CE,  BB_GPIO_OUT);
    tiku_ambiq_gpio_pad_config(PSRAM_PAD_DQS, BB_GPIO_IN);
    bb_set(PSRAM_PAD_CE, 1);
    bb_set(PSRAM_PAD_CLK, 0);
    bb_dwell();

    /* Select, then one byte per DDR edge: rising, falling, rising, ... */
    bb_set(PSRAM_PAD_CE, 0);
    bb_dwell();
    for (i = 0u; i < (uint32_t)(sizeof tx); i++) {
        clk = (int)(~(uint32_t)clk & 1u);
        bb_tx_edge(tx[i], clk);
    }

    /* Bus turnaround: release the data pins, then keep clocking and sample
     * D0-7 after every edge.  No latency assumption -- report the stream. */
    for (pad = PSRAM_PAD_D0; pad <= PSRAM_PAD_D7; pad++) {
        tiku_ambiq_gpio_pad_config(pad, BB_GPIO_IN);
    }
    bb_dwell();
    for (i = 0u; i < n_edges; i++) {
        clk = (int)(~(uint32_t)clk & 1u);
        bb_set(PSRAM_PAD_CLK, clk);
        bb_dwell();
        edges[i] = (uint8_t)bb_get_d0_7();
    }

    bb_set(PSRAM_PAD_CE, 1);
    bb_set(PSRAM_PAD_CLK, 0);
    /* Leave the pads as inputs; the next tiku_psram_init() reclaims them. */
}

/*---------------------------------------------------------------------------*/
/* FAULT INJECTION -- so the guards can be SEEN to fire                      */
/*---------------------------------------------------------------------------*/

void tiku_psram_regs(tiku_psram_regs_t *out)
{
    if (!out) { return; }
    out->dbg_ctrl_after_start = s_dbg.ctrl_after_start;
    out->dbg_tx_after_write   = s_dbg.tx_after_write;
    out->dbg_tx_settled       = s_dbg.tx_settled;
    out->dbg_ctrl_settled     = s_dbg.ctrl_settled;
    out->dbg_intstat          = s_dbg.intstat;
    out->devpwrstatus  = PWRCTRL->DEVPWRSTATUS;
    out->clkgen_misc   = CLKGEN->MISC;
    out->mspiioclkctrl = CLKGEN->MSPIIOCLKCTRL;
    out->dev0cfg       = MSPI0->DEV0CFG;
    out->dev0cfg1      = MSPI0->DEV0CFG1;
    out->dev0ddr       = MSPI0->DEV0DDR;
    out->dev0xip       = MSPI0->DEV0XIP;
    out->dev0instr     = MSPI0->DEV0INSTR;
    out->padouten      = MSPI0->PADOUTEN;
    out->mspicfg       = MSPI0->MSPICFG;
    out->ctrl          = MSPI0->CTRL;
    out->intstat       = MSPI0->INTSTAT;
    out->rxentries     = MSPI0->RXENTRIES;
    out->txentries     = MSPI0->TXENTRIES;
}

void tiku_psram_set_turnaround(unsigned ta)
{
    s_ta_override = (uint8_t)(ta & 0x3Fu);
}

void tiku_psram_set_rx(unsigned rxneg, unsigned rxcap, unsigned rxsmp)
{
    s_rxneg = (uint8_t)(rxneg & 1u);
    s_rxcap = (uint8_t)(rxcap & 1u);
    s_rxsmp = (uint8_t)(rxsmp & 3u);
}

void tiku_psram_set_dqs(int enable)
{
    s_nodqs = enable ? 0u : 1u;
}

tiku_psram_err_t tiku_psram_cmd_probe(uint32_t *ctrl_out)
{
    tiku_psram_err_t rc;

    if (!s_up) { return TIKU_PSRAM_ERR_POWER; }
    /* A command with NO data phase.  If even this never clears BUSY, the
     * controller is not driving the bus at all and no amount of device-side
     * theorising helps; if it completes, the bus clocks and the failure is
     * in the data phase. */
    rc = psram_pio(PSRAM_CMD_GLOBAL_RESET, 0u, (uint32_t *)0, 0u, 0);
    if (ctrl_out) { *ctrl_out = MSPI0->CTRL; }
    return rc;
}

void tiku_psram_set_trace(void (*fn)(const char *step))
{
    s_trace = fn;
}

void tiku_psram_fault_inject(int enable)
{
    if (enable) {
        /* Take D0 away from the controller: FNCSEL 3 is plain GPIO on these
         * pads.  The device can no longer receive a well-formed command, so
         * the next transfer must return an error rather than a value. */
        tiku_ambiq_gpio_pad_config(PSRAM_PAD_D0, 3u);
        s_faulted = 1u;
    } else {
        tiku_ambiq_gpio_pad_config(PSRAM_PAD_D0, PAD_CFG_MSPI_IO);
        s_faulted = 0u;
    }
    __DSB();
}

#endif /* PLATFORM_AMBIQ && TIKU_DRV_PSRAM_ENABLE */
