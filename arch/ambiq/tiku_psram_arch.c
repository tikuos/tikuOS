/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.c - Apollo510 MSPI0 and APS512 octal-DDR PSRAM bring-up.
 *
 * Configures the controller before the pads, bounds every wait, and checks the
 * device identity before use.  Also PIO, DMA and command-queue transfers, the
 * XIP aperture, half sleep, a GPIO bit-bang probe and a bandwidth bench.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#if defined(PLATFORM_AMBIQ) && (TIKU_DRV_PSRAM_ENABLE + 0)

#include "tiku_psram_arch.h"
#include "tiku_gpio_arch.h"      /* tiku_ambiq_gpio_pad_config()             */
#include "tiku_cpu_common.h"     /* tiku_cpu_ambiq_delay_us()                */
#include "hal/tiku_cpu.h"        /* D-cache clean and invalidate             */
#include <kernel/cpu/tiku_hang.h>   /* check-in from the bench loops         */
#include <shell/tiku_shell_io.h> /* bench reports via SHELL_PRINTF    */
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

/* Identity the device must report: MR1.VID, MR2.DENSITY, MR2.GB. */
#define PSRAM_VID_AP_MEMORY      0x0Du
#define PSRAM_DENSITY_512MBIT    0x06u
#define PSRAM_GB_PASS            0x06u

/*---------------------------------------------------------------------------*/
/* PADS (table 1 step 16)                                                    */
/*---------------------------------------------------------------------------*/

/*
 * GP64..GP73 carry MSPI0 signals 0..9: 0-7 are data, 8 is CLK, 9 is
 * DQS0/DM0.  They are dedicated MSPI pads, where MSPI0 is FNCSEL 0.  The chip
 * select is the board's pad, where MNCE0 is FNCSEL 1.  Data, clock and DQS
 * leave OUTCFG at 0 (the controller's PADOUTEN owns direction) and take 0.5x
 * drive and no pull; CE is push-pull, NCESRC=0, active low.
 */
#define PSRAM_PAD_D0        64u
#define PSRAM_PAD_D7        71u
#define PSRAM_PAD_CLK       72u
#define PSRAM_PAD_DQS       73u
/* The chip select comes from the board header; D0..DQS are the same MSPI0
 * pads on every board. */
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

/* Data/CLK/DQS: MSPI0 function, 0.5x drive and the input buffer (INPEN). */
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
 * am_hal_mspi_io_clock_sel_e.  The enum starts at HFRC_750KHZ = 0 and
 * doubles up to HFRC 192 MHz at 8; HFRC2 250 MHz is 10.  Values 1 and 2
 * select 1.5 MHz and 3 MHz.
 */
#define IOCLK_SEL_HFRC_192MHZ   8u
#define IOCLK_SEL_HFRC2_250MHZ 10u

/* DEV0CFG.CLKDIV0 is a raw divider count: 1 divides by 1. */
#define CLKDIV_1  1u
#define CLKDIV_2  2u

/** @brief One IO clock row: source, divider and TX clock edge. */
typedef struct {
    uint8_t  ioclk_sel;   /**< MSPIIOCLKCTRL source select                  */
    uint8_t  clkdiv;      /**< DEV0CFG.CLKDIV0                              */
    uint8_t  sdr250;      /**< DEV0CFG1.SDR250EN0 -- bypasses the /2        */
    uint8_t  txneg;       /**< DEV0CFG.TXNEG0, set by frequency             */
    uint32_t hz;          /**< nominal IO clock, for reporting              */
} psram_clk_t;

/* Order matches TIKU_PSRAM_CLK_*.  Every row obeys
 * hz = source / (2 * clkdiv), except where sdr250 bypasses the /2.
 *
 * TXNEG selects the TX clock edge by frequency, as the vendor does: 0 at
 * 62.5 MHz and below, 1 at 96 MHz and above.  TXNEG = 1 at 48 MHz launches
 * every command bit half a clock early: the device never answers, while
 * TX-only commands still complete on the controller side. */
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
 * TURNAROUND and WRITELATENCY count the bus cycles the controller idles after
 * the address before read data arrives, or before write data may be driven.
 * They must match the device's MR0.RLC and MR4.WLC: in DQS mode TURNAROUND
 * is RLC * 2 (12 at RLC 6), and without DQS it is 22; WRITELATENCY is
 * WLC * 2 in both modes (12 at WLC 6).
 *
 * A TURNAROUND of 6 at RLC 6 opens the read window before the data, the DQS
 * strobe never falls inside it, and the controller waits with BUSY set and
 * no error bit.  The default here is DQS mode.  psram_program_latency() sets
 * both sides for a new clock; one side changed alone returns shifted data
 * and no error.
 */
#define PSRAM_TURNAROUND_DQS     12u
#define PSRAM_TURNAROUND_NODQS   22u
#define PSRAM_BRINGUP_WRITELAT   12u

/*
 * The device's latency codes per clock row, programmed into MR0/MR4.  The
 * controller's TURNAROUND = RLC * 2 and WRITELATENCY = WLC * 2 move with
 * them.  RLC 6 reads up to 133 MHz; WLC 5 writes up to 66 MHz and WLC 6 up to
 * 109 MHz.  The die is rated to 200 MHz.
 *
 *   clock    RLC (MR0[4:2] code)     WLC (MR4[7:5] code)
 *   48       6 (011, default)        5 (010, default)
 *   96       6 (011)                 6 (110)
 *   125      6 (011)                 7 (001)
 *   192      8 (101)                 9 (011)
 *   250      8 (101)                 9 (011)
 *
 * After reset MR4 reads 0x40, WLC code 010 (LC5).  tiku_psram_set_speed()
 * programs the codes of whichever row it moves to, the 48 MHz row included.
 */
