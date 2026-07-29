/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nor_arch.c - Apollo510 MSPI1 and IS25WX064 octal NOR bring-up.
 *
 * A NOR wakes in 1-line SPI and must be talked into octal, cannot clear bits
 * without a slow destructive erase, and remembers everything through a power
 * cycle.  Those facts shape every function here.  PIO and XIP never mix.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#if defined(PLATFORM_AMBIQ) && (TIKU_DRV_NOR_ENABLE + 0)

#include "tiku_nor_arch.h"
#include "tiku_gpio_arch.h"      /* tiku_ambiq_gpio_pad_config(), gpio_set  */
#include "tiku_cpu_common.h"     /* tiku_cpu_ambiq_delay_us()               */
#include "apollo510.h"           /* CMSIS register map -- defs only         */
#include <kernel/cpu/tiku_hang.h>   /* erase waits block on purpose         */

/*---------------------------------------------------------------------------*/
/* COMMANDS (table 2)                                                        */
/*---------------------------------------------------------------------------*/

/* serial (1-byte opcodes) */
#define NOR_CMD_READ_ID        0x9Fu
#define NOR_CMD_RESET_ENABLE   0x66u
#define NOR_CMD_RESET_MEMORY   0x99u
#define NOR_CMD_WREN           0x06u
#define NOR_CMD_WRDI           0x04u
#define NOR_CMD_READ_STATUS    0x05u
#define NOR_CMD_READ_FLAGSTAT  0x70u
#define NOR_CMD_ENTER_4B       0xB7u
#define NOR_CMD_READ_VCR       0x85u
#define NOR_CMD_WRITE_VCR      0x81u
#define NOR_CMD_READ_NVCR      0xB5u
#define NOR_CMD_FAST_READ_4B   0x0Cu
#define NOR_CMD_PAGE_PROG_4B   0x12u
#define NOR_CMD_SUBSEC_ERASE   0x20u
#define NOR_CMD_SECTOR_ERASE   0xD8u
/* NOTE: 0xB1 (write non-volatile CR) and 0xC7 (chip erase) are deliberately
 * ABSENT.  One is permanent, the other destroys the whole die; neither has
 * any business being reachable from a driver that runs unattended. */

/* octal DDR (2-byte duplicated opcodes) */
#define NOR_OCMD_READ          0xFDFDu
#define NOR_OCMD_PAGE_PROG     0x1212u
#define NOR_OCMD_SUBSEC_ERASE  0x2121u
#define NOR_OCMD_SECTOR_ERASE  0xD8D8u
#define NOR_OCMD_WREN          0x0606u
#define NOR_OCMD_WRDI          0x0404u
#define NOR_OCMD_READ_STATUS   0x0505u
#define NOR_OCMD_READ_ID       0x9F9Fu

/* config-register values */
#define NOR_VCR_IO_OCTAL_DDR   0xE7u   /* volatile CR[0] -> octal DDR       */
#define NOR_NVCR6_XIP_DISABLE  0xFFu   /* non-volatile CR[6] expected value */

/* status bits */
#define NOR_STATUS_WIP         0x01u
#define NOR_FLAG_PROG_ERR      0x10u
#define NOR_FLAG_ERASE_ERR     0x20u

/*---------------------------------------------------------------------------*/
/* PADS (table 0)                                                            */
/*---------------------------------------------------------------------------*/

#if !defined(TIKU_BOARD_NOR_PAD_D0)
#error "This board declares no NOR pads (TIKU_BOARD_NOR_PAD_*). The build \
system should not have compiled tiku_nor_arch.c for it -- see BOARD_CAPS/NOR \
in the Makefile."
#endif
#define NOR_PAD_D0     TIKU_BOARD_NOR_PAD_D0
#define NOR_PAD_D7     TIKU_BOARD_NOR_PAD_D7
#define NOR_PAD_CLK    TIKU_BOARD_NOR_PAD_CLK
#define NOR_PAD_DQS    TIKU_BOARD_NOR_PAD_DQS
#define NOR_PAD_CE     TIKU_BOARD_NOR_PAD_CE
/*
 * RSTn and the load switch also come from the board header now.
 *
 * There USED to be a `#if defined(TIKU_BOARD_APOLLO510_EVB) / #else` here
 * carrying a second set of pads for the Blue EVB (RST GP17, no load switch),
 * kept "so the driver still compiles on that board".  After the capability
 * split that branch is unreachable: the Blue board declares no NOR cap, so
 * TIKU_DRV_NOR_ENABLE=1 is refused at make time and this file is never
 * compiled for it.  Carrying pin numbers for a part that is not on the board
 * is the fiction the split exists to end, so the branch is gone rather than
 * left to rot -- if a future board fits a NOR, it declares the cap and its own
 * pads, and this driver needs no edit at all.
 */
#define NOR_PAD_RST    TIKU_BOARD_NOR_PAD_RST
#define NOR_PAD_LSEN   TIKU_BOARD_NOR_PAD_LSEN

#define PAD_FNCSEL_MSPI1     0u
#define PAD_FNCSEL_MNCE1     0u
#define PAD_FNCSEL_GPIO      3u
#define PAD_DS_0P5X         (1u << 10)
#define PAD_OUTCFG_PUSHPULL (1u << 8)
#define PAD_INPEN           (1u << 4)

#define PAD_CFG_MSPI_IO  (PAD_FNCSEL_MSPI1 | PAD_DS_0P5X | PAD_INPEN)
#define PAD_CFG_MSPI_CE  (PAD_FNCSEL_MNCE1 | PAD_DS_0P5X | PAD_OUTCFG_PUSHPULL)
#define PAD_CFG_GPIO_OUT (PAD_FNCSEL_GPIO  | PAD_DS_0P5X | PAD_OUTCFG_PUSHPULL \
                          | PAD_INPEN)

/*---------------------------------------------------------------------------*/
/* CLOCKS                                                                    */
/*---------------------------------------------------------------------------*/

/* Same derived model as the PSRAM: out = source / (2 * CLKDIV), and TXNEG is
 * chosen BY FREQUENCY (0 at <= 62.5 MHz, 1 above). */
