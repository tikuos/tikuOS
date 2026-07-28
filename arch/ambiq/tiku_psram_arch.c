/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.c - Apollo510 MSPI0 + APS512 octal-DDR PSRAM bring-up.
 *
 * Implements tables 1 and 2 of tiku_psram_arch.h.  Read that header first:
 * it is the transcription this file is checked against, and the ordering
 * constraints in it are hardware requirements, not style.
 *
 * A LESSON THIS FILE PAID FOR ON ITS FIRST RUN: four field encodings were
 * DERIVED (from the order values appear in vendor code) instead of READ from
 * the register header's encoding tables, and all four were wrong -- the IO
 * clock select (off by 128x), XIPACK, the DMA boundary, and IOMSEL, where
 * the wrong value selected IOM6, the internal link to the radio die.  The
 * board wedged with no output and no fault.  Every field is now written
 * through the CMSIS enum NAME, so the compiler owns the encoding.
 *
 * DESIGN NOTES THAT COST SOMETHING TO LEARN ELSEWHERE IN THIS PORT:
 *
 *   - Configure the CONTROLLER before the PADS.  While the pads are still
 *     GPIO the device sees nothing, so a half-programmed controller cannot
 *     drive a malformed transaction at it.  (Mirrors the GPU's "mode and
 *     rail with the domain down" rule.)
 *   - Every wait is spin-bounded and returns a DISTINCT error.  The Nordic
 *     and STIMER work both showed that an unbounded poll on dead hardware
 *     is indistinguishable from a hang.
 *   - Identity is read at the LOWEST clock, before anything is trusted.  A
 *     mis-timed octal bus returns plausible garbage; asking the device who
 *     it is, slowly, is the only cheap way to know the wiring is right.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#if defined(PLATFORM_AMBIQ) && (TIKU_DRV_PSRAM_ENABLE + 0)

#include "tiku_psram_arch.h"
#include "tiku_gpio_arch.h"      /* tiku_ambiq_gpio_pad_config()             */
#include "tiku_cpu_common.h"     /* tiku_cpu_ambiq_delay_us()                */
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
#define PSRAM_PAD_CE        199u

#define PAD_FNCSEL_MSPI0    0u
#define PAD_FNCSEL_MNCE0    1u
#define PAD_DS_0P5X         (1u << 10)   /* DS[11:10] = 0.5x driver         */
#define PAD_OUTCFG_PUSHPULL (1u << 8)    /* OUTCFG[9:8] = push-pull         */
#define PAD_NCEPOL_LOW      (0u << 22)   /* NCEPOL: active low              */

/** Data/CLK/DQS: function select + drive strength + INPUT ENABLE.
 *
 * The vendor BSP leaves INPEN clear on these pads and its driver works, so
 * the dedicated-pad receive path presumably bypasses the GPIO input gate --
 * but "presumably" has been wrong seven times in this file, the bit-bang
 * probe demonstrated the GPIO input path DOES read these pins, and enabling
 * the input buffer costs nothing.  Belt and braces until the RX path is
 * proven either way. */
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

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Optional step tracer.
 *
 * Bring-up on a dead bus can HANG rather than fail: a register write to a
 * peripheral whose clock is wrong stalls the bus with no fault and no
 * output, which is indistinguishable from a crash.  When a tracer is
 * installed, each risky step announces itself first, so the last line
 * printed names the step that wedged -- the same "the trace is the report"
 * technique the deep-sleep autorun uses on the current meter.
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
 * Transcribed from am_hal_mspi_blocking_transfer's PIO path: INSTR and ADDR
 * are staged, then a single CTRL write with START launches it; RX data is
 * drained from RXFIFO as RXENTRIES reports words available; completion is
 * CTRL.STATUS going to 1.
 *
 * Interrupts are not used and INTEN is left alone -- this driver never
 * enables MSPI interrupts, so there is nothing to save and restore (the
 * vendor's save/restore exists because its HAL shares the peripheral with
 * a DMA/command-queue path that does).
 *
 * @param instr    2-byte octal-DDR opcode
 * @param addr     device address (byte address, or MR number for reg access)
 * @param data     word buffer in/out, may be NULL when n_bytes is 0
 * @param n_bytes  data phase length
 * @param is_read  non-zero for RX (adds turnaround + write-latency enable)
 */
static tiku_psram_err_t psram_pio(uint16_t instr, uint32_t addr,
                                  uint32_t *data, uint32_t n_bytes,
                                  int is_read)
{
    uint32_t ctrl = 0u;
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
        ctrl |= MSPI0_CTRL_ENWLAT_Msk;
    } else {
        ctrl |= (1u << MSPI0_CTRL_TXRX_Pos) & MSPI0_CTRL_TXRX_Msk;
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
        /* WAIT FOR COMPLETION FIRST, then drain.
         *
         * The vendor polls RXENTRIES per word and reads RXFIFO as entries
         * appear.  Measured here, that never terminates: the command
         * completes (CTRL.STATUS set, INTSTAT.CMDCMP set, no error bit) while
         * RXENTRIES stays 0 for the entire poll.  So on this part the PIO
         * receive data is not visible through RXENTRIES during the transfer;
         * it is drained after CMDCMP.  Completion is bounded and reported, and
         * RXENTRIES is recorded rather than trusted. */
        spins = PSRAM_PIO_SPINS;
        while (((MSPI0->CTRL & MSPI0_CTRL_STATUS_Msk) == 0u) && --spins != 0u) { }
        s_dbg.spins_left   = spins;
        s_dbg.ctrl_settled = MSPI0->CTRL;
        s_dbg.intstat      = MSPI0->INTSTAT;
        s_dbg.tx_settled   = MSPI0->RXENTRIES;
        if (spins == 0u) {
            return TIKU_PSRAM_ERR_TIMEOUT;
        }
        for (i = 0u; i < full_words; i++) {
            data[i] = MSPI0->RXFIFO;
        }
        if (leftover != 0u) {
            uint32_t tail = MSPI0->RXFIFO;
            uint8_t *dst  = (uint8_t *)&data[full_words];
            uint32_t b;
            for (b = 0u; b < leftover; b++) {
                dst[b] = (uint8_t)(tail >> (8u * b));
            }
        }
        return TIKU_PSRAM_OK;
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
 * TWO STEPS, and the first one is easy to miss: the oscillator block must be
 * FORCED ON before a peripheral can clock from it.  CLKGEN.MISC.FRCHFRC is
 * what the vendor's clock manager sets on the first HFRC user
 * (am_hal_clkgen_private_hfrc_force_on); without it the core still runs --
 * it has its own demand on HFRC -- but the MSPI's IO clock branch has no
 * source, and every transfer times out with a perfectly configured
 * controller.  That was the second bring-up failure here.
 */
static tiku_psram_err_t psram_ioclk_on(uint8_t sel)
{
    uint32_t v;

    /* THE VENDOR'S CLKGEN.MISC STATE, replicated.  Breakpointing the
     * vendor's own example at its ID-read moment (the experiment that ended
     * this hunt) showed its DEV0* configuration essentially identical to
     * ours -- but CLKGEN.MISC = 0x08FBBFC1 against our 0x08000021.  The
     * difference is the clock-gate-enable + power-on-clock chicken-bit block
     * that am_hal_pwrctrl_low_power_init() writes at vendor boot and our
     * bare-metal boot never has.  Replicated verbatim: bits 6-13 and 15-17
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
                                                  : PSRAM_TURNAROUND_DQS);

    /* Step 3: the SDR250 tap, before DEV0CFG so CLKDIV means what we think. */
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
    cfg |= ((uint32_t)PSRAM_BRINGUP_WRITELAT << MSPI0_DEV0CFG_WRITELATENCY0_Pos)
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
    MSPI0->DEV0XIP_b.XIPWRITELATENCY0 = PSRAM_BRINGUP_WRITELAT;

    /* Step 11: 1 KB DMA boundary -- the device's row boundary. */
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

    /* Step 14: FIFO threshold (high-speed class; DMA thresholds belong to
     * the M3 DMA path, which owns the transfer-control buffer). */
    MSPI0->THRESHOLD_b.RXTHRESH = 30u;

    /* Step 15: we are not bridging an IOM through this controller.
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
    uint8_t tx[6];
    uint32_t i, pad;
    int clk = 0;

    tx[0] = 0x40u; tx[1] = 0x40u;      /* READ_REGISTER, one byte per edge  */
    tx[2] = 0x00u; tx[3] = 0x00u;      /* 4-byte address, MSB first         */
    tx[4] = 0x00u; tx[5] = (uint8_t)mr;

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