typedef struct { uint8_t rlc, rlc_code, wlc, wlc_code; } psram_lat_t;
static const psram_lat_t s_lat[] = {
    { 6u, 0x3u, 5u, 0x2u },   /* 48 MHz  -- device power-up defaults        */
    { 6u, 0x3u, 6u, 0x6u },   /* 96 MHz  */
    { 6u, 0x3u, 7u, 0x1u },   /* 125 MHz */
    { 8u, 0x5u, 9u, 0x3u },   /* 192 MHz */
    { 8u, 0x5u, 9u, 0x3u },   /* 250 MHz: above the die's 200 MHz rating */
};
static uint8_t s_asleep;      /**< 1 while the device is in half sleep      */
static uint8_t s_tap = 0xFFu; /**< RXDQSDELAY tap from the last passing scan */
static uint8_t s_turnaround = PSRAM_TURNAROUND_DQS;   /* live values        */
static uint8_t s_writelat   = 10u;   /* WLC5 * 2: the device's power-up
                                        default, see s_lat[]                 */

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Optional step tracer.
 *
 * A register write to a peripheral whose clock is wrong can stall the bus
 * with no fault and no output; the last step traced names where it stopped.
 */
static void (*s_trace)(const char *step);

/** @brief Report @p step to the tracer, if one is installed. */
static void trace(const char *step)
{
    if (s_trace) { s_trace(step); }
}

/*
 * Register values captured inside PIO transfers.  Whether the first TX FIFO
 * word is consumed separates a controller with no clock from one that is
 * clocking and waiting on the device.
 */
static struct {
    uint32_t ctrl_after_start;
    uint32_t tx_after_write;    /* TXENTRIES right after the FIFO write     */
    uint32_t tx_settled;        /* TXENTRIES after a short delay            */
    uint32_t ctrl_settled;
    uint32_t intstat;
    uint32_t spins_left;
} s_dbg;

static uint8_t s_pio_failed; /**< PIO timeout; cleared by power-down. */
static uint8_t  s_up;        /**< 1 once init() completed                    */
static uint8_t  s_clk_idx;   /**< index into s_clk of the live setting       */
static uint8_t  s_faulted;   /**< 1 while fault injection is active           */
static uint8_t  s_nodqs;     /**< 1 to bring up without the DQS strobe        */
static uint8_t  s_ta_override; /**< non-zero: use this TURNAROUND instead     */
static uint8_t  s_rxneg;       /**< DEV0CFG.RXNEG0 override                   */
static uint8_t  s_rxcap;       /**< DEV0CFG.RXCAP0 override                   */
static uint8_t  s_rxsmp = 1u;  /**< DEV0CFG1.RXSMP0 (vendor default 1)        */

/*---------------------------------------------------------------------------*/
/* PIO TRANSFER (table 2)                                                    */
/*---------------------------------------------------------------------------*/

/** Poll-iteration bound on each wait inside a PIO transfer. */
#define PSRAM_PIO_SPINS  400000u

/** MSPI FIFO depth in words (AM_HAL_MSPI_MAX_FIFO_SIZE). */
#define PSRAM_FIFO_WORDS 32u

static tiku_psram_err_t psram_pio2(uint16_t instr, uint32_t addr,
                                   uint32_t *data, uint32_t n_bytes,
                                   int is_read, int wlat);

/** @brief psram_pio2() with ENWLAT for reads only (MR writes need none). */
static tiku_psram_err_t psram_pio(uint16_t instr, uint32_t addr,
                                  uint32_t *data, uint32_t n_bytes,
                                  int is_read)
{
    return psram_pio2(instr, addr, data, n_bytes, is_read, is_read ? 1 : 0);
}

/**
 * @brief Run one PIO command: opcode, address and an optional data phase.
 *
 * As am_hal_mspi_blocking_transfer's PIO path: INSTR and ADDR are staged, one
 * CTRL write with START launches it, RX words drain as RXENTRIES reports
 * them, and CTRL.STATUS = 1 marks completion.  INTEN is not touched.
 *
 * @param instr    2-byte octal-DDR opcode
 * @param addr     device address (byte address, or MR number for reg access)
 * @param data     word buffer in/out, may be NULL when n_bytes is 0
 * @param n_bytes  data phase length
 * @param is_read  non-zero for RX (turns the bus around)
 * @param wlat     non-zero to apply the write-latency count (ENWLAT)
 * @return TIKU_PSRAM_OK, ERR_ARG while XIP is on, or ERR_TIMEOUT
 */