#define IOCLK_SEL_HFRC_192MHZ  8u

typedef struct {
    uint8_t  ioclk_sel, clkdiv, txneg;
    uint32_t hz;
} nor_clk_t;

static const nor_clk_t s_clk[] = {
    { IOCLK_SEL_HFRC_192MHZ, 4u, 0u, 24000000u },
    { IOCLK_SEL_HFRC_192MHZ, 2u, 0u, 48000000u },
    { IOCLK_SEL_HFRC_192MHZ, 1u, 1u, 96000000u },
};
#define NOR_CLK_COUNT (sizeof s_clk / sizeof s_clk[0])

/* Latency: serial fast-read needs 8 dummy cycles; octal DDR needs the
 * vendor's 31 (its default dummy-cycle configuration).  Program has no
 * turnaround in either mode. */
#define NOR_TA_SERIAL  8u
#define NOR_TA_OCTAL  31u

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

static uint8_t  s_up;          /**< controller configured                   */
static uint8_t  s_octal;       /**< device+controller in octal DDR          */
static uint8_t  s_clk_idx;
static uint32_t s_erases;      /**< lifetime erase counter (this boot)      */
static void   (*s_trace)(const char *step);

static void trace(const char *step) { if (s_trace) { s_trace(step); } }

void tiku_nor_set_trace(void (*fn)(const char *step)) { s_trace = fn; }
uint32_t tiku_nor_erase_count(void) { return s_erases; }
int tiku_nor_is_octal(void) { return s_octal ? 1 : 0; }
unsigned long tiku_nor_clock_hz(void)
{
    return s_up ? (unsigned long)s_clk[s_clk_idx].hz : 0uL;
}

/*---------------------------------------------------------------------------*/
/* PIO                                                                       */
/*---------------------------------------------------------------------------*/

#define NOR_PIO_SPINS   400000u
#define NOR_FIFO_WORDS  32u

/**
 * @brief One PIO command on MSPI1.
 *
 * Deliberately a near-copy of the PSRAM's psram_pio2 rather than a shared
 * helper: the two devices disagree about instruction width, latency and
 * which phases exist, and a single parameterised routine serving both would
 * hide exactly the differences that matter.  Same discipline though --
 * bounded everywhere, distinct errors, drain-as-you-go, XIP guard.
 *
 * @param instr    opcode (1 byte in serial, 2 duplicated bytes in octal)
 * @param addr     device address; ignored when @p send_addr is 0
 * @param data     word buffer in/out, may be NULL when n_bytes is 0
 * @param n_bytes  data phase length
 * @param is_read  non-zero for RX
 * @param send_addr non-zero to emit the address phase
 * @param turnaround non-zero to insert the read latency
 */