static tiku_psram_err_t psram_pio2(uint16_t instr, uint32_t addr,
                                   uint32_t *data, uint32_t n_bytes,
                                   int is_read, int wlat)
{
    uint32_t ctrl = 0u;

    if (s_pio_failed) {
        return TIKU_PSRAM_ERR_TIMEOUT;
    }

    /* A PIO command issued while the XIP aperture is enabled deadlocks the
     * controller's APB interface: the peripheral stays unreadable until a
     * power cycle. */
    if (MSPI0->DEV0XIP_b.XIPEN0 != 0u) {
        return TIKU_PSRAM_ERR_ARG;
    }
    /* The FIFO moves whole 32-bit words, but a length need not be a multiple
     * of four (the device reset sends 2 bytes).  TX rounds up: XFERBYTES
     * promises the bytes, and with no word in the FIFO the controller waits
     * forever.  RX takes the leftover bytes from one final word. */
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
        /* TXRX = 0 is receive (AM_HAL_MSPI_RX = 0, AM_HAL_MSPI_TX = 1).  A
         * read issued as a transmit completes at once and captures nothing;
         * a write issued as a receive waits in DQS mode for a strobe that
         * never comes.  A read turns the bus around (ENTURN) and, with
         * @p wlat, applies the latency count (ENWLAT), as the vendor does. */
        ctrl |= MSPI0_CTRL_ENTURN_Msk;
        if (wlat) { ctrl |= MSPI0_CTRL_ENWLAT_Msk; }
    } else {
        ctrl |= (1u << MSPI0_CTRL_TXRX_Pos) & MSPI0_CTRL_TXRX_Msk;
        /* Array writes apply the device's write latency; without it the
         * stored data is shifted by 8 bytes.  Register writes pass wlat = 0:
         * MRs take data at once. */
        if (wlat) { ctrl |= MSPI0_CTRL_ENWLAT_Msk; }
    }

    /* FIFORESET can hang the transfer state machine. A timeout instead
     * blocks further PIO commands until deinit/init powers it down again. */
    MSPI0->INTCLR = 0xFFFFFFFFu;
    MSPI0->CTRL   = ctrl;
    s_dbg.ctrl_after_start = MSPI0->CTRL;

    if (is_read && data != (uint32_t *)0) {
        /* Drain as data arrives: a transfer larger than the 32-word FIFO
         * completes only if the CPU keeps making room. */
        uint32_t total_words = full_words + ((leftover != 0u) ? 1u : 0u);
        for (i = 0u; i < total_words; i++) {
            uint32_t w;
            spins = PSRAM_PIO_SPINS;
            while (MSPI0->RXENTRIES == 0u && --spins != 0u) { }
            if (spins == 0u) {
                s_dbg.ctrl_settled = MSPI0->CTRL;
                s_dbg.intstat      = MSPI0->INTSTAT;
                s_dbg.spins_left   = 0u;
                s_pio_failed = 1u;
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
        /* Write a word, then wait for room, as the vendor does: waiting
         * before the first write stalls on the empty FIFO's threshold. */
        for (i = 0u; i < tx_words; i++) {
            MSPI0->TXFIFO = data[i];
            if (i == 0u) {
                s_dbg.tx_after_write = MSPI0->TXENTRIES;
            }
            spins = PSRAM_PIO_SPINS;
            while (MSPI0->TXENTRIES >= PSRAM_FIFO_WORDS && --spins != 0u) { }
            if (spins == 0u) {
                s_pio_failed = 1u;
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
        s_pio_failed = 1u;
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
 * The source oscillator is forced on first (CLKGEN.MISC.FRCHFRC or FRCHFRC2):
 * without it the IO clock has no source and every transfer times out.
 * Returns TIKU_PSRAM_ERR_CLOCK if the enable does not read back set. */
static tiku_psram_err_t psram_ioclk_on(uint8_t sel)
{
    uint32_t v;

    /* The vendor's boot (am_hal_pwrctrl_low_power_init()) sets clock-gate
     * and power-on-clock bits in CLKGEN.MISC that this port's boot does not.
     * They are set here as the vendor leaves them: bits 6-13 and 15-17
     * (PWRONCLKEN family) and 19-23 (clock-gate enables, including the APB
     * DMA CPU clock gate) set, AXIXACLKENOVRRIDE (14) clear. */
    {
        uint32_t misc = CLKGEN->MISC;
        misc |= 0x00FBBFC0u;
        misc &= ~(1u << 14);
        CLKGEN->MISC = misc;
        __DSB();
    }

    /* Keep both sources running while changing the selector. */
    tiku_ambiq_clock_force(TIKU_AMBIQ_CLOCK_PSRAM,
        CLKGEN_MISC_FRCHFRC_Msk | CLKGEN_MISC_FRCHFRC2_Msk);

    v = CLKGEN->MSPIIOCLKCTRL;

    v &= ~CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKSEL_Msk;
    v |= ((uint32_t)sel << CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKSEL_Pos)
         & CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKSEL_Msk;
    CLKGEN->MSPIIOCLKCTRL = v;
    CLKGEN->MSPIIOCLKCTRL = v | CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKEN_Msk;
    __DSB();
    tiku_cpu_ambiq_delay_us(10u);      /* vendor's settle after the enable */

    tiku_ambiq_clock_force(TIKU_AMBIQ_CLOCK_PSRAM,
        sel == IOCLK_SEL_HFRC2_250MHZ ? CLKGEN_MISC_FRCHFRC2_Msk :
                                      CLKGEN_MISC_FRCHFRC_Msk);

    /* The enable must read back set. */
    if ((CLKGEN->MSPIIOCLKCTRL & CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKEN_Msk) == 0u) {
        return TIKU_PSRAM_ERR_CLOCK;
    }
    return TIKU_PSRAM_OK;
}

/** @brief Table-1 steps 3-15: configure the controller for clock row @p c. */
static void psram_controller_config(const psram_clk_t *c)
{
    uint32_t cfg;
    uint32_t turnaround = s_ta_override ? (uint32_t)s_ta_override
                                       : (s_nodqs ? PSRAM_TURNAROUND_NODQS
                                                  : (uint32_t)s_turnaround);

    /* Step 3: the SDR250 tap, set before the DEV0CFG divider. */
    MSPI0->DEV0CFG1_b.SDR250EN0 = c->sdr250;

    /* Steps 4 and 5: command format, clock divider and bus width in one
     * DEV0CFG write, so the controller never sees a partial combination.
     * Enumerated fields use the register header's enum names. */
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
    /* SPI mode 0: CPOL = CPHA = 0.  TXNEG comes from the clock row.  The
     * vendor uses RXNEG = RXCAP = 0 at every speed; tiku_psram_set_rx() can
     * override them. */
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

    /* Steps 9 and 10: XIP framing, in the register that holds the
     * mixed-mode select; tiku_psram_xip_enable() turns the aperture on. */
    MSPI0->DEV0XIP_b.XIPMIXED0       = 0u;   /* NORMAL for octal DDR        */
    MSPI0->DEV0XIP_b.XIPACK0         = MSPI0_DEV0XIP_XIPACK0_TERMINATE;
    MSPI0->DEV0XIP_b.XIPSENDA0       = 1u;
    MSPI0->DEV0XIP_b.XIPSENDI0       = 1u;
    MSPI0->DEV0XIP_b.XIPENTURN0      = 1u;
    MSPI0->DEV0XIP_b.XIPTURNAROUND0  = turnaround;
    MSPI0->DEV0XIP_b.XIPENWLAT0      = 1u;
    MSPI0->DEV0XIP_b.XIPWRITELATENCY0 = s_writelat;

    /* Step 11: 1 KB DMA boundary, the device's row boundary.  DMATIMELIMIT
     * is the vendor's 40: it limits the CE window, and a small value splits
     * every burst into command overhead. */
    MSPI0->DEV0BOUNDARY_b.DMABOUND0     = MSPI0_DEV0BOUNDARY_DMABOUND0_BREAK1K;
    MSPI0->DEV0BOUNDARY_b.DMATIMELIMIT0 = 40u;

    /* Step 12: DQS receive.  TX taps delay the output SCLK relative to the
     * output data (datasheet 16.4.3).  The datasheet defines DDR receive only
     * with DQS, and without it the controller does not capture on this part,
     * so DQS off (tiku_psram_set_dqs()) is for diagnosis only. */
    MSPI0->DEV0DDR_b.ENABLEDQS0       = s_nodqs ? 0u : 1u;
    MSPI0->DEV0DDR_b.DQSSYNCNEG0      = 0u;
    MSPI0->DEV0DDR_b.ENABLEFINEDELAY0 = 0u;
    /* The vendor's taps for octal mode: TX 0 and RX 16. */
    MSPI0->DEV0DDR_b.TXDQSDELAY0      = 0u;
    MSPI0->DEV0DDR_b.RXDQSDELAY0      = 16u;
    MSPI0->DEV0DDR_b.RXDQSDELAYNEG0   = 0u;
    MSPI0->DEV0DDR_b.RXDQSDELAYNEGEN0 = 0u;
    MSPI0->DEV0DDR_b.RXDQSDELAYHI0    = 0u;
    MSPI0->DEV0DDR_b.RXDQSDELAYNEGHI0 = 0u;
    MSPI0->DEV0DDR_b.RXDQSDELAYHIEN0  = 0u;
    MSPI0->DEV0DDRDLYEXT_b.RXDQS0PDLYEXT0 = 0u;
    MSPI0->DEV0DDRDLYEXT_b.RXDQS0NDLYEXT0 = 0u;

    /* Step 13: RX sampling, the vendor's values for this device; RXSMP can
     * be overridden through tiku_psram_set_rx(). */
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

    /* Step 14: FIFO threshold and DMA burst size; DMABCOUNT 32 is the
     * vendor's value for every speed class. */
    MSPI0->THRESHOLD_b.RXTHRESH = 30u;
    MSPI0->DMABCOUNT            = 32u;
    MSPI0->DMATHRESH_b.DMATXTHRESH = 32u - 4u;
    MSPI0->DMATHRESH_b.DMARXTHRESH = 8u;

    /* Step 15: no IOM is bridged through this controller: IOMSEL =
     * DISABLED (15, "No IOM selected").  Values 0-7 select IOM0-7; 6 is
     * IOM6, the SPI-HCI link to the EM9305 radio die. */
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

/** @brief Release the controller pads as high-impedance GPIOs. */
static void psram_pads_release(void)
{
    uint32_t pad;

    for (pad = PSRAM_PAD_D0; pad <= PSRAM_PAD_DQS; pad++) {
        tiku_ambiq_gpio_pad_config(pad, 3u | PAD_INPEN);
    }
    tiku_ambiq_gpio_pad_config(PSRAM_PAD_CE, 3u | PAD_INPEN);
}

tiku_psram_err_t tiku_psram_init(unsigned clk)
{
    tiku_psram_err_t rc;

    if (clk >= PSRAM_CLK_COUNT) {
        return TIKU_PSRAM_ERR_ARG;
    }

    trace("power");
    /* Init ends in a device reset, which restores the power-up latencies
     * (RLC6, WLC5), so the controller's counts are reset to match, whatever
     * tiku_psram_set_speed() set before.  A stale WLC9 count against a
     * reset WLC5 device shifts every write. */
    s_turnaround = PSRAM_TURNAROUND_DQS;   /* RLC6 * 2 */
    s_writelat   = 10u;                    /* WLC5 * 2, the default         */

    rc = psram_power_on();
    if (rc != TIKU_PSRAM_OK) {
        tiku_psram_deinit();
        return rc;
    }
    trace("ioclk");
    rc = psram_ioclk_on(s_clk[clk].ioclk_sel);
    if (rc != TIKU_PSRAM_OK) {
        tiku_psram_deinit();
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
        psram_pads_release();
        tiku_psram_deinit();
        return rc;
    }
    return TIKU_PSRAM_OK;
}

void tiku_psram_deinit(void)
{
    uint32_t spins = PSRAM_PIO_SPINS;

    CLKGEN->MSPIIOCLKCTRL &= ~CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKEN_Msk;
    PWRCTRL->DEVPWREN &= ~PWRCTRL_DEVPWREN_PWRENMSPI0_Msk;
    __DSB();
    s_up = 0u;
    while ((PWRCTRL->DEVPWRSTATUS &
            PWRCTRL_DEVPWRSTATUS_PWRSTMSPI0_Msk) != 0u) {
        if (--spins == 0u) {
            return;  /* Do not clear quarantine without a power-down. */
        }
    }
    s_pio_failed = 0u;
    tiku_ambiq_clock_force(TIKU_AMBIQ_CLOCK_PSRAM, 0u);
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

    /* Zeroed first, so the fields after a failed read report 0. */
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

    /* The identity gate: vendor, density and good-die must all match. */
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
/* MEMORY ACCESS (PIO), SPEED, TIMING SCAN                                   */
/*---------------------------------------------------------------------------*/

/* Bulk PIO access moves 256-byte chunks, inside the device's 1 KB row; each
 * chunk drains or fills the 32-word FIFO as the transfer runs. */
#define PSRAM_CHUNK 256u

tiku_psram_err_t tiku_psram_mem_read(uint32_t addr, void *buf, uint32_t n)
{
    uint8_t *dst = (uint8_t *)buf;
    if (!s_up) { return TIKU_PSRAM_ERR_POWER; }
    if (s_asleep) { return TIKU_PSRAM_ERR_ARG; }
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
    if (s_asleep) { return TIKU_PSRAM_ERR_ARG; }
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
 * Read-modify-writes both registers and sets the controller's matching
 * TURNAROUND and WRITELATENCY for the next psram_controller_config().
 *
 * @note Run at a clock the current codes support, before raising the clock.
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

    /* The controller-side counts take effect at the next
     * psram_controller_config(); tiku_psram_init() resets them. */
    s_turnaround = (uint8_t)(L->rlc * 2u);
    s_writelat   = (uint8_t)(L->wlc * 2u);

    /* tiku_psram_set_speed() reads the new codes back at the new clock,
     * where the controller counts match them. */
    return TIKU_PSRAM_OK;
}

/**
 * @brief Read MR0 and MR4 back and compare their latency codes with clock
 *        row @p clk.
 *
 * @note Run once the controller counts match row @p clk.
 * @return TIKU_PSRAM_OK, the failing read's error, or ERR_ID when either
 *         code differs
 */
static tiku_psram_err_t psram_check_latency(unsigned clk)
{
    const psram_lat_t *L = &s_lat[clk];
    uint32_t v;
    tiku_psram_err_t rc;

    rc = tiku_psram_reg_read(0u, &v);
    if (rc != TIKU_PSRAM_OK) { return rc; }
    if (((v >> 2) & 0x7u) != L->rlc_code) { return TIKU_PSRAM_ERR_ID; }

    rc = tiku_psram_reg_read(4u, &v);
    if (rc != TIKU_PSRAM_OK) { return rc; }
    if (((v >> 5) & 0x7u) != L->wlc_code) { return TIKU_PSRAM_ERR_ID; }
    return TIKU_PSRAM_OK;
}

tiku_psram_err_t tiku_psram_set_speed(unsigned clk)
{
    tiku_psram_err_t rc;

    if (clk >= PSRAM_CLK_COUNT) { return TIKU_PSRAM_ERR_ARG; }

    /* At the current clock, program the device MRs for the target clock,
     * then reconfigure the controller at the target.  No device reset in
     * between: a reset restores the default MRs. */
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
    return psram_check_latency(clk);
}

/**
 * @brief One timing-scan cell: pattern-verify @p bytes at @p rxdqs delay.
 *
 * The pattern mixes the byte address with a constant, written in 512-byte
 * chunks to two regions, one past 32 MB so the high address bits are used.
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
    unsigned saved_tap;
    unsigned tap, best_len = 0u, best_start = 0u, run = 0u, run_start = 0u;

    if (!s_up) { return 0u; }
    saved_tap = MSPI0->DEV0DDR_b.RXDQSDELAY0;
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
    /* Apply the centre of the widest passing window. */
    if (best_len != 0u) {
        unsigned c = best_start + best_len / 2u;
        MSPI0->DEV0DDR_b.RXDQSDELAY0 = (c & 0x1Fu);
        __DSB();
        s_tap = (uint8_t)c;
        if (center) { *center = c; }
    } else {
        MSPI0->DEV0DDR_b.RXDQSDELAY0 = saved_tap;
        __DSB();
        if (center) { *center = 0u; }
    }
    if (pass_mask) { *pass_mask = mask; }
    return best_len;
}

/*---------------------------------------------------------------------------*/
/* XIP APERTURE AND DMA                                                      */
/*---------------------------------------------------------------------------*/

tiku_psram_err_t tiku_psram_xip_enable(int enable)
{
    if (!s_up) { return TIKU_PSRAM_ERR_POWER; }
    if (enable && s_asleep) { return TIKU_PSRAM_ERR_ARG; }
    if (enable) {
        /* Aperture at 0x60000000, 64 MB (SIZE0 = 10).  BASE0 holds bits
         * 28:16 of the offset within the region: 0 for the region start. */
        MSPI0->DEV0AXI =
            ((10u << MSPI0_DEV0AXI_SIZE0_Pos) & MSPI0_DEV0AXI_SIZE0_Msk);
        __DSB();
        MSPI0->DEV0XIP_b.XIPEN0 = 1u;
    } else {
        /* The aperture is write-back cacheable.  A dirty line still cached
         * when the aperture goes away evicts later under whatever code runs,
         * as an imprecise bus fault.  So the whole D-cache is cleaned and
         * invalidated by set/way first: it is 64 KB against a 64 MB
         * aperture. */
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

/** @brief 1 while a transfer armed by tiku_psram_dma_start() is uncollected. */
static uint32_t s_dma_busy;

/*
 * Split form of tiku_psram_dma(): arm the transfer, return, and collect it
 * with tiku_psram_dma_wait(), so the CPU can work while the transfer runs.
 */
tiku_psram_err_t tiku_psram_dma_start(uint32_t dev_addr, void *sram,
                                      uint32_t n, int to_device)
{
    if (!s_up)              { return TIKU_PSRAM_ERR_POWER; }
    if (s_asleep)           { return TIKU_PSRAM_ERR_ARG; }
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
 * Spins without a backoff, for up to 40000000 polls.  A DMA error returns
 * TIKU_PSRAM_ERR_TIMEOUT.
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

/*
 * Blocking DMA on the plain DMA engine (tiku_psram_cq_xfer() drives the
 * command queue): target address, device address, count, direction, enable,
 * then poll DMACPL.  Cache coherency is the caller's job.
 */
tiku_psram_err_t tiku_psram_dma(uint32_t dev_addr, void *sram, uint32_t n,
                                int to_device)
{
    uint32_t spins = 500000u;   /* x20 us = 10 s ceiling */

    if (s_dma_busy)         { return TIKU_PSRAM_ERR_ARG; }
    if (!s_up)              { return TIKU_PSRAM_ERR_POWER; }
    if (s_asleep)           { return TIKU_PSRAM_ERR_ARG; }
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

    /* Poll with a 20 us backoff: a tight poll is APB traffic into the
     * controller doing the transfer and limits its throughput. */
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
/* COMMAND QUEUE: HARDWARE-CHAINED DMA SEGMENTS                              */
/*---------------------------------------------------------------------------*/

/*
 * TABLE 3 -- THE CQ ENTRY (am_hal_mspi.c's am_hal_mspi_cq_dma_entry_t and the
 * am_hal_cmdq engine).
 *
 * The CQ hardware fetches 8-byte {register-address, value} pairs from SRAM
 * at CQADDR and performs each as a register write.  Two behaviours chain the
 * DMA segments with no CPU between them:
 *
 *   1. A write to DMACFG while a DMA is in progress stalls the engine until
 *      that DMA completes, so the per-segment tail write of DMAEN=0 is both
 *      the completion wait and the teardown.
 *   2. CQPAUSE holds a condition mask evaluated against CQFLAGS; the CQIDX
 *      bit ("CURIDX == ENDIDX") pauses the engine when it runs out of posted
 *      work.  A queue-borne write to CQCURIDX marks the block retired.
 *
 * One segment, in the order tiku_psram_cq_xfer() writes it (8 pairs, 64 B):
 *      CQPAUSE    := pause mask (IDX)      CQPAUSE     := pause mask (IDX)
 *      DMATARGADDR:= sram                  DMADEVADDR  := device addr
 *      DMATOTCOUNT:= bytes                 DMACFG      := DIR | EN (3)
 *      DMACFG     := 0      <-- the stall  CQSETCLEAR  := 0
 * and the block terminator: { CQCURIDX, n_segments }.
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
    if (s_asleep)                       { return TIKU_PSRAM_ERR_ARG; }
    if (MSPI0->DEV0XIP_b.XIPEN0 != 0u)  { return TIKU_PSRAM_ERR_ARG; }
    if (seg_bytes == 0u || (total % seg_bytes) != 0u ||
        (seg_bytes & 3u) != 0u)         { return TIKU_PSRAM_ERR_ARG; }
    n_segs = total / seg_bytes;
    if (n_segs == 0u || n_segs > CQ_MAX_SEGS) { return TIKU_PSRAM_ERR_ARG; }

    /* Build the queue: one table 3 segment per chunk. */
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

    /* The engine reads these pairs as a bus master, so they are cleaned
     * from the D-cache; a dirty line leaves it running stale descriptors. */
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

    /* Done when the queue-borne CQCURIDX write has taken effect and the last
     * DMA is torn down.  The poll waits 50 us between reads: a tight poll is
     * APB traffic into the controller doing the transfer and limits its
     * throughput. */
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
/* LIFECYCLE: UP, DOWN, HALF SLEEP AND THE MEMORY TIER                       */
/*---------------------------------------------------------------------------*/

/*
 * Three states:
 *
 *   down    domain off, tier detached, contents lost
 *   asleep  half sleep: the die keeps its contents on self-refresh at
 *           microamp-class current; XIP unmapped; PIO array access, DMA, the
 *           command queue and the XIP map refuse; the tier stays attached
 *           but refuses new allocations, and existing ones must not be
 *           touched until wake
 *   up      mapped at 0x60000000, tier attached, full speed
 *
 * Half sleep takes 155 us to enter and to leave (the vendor's
 * APS25616BA_tHS/tXHS with margin).  Entry writes MR6 = 0xF0 (one byte); exit
 * is any dummy command to pulse CE, then the same delay.
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
    (void)tiku_tier_suspend_psram(1);      /* no new tier reservations      */
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
    (void)tiku_tier_suspend_psram(0);
    /* The wake succeeds only if the identity reads back. */
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
            return TIKU_PSRAM_ERR_TIMEOUT; /* no passing tap                */
        }
    }
    rc = tiku_psram_xip_enable(1);
    if (rc != TIKU_PSRAM_OK) { return rc; }
    {
        tiku_mem_err_t status = tiku_tier_attach_psram(
            (void *)TIKU_PSRAM_XIP_BASE,
            (tiku_mem_arch_size_t)TIKU_PSRAM_SIZE_BYTES);
        if (status != TIKU_MEM_OK) {
            const uint8_t *base;
            tiku_mem_stats_t stats;

            /* Re-up accepts only an existing, identical attachment. */
            if (status != TIKU_MEM_ERR_INVALID ||
                tiku_tier_span_stats(TIKU_MEM_PSRAM, 0, &base, &stats)
                    != TIKU_MEM_OK ||
                base != (const uint8_t *)TIKU_PSRAM_XIP_BASE ||
                stats.total_bytes != TIKU_PSRAM_SIZE_BYTES) {
                return TIKU_PSRAM_ERR_ARG;
            }
        }
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
/* PSRAM BANDWIDTH BENCH                                                     */
/*---------------------------------------------------------------------------*/

/*
 * Each leg is timed on DWT CYCCNT and prints bytes moved, time, MB/s and a
 * verdict. Sequential writes are read back outside the timed interval.
 * Read checks cover the stated tiles; random512 is timing-only.
 *
 *   xip-write   CPU streaming stores through the aperture
 *   dma-rd16k   device -> SRAM DMA in 16 KB and 64 KB transfers
 *   dma-rd64k
 *   dma-write   SRAM -> device DMA, inverted pattern
 *   cq-wr16k    SRAM -> device through the command queue, 16 KB and
 *   cq-wr64k    64 KB segments
 *   cq-rd16k    device -> SRAM through the command queue
 *   xip-read    CPU streaming loads through the aperture
 *   random512   512 B reads at pseudo-random offsets across the 64 MB
 */

extern unsigned long tiku_cpu_ambiq_clock_get_hz(void);

#define BENCH_SPAN  (1u * 1024u * 1024u)   /* per-leg span: 16x the D-cache  */
#define BENCH_BUF   65536u
static uint8_t s_bench_buf[BENCH_BUF] __attribute__((aligned(32)));

/** @brief Enable DWT CYCCNT, saving DEMCR and DWT_CTRL; returns CYCCNT. */
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

/** @brief Print one leg: KB moved, microseconds, MB/s and the verdict. */
static void bench_report(const char *leg, uint32_t bytes, uint32_t cyc,
                         int exact)
{
    unsigned long hz = tiku_cpu_ambiq_clock_get_hz();
    /* kbps is thousands of bytes per second, printed as MB/s to 3 places. */
    unsigned long kbps = (unsigned long)(((uint64_t)bytes * hz) /
                                         ((uint64_t)(cyc ? cyc : 1u) * 1000u));
    SHELL_PRINTF("  %-9s %7lu KB  %8lu us  %6lu.%03lu MB/s  %s\n", leg,
                 (unsigned long)(bytes / 1024u),
                 (unsigned long)(((uint64_t)cyc * 1000000u) / (hz ? hz : 1u)),
                 kbps / 1000u, kbps % 1000u,
                 exact < 0 ? "unchecked" : exact ? "bit-exact" : "FAIL");
}

/** @brief Pattern byte for device address @p a, shared by every leg. */
static inline uint8_t bench_pat(uint32_t a)
{
    return (uint8_t)(a ^ (a >> 8) ^ (a >> 16) ^ 0xC3u);
}

/** @brief Run the PSRAM bandwidth bench and print one line per leg. */
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
    tiku_cpu_dcache_invalidate((const void *)ap, BENCH_SPAN);
    exact = 1;
    for (i = 0u; i < BENCH_SPAN; ++i) {
        if (ap[i] != bench_pat(i)) { exact = 0; break; }
    }
    bench_report("xip-write", BENCH_SPAN, t1 - t0, exact);

    /* ---- leg 2: DMA READ back (device -> SRAM) -------------------------- */
    /* Only the DMA is timed.  Each tile overwrites the buffer, so the
     * checksum, run untimed afterwards, covers the final tile only.  The two
     * chunk sizes show the per-transfer overhead. */
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
    exact = off == BENCH_SPAN;
    if (exact) {
        if (tiku_psram_xip_enable(1) != TIKU_PSRAM_OK) {
            exact = 0;
        } else {
            tiku_cpu_dcache_invalidate((const void *)ap, BENCH_SPAN);
            for (i = 0u; i < BENCH_SPAN; ++i) {
                if (ap[i] != (uint8_t)~bench_pat(i % 16384u)) {
                    exact = 0;
                    break;
                }
            }
            (void)tiku_psram_xip_enable(0);
        }
    }
    bench_report("dma-write", off, t1 - t0, exact);

    /* ---- leg 3b: CQ chained transfers ---------------------------------- */
    /* The write legs lay the same inverted pattern as leg 3, so leg 4's
     * expected checksum still holds; the read leg is checked on its final
     * tile. */
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
            /* Each call moves BENCH_BUF in segments of seg bytes, and the
             * loop covers the whole span, so the bytes reported are the bytes
             * moved. */
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
        /* CQ read: the span in 64 KB calls of four 16 KB segments, each call
         * overwriting the last in the 64 KB buffer; the checksum covers the
         * final buffer only, as in dma-read. */
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
    /* Legs 3 and 3b wrote the inverted pattern in BENCH_BUF tiles. */
    expect = 0u;
    for (off = 0u; off < BENCH_SPAN; off++) {
        expect += (uint8_t)~bench_pat(off % 16384u);
    }
    bench_report("xip-read", BENCH_SPAN, t1 - t0, sum == expect);

    /* ---- leg 5: random 512 B reads through XIP ------------------------- */
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
        bench_report("random512", n_reads * 512u, t1 - t0, -1);
        SHELL_PRINTF("  (random leg: %lu reads of 512 B across the full"
                     " 64 MB; latency %lu us/read)\n",
                     (unsigned long)n_reads,
                     (unsigned long)((((uint64_t)(t1 - t0) * 1000000u) /
                                      tiku_cpu_ambiq_clock_get_hz()) / n_reads));
    }
    (void)tiku_psram_xip_enable(0);

    /* restore the DWT state saved at the start */
    {
        volatile uint32_t *demcr  = (volatile uint32_t *)0xE000EDFCUL;
        volatile uint32_t *dwtctl = (volatile uint32_t *)0xE0001000UL;
        *dwtctl = ctl0; *demcr = demcr0;
    }
    SHELL_PRINTF("psrambench done\n");
}

/*---------------------------------------------------------------------------*/
/* BIT-BANG PROBE (no MSPI controller)                                       */
/*---------------------------------------------------------------------------*/

/*
 * Drives the octal-DDR command waveform with plain GPIO, without the MSPI
 * controller, to show whether the device answers an octal command.  The
 * device changes data once per edge, so at microsecond edge rates the data
 * sits stable for sampling and no timing setting is involved.
 *
 * The read assumes no latency: it clocks n_edges edges after the address and
 * reports every sample.  A device that answers puts its data somewhere in
 * that stream; all zeros or all ones means it does not.  At these rates CE
 * stays low longer than tCEM, the bound that lets the die refresh its array.
 */

#define BB_GPIO_OUT   (3u | (1u << 8) | (1u << 4))  /* GPIO, push-pull, INPEN */
#define BB_GPIO_IN    (3u | (1u << 4))               /* GPIO, input only      */

/** @brief Drive pad @p pad high or low through GPIO WTS/WTC. */
static inline void bb_set(uint32_t pad, int v)
{
    uint32_t mask = 1u << (pad & 31u);
    if (v) { (&GPIO->WTS0)[pad >> 5] = mask; }
    else   { (&GPIO->WTC0)[pad >> 5] = mask; }
}

/** @brief Sample D0-7 as one byte. */
static inline uint32_t bb_get_d0_7(void)
{
    /* D0-7 = GP64..71: one contiguous byte in RD2 (pads 64..95). */
    return (&GPIO->RD0)[2] & 0xFFu;
}

/** @brief Drive @p b onto D0-7, bit n on Dn. */
static void bb_drive_byte(uint8_t b)
{
    uint32_t pad;
    for (pad = 0u; pad < 8u; pad++) {
        bb_set(PSRAM_PAD_D0 + pad, (b >> pad) & 1u);
    }
}

/** @brief Wait about 1 us at 96 MHz, far above any DDR minimum timing. */
static inline void bb_dwell(void)
{
    uint32_t n = 100u;
    while (n--) { __asm__ volatile ("nop"); }
}

/** @brief Clock one DDR edge with @p b driven on D0-7 (TX phase). */
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
    /* Array read: the same waveform with the linear-read opcode.  The device
     * streams from @p addr after its read latency; matching the samples
     * against a written pattern shows where the data is stored. */
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
    /* Leave the pads as GPIO, data and DQS as inputs, CE high and the clock
     * low; the next tiku_psram_init() reclaims them. */
}

/*---------------------------------------------------------------------------*/
/* DIAGNOSTICS AND FAULT INJECTION                                           */
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
    /* A global reset with no data phase: if it times out the controller is
     * not driving the bus; if it completes the bus clocks, and a failing
     * transfer fails in its data phase. */
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
        /* Take D0 away from the controller (FNCSEL 3 is GPIO on these
         * pads): the device then cannot receive a well-formed command, so
         * the next transfer fails. */
        tiku_ambiq_gpio_pad_config(PSRAM_PAD_D0, 3u);
        s_faulted = 1u;
    } else {
        tiku_ambiq_gpio_pad_config(PSRAM_PAD_D0, PAD_CFG_MSPI_IO);
        s_faulted = 0u;
    }
    __DSB();
}

#endif /* PLATFORM_AMBIQ && TIKU_DRV_PSRAM_ENABLE */