static tiku_nor_err_t nor_pio(uint16_t instr, uint32_t addr,
                              uint32_t *data, uint32_t n_bytes,
                              int is_read, int send_addr, int turnaround)
{
    uint32_t ctrl = 0u;
    uint32_t full_words = n_bytes / 4u;
    uint32_t leftover   = n_bytes - (full_words * 4u);
    uint32_t tx_words   = full_words + ((leftover != 0u) ? 1u : 0u);
    uint32_t total_words = full_words + ((leftover != 0u) ? 1u : 0u);
    uint32_t spins, i;

    /* The PSRAM's hardest-won guard: a PIO command while the aperture is
     * enabled deadlocks the APB and needs a power cycle. */
    if (MSPI1->DEV0XIP_b.XIPEN0 != 0u) {
        return TIKU_NOR_ERR_ARG;
    }

    MSPI1->INSTR = (uint32_t)instr;
    MSPI1->ADDR  = addr;

    ctrl |= (n_bytes << MSPI0_CTRL_XFERBYTES_Pos) & MSPI0_CTRL_XFERBYTES_Msk;
    ctrl |= MSPI0_CTRL_SENDI_Msk;
    if (send_addr) { ctrl |= MSPI0_CTRL_SENDA_Msk; }
    ctrl |= MSPI0_CTRL_START_Msk;
    if (is_read) {
        /* TXRX = 0 is RECEIVE.  See the file header. */
        if (turnaround) { ctrl |= MSPI0_CTRL_ENTURN_Msk; }
    } else {
        ctrl |= (1u << MSPI0_CTRL_TXRX_Pos) & MSPI0_CTRL_TXRX_Msk;
    }

    MSPI1->INTCLR = 0xFFFFFFFFu;
    MSPI1->CTRL   = ctrl;

    if (is_read && data != (uint32_t *)0) {
        for (i = 0u; i < total_words; i++) {
            uint32_t w;
            spins = NOR_PIO_SPINS;
            while (MSPI1->RXENTRIES == 0u && --spins != 0u) { }
            if (spins == 0u) { return TIKU_NOR_ERR_TIMEOUT; }
            w = MSPI1->RXFIFO;
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
        for (i = 0u; i < tx_words; i++) {
            MSPI1->TXFIFO = data[i];
            spins = NOR_PIO_SPINS;
            while (MSPI1->TXENTRIES >= NOR_FIFO_WORDS && --spins != 0u) { }
            if (spins == 0u) { return TIKU_NOR_ERR_TIMEOUT; }
        }
    }

    spins = NOR_PIO_SPINS;
    while (((MSPI1->CTRL & MSPI0_CTRL_STATUS_Msk) == 0u) && --spins != 0u) { }
    if (spins == 0u) { return TIKU_NOR_ERR_TIMEOUT; }
    return TIKU_NOR_OK;
}

/** @brief Opcode helper: the same command in whichever mode is live. */
static uint16_t nor_op(uint8_t serial_op, uint16_t octal_op)
{
    return s_octal ? octal_op : (uint16_t)serial_op;
}

/*---------------------------------------------------------------------------*/
/* CONTROLLER                                                                */
/*---------------------------------------------------------------------------*/

static tiku_nor_err_t nor_power_on(void)
{
    uint32_t spins = 100000u;
    PWRCTRL->DEVPWREN |= PWRCTRL_DEVPWREN_PWRENMSPI1_Msk;
    __DSB();
    while (((PWRCTRL->DEVPWRSTATUS & PWRCTRL_DEVPWRSTATUS_PWRSTMSPI1_Msk) == 0u)
           && --spins != 0u) { }
    return (spins != 0u) ? TIKU_NOR_OK : TIKU_NOR_ERR_POWER;
}

static tiku_nor_err_t nor_ioclk_on(uint8_t sel)
{
    uint32_t v;

    /* Replicate the vendor's CLKGEN.MISC clock-gate/power-on-clock block and
     * force the HFRC oscillator -- both learned on MSPI0. */
    {
        uint32_t misc = CLKGEN->MISC;
        misc |= 0x00FBBFC0u;
        misc &= ~(1u << 14);
        CLKGEN->MISC = misc | CLKGEN_MISC_FRCHFRC_Msk;
        __DSB();
    }

    /* MSPI1's field group sits one stride (5 bits) above MSPI0's. */
    v = CLKGEN->MSPIIOCLKCTRL;
    v &= ~(CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKSEL_Msk
           << CLKGEN_MSPIIOCLKCTRL_MSPI1IOCLKEN_Pos);
    v |= ((uint32_t)sel << (CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKSEL_Pos
                            + CLKGEN_MSPIIOCLKCTRL_MSPI1IOCLKEN_Pos))
         & (CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKSEL_Msk
            << CLKGEN_MSPIIOCLKCTRL_MSPI1IOCLKEN_Pos);
    CLKGEN->MSPIIOCLKCTRL = v;
    CLKGEN->MSPIIOCLKCTRL = v | (CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKEN_Msk
                                 << CLKGEN_MSPIIOCLKCTRL_MSPI1IOCLKEN_Pos);
    __DSB();
    tiku_cpu_ambiq_delay_us(10u);

    if ((CLKGEN->MSPIIOCLKCTRL & (CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKEN_Msk
                                  << CLKGEN_MSPIIOCLKCTRL_MSPI1IOCLKEN_Pos))
            == 0u) {
        return TIKU_NOR_ERR_CLOCK;
    }
    return TIKU_NOR_OK;
}

/** @brief Program the controller for serial (phase A) or octal DDR (B). */
static void nor_controller_config(const nor_clk_t *c, int octal)
{
    uint32_t cfg = 0u;
    uint32_t ta  = octal ? NOR_TA_OCTAL : NOR_TA_SERIAL;

    MSPI1->DEV0CFG1_b.SDR250EN0 = 0u;

    cfg |= ((uint32_t)MSPI0_DEV0CFG_ASIZE0_A4 << MSPI0_DEV0CFG_ASIZE0_Pos)
           & MSPI0_DEV0CFG_ASIZE0_Msk;
    cfg |= ((uint32_t)(octal ? MSPI0_DEV0CFG_ISIZE0_I16
                             : MSPI0_DEV0CFG_ISIZE0_I8)
            << MSPI0_DEV0CFG_ISIZE0_Pos) & MSPI0_DEV0CFG_ISIZE0_Msk;
    cfg |= (ta << MSPI0_DEV0CFG_TURNAROUND0_Pos)
           & MSPI0_DEV0CFG_TURNAROUND0_Msk;
    cfg |= ((uint32_t)c->clkdiv << MSPI0_DEV0CFG_CLKDIV0_Pos)
           & MSPI0_DEV0CFG_CLKDIV0_Msk;
    if (c->txneg) { cfg |= MSPI0_DEV0CFG_TXNEG0_Msk; }
    cfg |= ((uint32_t)(octal ? MSPI0_DEV0CFG_DEVCFG0_OCTAL0
                             : MSPI0_DEV0CFG_DEVCFG0_SERIAL0)
            << MSPI0_DEV0CFG_DEVCFG0_Pos) & MSPI0_DEV0CFG_DEVCFG0_Msk;
    MSPI1->DEV0CFG = cfg;

    MSPI1->DEV0DDR_b.EMULATEDDR0 = octal ? 1u : 0u;
    MSPI1->DEV0DDR_b.ENABLEDQS0  = octal ? 1u : 0u;
    MSPI1->DEV0DDR_b.TXDQSDELAY0 = 0u;
    MSPI1->DEV0DDR_b.RXDQSDELAY0 = 16u;

    MSPI1->PADOUTEN = octal ? MSPI0_PADOUTEN_OUTEN_OCTAL
                            : MSPI0_PADOUTEN_OUTEN_SERIAL0;

    MSPI1->DEV0INSTR =
        (((uint32_t)(octal ? NOR_OCMD_READ : NOR_CMD_FAST_READ_4B)
          << MSPI0_DEV0INSTR_READINSTR0_Pos) & MSPI0_DEV0INSTR_READINSTR0_Msk)|
        (((uint32_t)(octal ? NOR_OCMD_PAGE_PROG : NOR_CMD_PAGE_PROG_4B)
          << MSPI0_DEV0INSTR_WRITEINSTR0_Pos)& MSPI0_DEV0INSTR_WRITEINSTR0_Msk);

    MSPI1->DEV0XIP_b.XIPMIXED0        = 0u;
    MSPI1->DEV0XIP_b.XIPACK0          = MSPI0_DEV0XIP_XIPACK0_TERMINATE;
    MSPI1->DEV0XIP_b.XIPSENDA0        = 1u;
    MSPI1->DEV0XIP_b.XIPSENDI0        = 1u;
    MSPI1->DEV0XIP_b.XIPENTURN0       = 1u;
    MSPI1->DEV0XIP_b.XIPTURNAROUND0   = ta;
    MSPI1->DEV0XIP_b.XIPENWLAT0       = 0u;
    MSPI1->DEV0XIP_b.XIPWRITELATENCY0 = 0u;

    /* NO DMA BOUNDARY: this is flash, not DRAM -- there are no rows to break
     * bursts at, and the vendor agrees (BOUNDARY_NONE for this part).  That
     * makes this driver's bandwidth an independent read on whether the
     * PSRAM's per-KB cost really was row economics. */
    MSPI1->DEV0BOUNDARY_b.DMABOUND0     = 0u;
    MSPI1->DEV0BOUNDARY_b.DMATIMELIMIT0 = 0u;

    MSPI1->DEV0CFG1_b.DQSTURN0    = octal ? 2u : 0u;
    MSPI1->DEV0CFG1_b.RXSMP0      = 1u;
    MSPI1->DEV0CFG1_b.TAFOURTH0   = octal ? 1u : 0u;
    MSPI1->DEV0CFG1_b.SFTURN0     = 10u;
    MSPI1->THRESHOLD_b.RXTHRESH   = 30u;
    MSPI1->DMABCOUNT              = 32u;
    MSPI1->MSPICFG_b.IOMSEL       = MSPI0_MSPICFG_IOMSEL_DISABLED;
    MSPI1->MSPICFG_b.APBCLK       = MSPI0_MSPICFG_APBCLK_DIS;
    __DSB();
}

static void nor_pads_config(void)
{
    uint32_t pad;
    for (pad = NOR_PAD_D0; pad <= NOR_PAD_DQS; pad++) {
        tiku_ambiq_gpio_pad_config(pad, PAD_CFG_MSPI_IO);
    }
    tiku_ambiq_gpio_pad_config(NOR_PAD_CE, PAD_CFG_MSPI_CE);
}

void tiku_nor_power(int on)
{
    /* The load switch (GP208).  This is the only external memory on the
     * board that TikuOS can take to true zero -- and after an off/on the
     * device is back in serial mode with default config, so callers must
     * re-run tiku_nor_init_serial().
     *
     * DANGER, LEARNED THE HARD WAY: GP208's polarity and its true load are
     * NOT established.  The schematic names the net MSPI1_LS_EN_GP208 into
     * a LOADSW input but does not tell us the sense, and the first session
     * that drove this pad ended with the board unreachable over SWD at
     * every speed and reset type -- a physical power cycle was required.
     * Nothing in the bring-up path touches it any more; it is reachable
     * only from an explicit operator verb, and only with the meter watching.
     * Until the polarity is confirmed on a scope or by the schematic's
     * switch part number, treat driving this pad as a hardware experiment,
     * not a driver action. */
    tiku_ambiq_gpio_pad_config(NOR_PAD_LSEN, PAD_CFG_GPIO_OUT);
    tiku_ambiq_gpio_set(NOR_PAD_LSEN, on ? 1u : 0u);
    __DSB();
    if (on) {
        tiku_cpu_ambiq_delay_us(2000u);   /* rail rise + device power-on    */
    } else {
        s_up = 0u; s_octal = 0u;
    }
}

int tiku_nor_powered(void)
{
    return ((PWRCTRL->DEVPWRSTATUS & PWRCTRL_DEVPWRSTATUS_PWRSTMSPI1_Msk) != 0u)
           ? 1 : 0;
}

void tiku_nor_deinit(void)
{
    CLKGEN->MSPIIOCLKCTRL &= ~(CLKGEN_MSPIIOCLKCTRL_MSPI0IOCLKEN_Msk
                               << CLKGEN_MSPIIOCLKCTRL_MSPI1IOCLKEN_Pos);
    PWRCTRL->DEVPWREN &= ~PWRCTRL_DEVPWREN_PWRENMSPI1_Msk;
    __DSB();
    s_up = 0u; s_octal = 0u;
}

/*---------------------------------------------------------------------------*/
/* PHASE A -- SERIAL BRING-UP AND IDENTITY                                   */
/*---------------------------------------------------------------------------*/

/** @brief Hardware reset pulse on GP54 (schematic-confirmed pin). */
static void nor_hw_reset(void)
{
    tiku_ambiq_gpio_pad_config(NOR_PAD_RST, PAD_CFG_GPIO_OUT);
    tiku_ambiq_gpio_set(NOR_PAD_RST, 1u);
    tiku_cpu_ambiq_delay_us(10u);
    tiku_ambiq_gpio_set(NOR_PAD_RST, 0u);
    tiku_cpu_ambiq_delay_us(50u);          /* well past the part's tRSTP    */
    tiku_ambiq_gpio_set(NOR_PAD_RST, 1u);
    tiku_cpu_ambiq_delay_us(500u);         /* tRST recovery                 */
}

tiku_nor_err_t tiku_nor_init_serial(unsigned clk)
{
    tiku_nor_err_t rc;
    uint32_t dummy = 0u;

    if (clk >= NOR_CLK_COUNT) { return TIKU_NOR_ERR_ARG; }

    trace("power");
    rc = nor_power_on();
    if (rc != TIKU_NOR_OK) { return rc; }

    trace("ioclk");
    rc = nor_ioclk_on(s_clk[TIKU_NOR_CLK_24MHZ].ioclk_sel);
    if (rc != TIKU_NOR_OK) {
        PWRCTRL->DEVPWREN &= ~PWRCTRL_DEVPWREN_PWRENMSPI1_Msk;
        return rc;
    }

    /* Serial phase always runs at the slow row; @p clk names the octal
     * target and is remembered for phase B. */
    trace("controller");
    s_octal = 0u;
    nor_controller_config(&s_clk[TIKU_NOR_CLK_24MHZ], 0);
    trace("pads");
    nor_pads_config();
    tiku_cpu_ambiq_delay_us(150u);
    s_clk_idx = (uint8_t)TIKU_NOR_CLK_24MHZ;
    s_up = 1u;

    trace("hw-reset");
    nor_hw_reset();

    trace("sw-reset");
    (void)nor_pio(NOR_CMD_RESET_ENABLE, 0u, &dummy, 0u, 0, 0, 0);
    (void)nor_pio(NOR_CMD_RESET_MEMORY, 0u, &dummy, 0u, 0, 0, 0);
    tiku_cpu_ambiq_delay_us(1000u);
    return TIKU_NOR_OK;
}

tiku_nor_err_t tiku_nor_read_id(tiku_nor_id_t *out)
{
    tiku_nor_id_t id;
    uint32_t raw = 0u;
    tiku_nor_err_t rc;

    if (!s_up) { return TIKU_NOR_ERR_POWER; }

    id.mfr = 0u; id.type = 0u; id.capacity = 0u;
    id.ncr6 = 0u; id.status = 0u; id.octal = s_octal;

    /* READ_ID takes no address in serial mode; in octal the device expects
     * the standard address+dummy framing. */
    rc = nor_pio(nor_op(NOR_CMD_READ_ID, NOR_OCMD_READ_ID), 0u, &raw, 4u,
                 1, s_octal ? 1 : 0, s_octal ? 1 : 0);
    if (rc == TIKU_NOR_OK) {
        id.mfr      = (uint8_t)raw;
        id.type     = (uint8_t)(raw >> 8);
        id.capacity = (uint8_t)(raw >> 16);
    }

    {   /* status register: 1 byte, no address */
        uint32_t s = 0u;
        if (nor_pio(nor_op(NOR_CMD_READ_STATUS, NOR_OCMD_READ_STATUS), 0u,
                    &s, 1u, 1, s_octal ? 1 : 0, s_octal ? 1 : 0)
                == TIKU_NOR_OK) {
            id.status = (uint8_t)s;
        }
    }

    if (!s_octal) {
        /* Non-volatile CR[6] -- READ ONLY.  Octal entry needs it to be
         * 0xFF; this driver reports rather than writes (see the header). */
        uint32_t n = 0u;
        if (nor_pio(NOR_CMD_READ_NVCR, 6u, &n, 1u, 1, 1, 1) == TIKU_NOR_OK) {
            id.ncr6 = (uint8_t)n;
        }
    }

    if (out) { *out = id; }
    if (rc != TIKU_NOR_OK) { return rc; }
    return (id.mfr == TIKU_NOR_MFR_ISSI) ? TIKU_NOR_OK : TIKU_NOR_ERR_ID;
}

/*---------------------------------------------------------------------------*/
/* PHASE B -- OCTAL DDR                                                      */
/*---------------------------------------------------------------------------*/

tiku_nor_err_t tiku_nor_force_octal(unsigned clk)
{
    /* Configure the CONTROLLER for octal DDR without asking the device to
     * switch -- for the case where the device is ALREADY octal (a
     * non-volatile IO-mode default, or a previous session's state that a
     * board power cycle did not clear because the mode is not volatile).
     * A serial-mode command is meaningless to such a part, which looks
     * exactly like a dead device from the data lines' point of view. */
    if (!s_up)                { return TIKU_NOR_ERR_POWER; }
    if (clk >= NOR_CLK_COUNT) { return TIKU_NOR_ERR_ARG; }
    s_octal = 1u;
    nor_controller_config(&s_clk[clk], 1);
    if (nor_ioclk_on(s_clk[clk].ioclk_sel) != TIKU_NOR_OK) {
        return TIKU_NOR_ERR_CLOCK;
    }
    s_clk_idx = (uint8_t)clk;
    tiku_cpu_ambiq_delay_us(100u);
    return TIKU_NOR_OK;
}

tiku_nor_err_t tiku_nor_enter_octal(unsigned clk)
{
    tiku_nor_id_t id;
    tiku_nor_err_t rc;
    uint32_t v;

    if (!s_up)                 { return TIKU_NOR_ERR_POWER; }
    if (clk >= NOR_CLK_COUNT)  { return TIKU_NOR_ERR_ARG; }
    if (s_octal)               { return TIKU_NOR_OK; }

    trace("id-serial");
    rc = tiku_nor_read_id(&id);
    if (rc != TIKU_NOR_OK) { return rc; }

    /* THE REFUSAL.  Octal mode wants non-volatile CR[6] == 0xFF; the vendor
     * WRITES it when it disagrees.  Non-volatile config writes are the
     * permanent kind, so this driver stops and reports instead. */
    if (id.ncr6 != NOR_NVCR6_XIP_DISABLE) {
        return TIKU_NOR_ERR_STATE;
    }

    trace("wren+4b");
    {
        uint32_t dummy = 0u;
        (void)nor_pio(NOR_CMD_WREN, 0u, &dummy, 0u, 0, 0, 0);
        (void)nor_pio(NOR_CMD_ENTER_4B, 0u, &dummy, 0u, 0, 0, 0);
        tiku_cpu_ambiq_delay_us(100u);
    }

    trace("vcr-octal");
    v = NOR_VCR_IO_OCTAL_DDR;
    rc = nor_pio(NOR_CMD_WRITE_VCR, 0u, &v, 1u, 0, 1, 0);
    if (rc != TIKU_NOR_OK) { return rc; }
    tiku_cpu_ambiq_delay_us(100u);

    /* The device is now octal; the controller must follow before any
     * further command is legible to it. */
    trace("controller-octal");
    s_octal = 1u;
    nor_controller_config(&s_clk[clk], 1);
    rc = nor_ioclk_on(s_clk[clk].ioclk_sel);
    if (rc != TIKU_NOR_OK) { return rc; }
    s_clk_idx = (uint8_t)clk;
    tiku_cpu_ambiq_delay_us(100u);

    {   /* leave the write-enable latch clear in the new mode */
        uint32_t dummy = 0u;
        (void)nor_pio(NOR_OCMD_WRDI, 0u, &dummy, 0u, 0, 0, 0);
    }

    /* PROVE THE SWITCH: identity again, now in octal.  Two identities in two
     * modes is this part's equivalent of the PSRAM's bit-bang arbiter. */
    trace("id-octal");
    rc = tiku_nor_read_id(&id);
    if (rc != TIKU_NOR_OK) {
        s_octal = 0u;
        nor_controller_config(&s_clk[TIKU_NOR_CLK_24MHZ], 0);
        s_clk_idx = (uint8_t)TIKU_NOR_CLK_24MHZ;
        return rc;
    }
    return TIKU_NOR_OK;
}

/*---------------------------------------------------------------------------*/
/* READ / PROGRAM / ERASE                                                    */
/*---------------------------------------------------------------------------*/

#define NOR_CHUNK 256u

tiku_nor_err_t tiku_nor_read(uint32_t addr, void *buf, uint32_t n)
{
    uint8_t *dst = (uint8_t *)buf;

    if (!s_up) { return TIKU_NOR_ERR_POWER; }
    while (n != 0u) {
        uint32_t chunk = (n > NOR_CHUNK) ? NOR_CHUNK : n;
        uint32_t words[NOR_CHUNK / 4u];
        uint32_t b;
        tiku_nor_err_t rc =
            nor_pio(nor_op(NOR_CMD_FAST_READ_4B, NOR_OCMD_READ), addr,
                    words, chunk, 1, 1, 1);
        if (rc != TIKU_NOR_OK) { return rc; }
        for (b = 0u; b < chunk; b++) {
            dst[b] = (uint8_t)(words[b / 4u] >> (8u * (b & 3u)));
        }
        dst += chunk; addr += chunk; n -= chunk;
    }
    return TIKU_NOR_OK;
}

/**
 * @brief Poll WIP with the backoff cadence.
 *
 * @param step_us  gap between glances -- ~100 us for programs, ~10 ms for
 *                 erases.  A tight spin here would be APB traffic into the
 *                 controller that is servicing the device; the PSRAM work
 *                 measured that cost at 20 % of throughput.
 * @param max_us   bound; returns TIMEOUT rather than hanging
 */
static tiku_nor_err_t nor_wait_wip(uint32_t step_us, uint32_t max_us)
{
    uint32_t waited = 0u;
    while (waited < max_us) {
        uint32_t st = 0u;
        tiku_nor_err_t rc =
            nor_pio(nor_op(NOR_CMD_READ_STATUS, NOR_OCMD_READ_STATUS), 0u,
                    &st, 1u, 1, s_octal ? 1 : 0, s_octal ? 1 : 0);
        if (rc != TIKU_NOR_OK) { return rc; }
        if ((st & NOR_STATUS_WIP) == 0u) { return TIKU_NOR_OK; }
        tiku_cpu_ambiq_delay_us(step_us);
        waited += step_us;
        tiku_hang_checkin();
    }
    return TIKU_NOR_ERR_TIMEOUT;
}

/** @brief Read the flag-status register and translate program/erase errors. */
static tiku_nor_err_t nor_check_flags(void)
{
    uint32_t f = 0u;
    if (s_octal) { return TIKU_NOR_OK; }   /* serial-only opcode here */
    if (nor_pio(NOR_CMD_READ_FLAGSTAT, 0u, &f, 1u, 1, 0, 0) != TIKU_NOR_OK) {
        return TIKU_NOR_OK;                /* absence of evidence, not error */
    }
    if ((f & (NOR_FLAG_PROG_ERR | NOR_FLAG_ERASE_ERR)) != 0u) {
        return TIKU_NOR_ERR_PROGRAM;
    }
    return TIKU_NOR_OK;
}

tiku_nor_err_t tiku_nor_program(uint32_t addr, const void *buf, uint32_t n)
{
    const uint8_t *src = (const uint8_t *)buf;

    if (!s_up) { return TIKU_NOR_ERR_POWER; }
    if (addr + n > TIKU_NOR_SIZE_BYTES) { return TIKU_NOR_ERR_ARG; }

    while (n != 0u) {
        /* Never cross a page boundary: the device wraps within the page
         * instead of advancing, which would scramble the tail silently. */
        uint32_t page_left = TIKU_NOR_PAGE_SIZE - (addr % TIKU_NOR_PAGE_SIZE);
        uint32_t chunk = (n < page_left) ? n : page_left;
        uint32_t words[TIKU_NOR_PAGE_SIZE / 4u];
        uint32_t b, dummy = 0u;
        tiku_nor_err_t rc;

        for (b = 0u; b < ((chunk + 3u) / 4u); b++) { words[b] = 0u; }
        for (b = 0u; b < chunk; b++) {
            words[b / 4u] |= ((uint32_t)src[b]) << (8u * (b & 3u));
        }

        rc = nor_pio(nor_op(NOR_CMD_WREN, NOR_OCMD_WREN), 0u, &dummy, 0u,
                     0, 0, 0);
        if (rc != TIKU_NOR_OK) { return rc; }
        rc = nor_pio(nor_op(NOR_CMD_PAGE_PROG_4B, NOR_OCMD_PAGE_PROG), addr,
                     words, chunk, 0, 1, 0);
        if (rc != TIKU_NOR_OK) { return rc; }
        rc = nor_wait_wip(100u, 5000u);        /* page program ~ 0.2-1 ms   */
        if (rc != TIKU_NOR_OK) { return rc; }
        rc = nor_check_flags();
        if (rc != TIKU_NOR_OK) { return rc; }

        src += chunk; addr += chunk; n -= chunk;
        tiku_hang_checkin();
    }
    return TIKU_NOR_OK;
}

tiku_nor_err_t tiku_nor_erase(uint32_t addr, int small, int force)
{
    uint32_t dummy = 0u;
    tiku_nor_err_t rc;

    if (!s_up) { return TIKU_NOR_ERR_POWER; }
    if (addr >= TIKU_NOR_SIZE_BYTES) { return TIKU_NOR_ERR_ARG; }

    /* Default-deny outside the scratch sector.  Erase endurance is finite
     * and this driver runs unattended; protecting the rest of the die from
     * its own test machinery is the cheapest safety there is. */
    if (!force && (addr < TIKU_NOR_SCRATCH_ADDR)) {
        return TIKU_NOR_ERR_ARG;
    }

    rc = nor_pio(nor_op(NOR_CMD_WREN, NOR_OCMD_WREN), 0u, &dummy, 0u, 0, 0, 0);
    if (rc != TIKU_NOR_OK) { return rc; }
    rc = nor_pio(small ? nor_op(NOR_CMD_SUBSEC_ERASE, NOR_OCMD_SUBSEC_ERASE)
                       : nor_op(NOR_CMD_SECTOR_ERASE, NOR_OCMD_SECTOR_ERASE),
                 addr, &dummy, 0u, 0, 1, 0);
    if (rc != TIKU_NOR_OK) { return rc; }

    s_erases++;
    /* Subsector ~ 0.1-0.5 s, sector ~ 0.5-3 s: poll at 10 ms, bound at 10 s. */
    rc = nor_wait_wip(10000u, 10000000u);
    if (rc != TIKU_NOR_OK) { return rc; }
    return nor_check_flags();
}

/*---------------------------------------------------------------------------*/
/* XIP                                                                       */
/*---------------------------------------------------------------------------*/

tiku_nor_err_t tiku_nor_xip_enable(int enable)
{
    if (!s_up) { return TIKU_NOR_ERR_POWER; }
    if (enable) {
        /* 8 MB aperture (SIZE code 7 = 8M) at MSPI1's base. */
        MSPI1->DEV0AXI =
            ((7u << MSPI0_DEV0AXI_SIZE0_Pos) & MSPI0_DEV0AXI_SIZE0_Msk);
        __DSB();
        MSPI1->DEV0XIP_b.XIPEN0 = 1u;
    } else {
        MSPI1->DEV0XIP_b.XIPEN0 = 0u;
    }
    __DSB();
    return TIKU_NOR_OK;
}

int tiku_nor_xip_enabled(void)
{
    return (s_up && MSPI1->DEV0XIP_b.XIPEN0 != 0u) ? 1 : 0;
}

void tiku_nor_regs(uint32_t *out, unsigned n)
{
    unsigned i;

    /* POWER-SAFE, and it is not optional: reading an MSPI register while
     * its domain is unpowered stalls the APB and wedges the CPU with no
     * fault -- the same class of hang the PSRAM's first IOMSEL bug caused,
     * and this function caused it again by dumping registers before any
     * bring-up.  The two always-available registers are reported either
     * way; the rest read back as 0xDEADDEAD when the domain is down, which
     * is unmistakable in a dump. */
    for (i = 0u; i < n; i++) { out[i] = 0xDEADDEADu; }
    if (n > 0u) { out[0] = PWRCTRL->DEVPWRSTATUS; }
    if (n > 1u) { out[1] = CLKGEN->MSPIIOCLKCTRL; }
    if (!tiku_nor_powered()) {
        return;
    }
    {
        const volatile uint32_t *const regs[] = {
            &MSPI1->DEV0CFG, &MSPI1->DEV0CFG1, &MSPI1->DEV0DDR,
            &MSPI1->DEV0XIP, &MSPI1->DEV0INSTR, &MSPI1->PADOUTEN,
            &MSPI1->MSPICFG, &MSPI1->CTRL, &MSPI1->INTSTAT, &MSPI1->RXENTRIES,
        };
        for (i = 2u; i < n && (i - 2u) < (sizeof regs / sizeof regs[0]); i++) {
            out[i] = *regs[i - 2u];
        }
    }
}

void tiku_nor_ls_set(int level)
{
    /* level: 0 low, 1 high, -1 leave as high-Z input.  The schematic names
     * MSPI1_LS_EN_GP208 but not its polarity, and a load switch can be
     * either sense -- so this exists to settle it by experiment rather than
     * by assumption. */
    if (level < 0) {
        tiku_ambiq_gpio_pad_config(NOR_PAD_LSEN, PAD_FNCSEL_GPIO | PAD_INPEN);
    } else {
        tiku_ambiq_gpio_pad_config(NOR_PAD_LSEN, PAD_CFG_GPIO_OUT);
        tiku_ambiq_gpio_set(NOR_PAD_LSEN, level ? 1u : 0u);
    }
    __DSB();
    tiku_cpu_ambiq_delay_us(2000u);
}

/*---------------------------------------------------------------------------*/
/* BIT-BANG ARBITER -- controller-free ground truth                          */
/*---------------------------------------------------------------------------*/

/*
 * The instrument that cracked the PSRAM, ported to MSPI1's pads and to
 * single-lane SPI.  Drives CE/CLK/D0 by hand and samples D1, so it answers
 * the only question that matters at first contact: IS THE DEVICE ALIVE AND
 * DOES IT SPEAK SERIAL SPI?  Everything about the controller's framing,
 * latency and lane assignment is out of the picture.
 *
 * Serial SPI here is mode 0: data launched on the falling edge, sampled by
 * the device on the rising edge; the device returns data on D1, which we
 * sample after each rising edge.  Microsecond edges -- far slower than any
 * timing requirement.
 */

#define BB_OUT  (PAD_FNCSEL_GPIO | PAD_OUTCFG_PUSHPULL | PAD_INPEN | PAD_DS_0P5X)
#define BB_IN   (PAD_FNCSEL_GPIO | PAD_INPEN)
#define NOR_PAD_D1  96u

static void bb_dwell(void)
{
    uint32_t n = 60u;
    while (n--) { __asm__ volatile ("nop"); }
}

static uint32_t bb_read_d1(void)
{
    /* D1 = GP96: RD2 covers pads 64..95, RD3 covers 96..127 -> bit 0. */
    return ((&GPIO->RD0)[3] >> 0) & 1u;
}

void tiku_nor_bitbang_id(uint8_t *out, uint32_t n_bytes)
{
    uint32_t i, b;

    /* DEASSERT RESET FIRST.  GP54 is hi-Z out of SoC reset, and if the board
     * has no pull-up on RSTn the device sits held in reset -- answering
     * nothing, on every lane, forever.  The controller path pulses reset in
     * nor_hw_reset(); the bit-bang path never did, which made this the one
     * stone left unturned when the arbiter reported all-ones. */
    tiku_ambiq_gpio_pad_config(NOR_PAD_RST, BB_OUT);
    tiku_ambiq_gpio_set(NOR_PAD_RST, 1u);
    tiku_cpu_ambiq_delay_us(500u);

    /* Claim the four pins we need; leave the rest of the bus alone. */
    tiku_ambiq_gpio_pad_config(NOR_PAD_CE,  BB_OUT);
    tiku_ambiq_gpio_pad_config(NOR_PAD_CLK, BB_OUT);
    tiku_ambiq_gpio_pad_config(NOR_PAD_D0,  BB_OUT);
    tiku_ambiq_gpio_pad_config(NOR_PAD_D1,  BB_IN);
    tiku_ambiq_gpio_set(NOR_PAD_CE, 1u);
    tiku_ambiq_gpio_set(NOR_PAD_CLK, 0u);
    bb_dwell();

    tiku_ambiq_gpio_set(NOR_PAD_CE, 0u);        /* select                   */
    bb_dwell();

    /* Opcode 0x9F, MSB first: set D0 while clock low, pulse clock high. */
    for (i = 0u; i < 8u; i++) {
        tiku_ambiq_gpio_set(NOR_PAD_D0, (0x9Fu >> (7u - i)) & 1u);
        bb_dwell();
        tiku_ambiq_gpio_set(NOR_PAD_CLK, 1u);
        bb_dwell();
        tiku_ambiq_gpio_set(NOR_PAD_CLK, 0u);
    }

    /* Read n_bytes from D1, MSB first. */
    for (b = 0u; b < n_bytes; b++) {
        uint8_t v = 0u;
        for (i = 0u; i < 8u; i++) {
            bb_dwell();
            tiku_ambiq_gpio_set(NOR_PAD_CLK, 1u);
            bb_dwell();
            v = (uint8_t)((v << 1) | (uint8_t)bb_read_d1());
            tiku_ambiq_gpio_set(NOR_PAD_CLK, 0u);
        }
        out[b] = v;
    }

    tiku_ambiq_gpio_set(NOR_PAD_CE, 1u);
    /* Leave the pads as inputs; the next init reclaims them. */
    tiku_ambiq_gpio_pad_config(NOR_PAD_D0, BB_IN);
    tiku_ambiq_gpio_pad_config(NOR_PAD_CLK, BB_IN);
    tiku_ambiq_gpio_pad_config(NOR_PAD_CE, BB_IN);
}

uint32_t tiku_nor_bitbang_selftest(void)
{
    /* PROVE THE INSTRUMENT BEFORE BELIEVING ITS VERDICT.
     *
     * The arbiter reads D1 (GP96).  If that read path is wrong it reports
     * all-ones forever and looks exactly like a dead device -- so drive D1
     * as an output, low then high, and read it back each time.  Result bits:
     *   b0 = value read while driving LOW  (want 0)
     *   b1 = value read while driving HIGH (want 1)
     *   b2 = value read with D1 released to input (the device's own level)
     *   b3 = same for D0 (GP95), the line we transmit on
     * So 0x02 or 0x06 means the instrument works.  0x03 or 0x07 means the
     * read is stuck high and every all-ff verdict is meaningless.
     */
    uint32_t r = 0u;

    tiku_ambiq_gpio_pad_config(NOR_PAD_D1, BB_OUT);
    tiku_ambiq_gpio_set(NOR_PAD_D1, 0u);
    bb_dwell();
    r |= (bb_read_d1() & 1u) << 0;
    tiku_ambiq_gpio_set(NOR_PAD_D1, 1u);
    bb_dwell();
    r |= (bb_read_d1() & 1u) << 1;

    tiku_ambiq_gpio_pad_config(NOR_PAD_D1, BB_IN);
    bb_dwell();
    r |= (bb_read_d1() & 1u) << 2;

    tiku_ambiq_gpio_pad_config(NOR_PAD_D0, BB_IN);
    bb_dwell();
    r |= ((((&GPIO->RD0)[2] >> 31) & 1u) << 3);   /* GP95 = RD2 bit 31 */

    /* b4/b5: does CE (GP53) actually drive?  A chip select that never
     * asserts is indistinguishable from a dead device from the data lines'
     * point of view -- everything reads as idle-high forever. */
    tiku_ambiq_gpio_pad_config(NOR_PAD_CE, BB_OUT);
    tiku_ambiq_gpio_set(NOR_PAD_CE, 0u);
    bb_dwell();
    r |= ((((&GPIO->RD0)[1] >> 21) & 1u) << 4);   /* GP53 = RD1 bit 21 */
    tiku_ambiq_gpio_set(NOR_PAD_CE, 1u);
    bb_dwell();
    r |= ((((&GPIO->RD0)[1] >> 21) & 1u) << 5);

    /* b6/b7: same for CLK (GP103 = RD3 bit 7). */
    tiku_ambiq_gpio_pad_config(NOR_PAD_CLK, BB_OUT);
    tiku_ambiq_gpio_set(NOR_PAD_CLK, 0u);
    bb_dwell();
    r |= ((((&GPIO->RD0)[3] >> 7) & 1u) << 6);
    tiku_ambiq_gpio_set(NOR_PAD_CLK, 1u);
    bb_dwell();
    r |= ((((&GPIO->RD0)[3] >> 7) & 1u) << 7);
    tiku_ambiq_gpio_set(NOR_PAD_CLK, 0u);

    /* b8: RSTn (GP54 = RD1 bit 22) driven high, read back. */
    tiku_ambiq_gpio_pad_config(NOR_PAD_RST, BB_OUT);
    tiku_ambiq_gpio_set(NOR_PAD_RST, 1u);
    bb_dwell();
    r |= ((((&GPIO->RD0)[1] >> 22) & 1u) << 8);

    /* b9: the load-switch pad (GP208 = RD6 bit 16) driven high, read back. */
    tiku_ambiq_gpio_pad_config(NOR_PAD_LSEN, BB_OUT);
    tiku_ambiq_gpio_set(NOR_PAD_LSEN, 1u);
    bb_dwell();
    r |= ((((&GPIO->RD0)[6] >> 16) & 1u) << 9);
    return r;
}

void tiku_nor_fault_inject(int enable)
{
    if (enable) {
        tiku_ambiq_gpio_pad_config(NOR_PAD_D0, PAD_FNCSEL_GPIO);
    } else {
        tiku_ambiq_gpio_pad_config(NOR_PAD_D0, PAD_CFG_MSPI_IO);
    }
    __DSB();
}

#endif /* PLATFORM_AMBIQ && TIKU_DRV_NOR_ENABLE */
