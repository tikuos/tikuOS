/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nor_arch.c - Apollo510 MSPI1 and IS25WX064 octal NOR bring-up.
 *
 * The NOR wakes in 1-line SPI and is switched to octal DDR by command, sets
 * bits only through a slow erase, and keeps its contents through a power
 * cycle.  PIO and DMA are refused while the XIP aperture is open.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#if defined(PLATFORM_AMBIQ) && (TIKU_DRV_NOR_ENABLE + 0)

#include "tiku_nor_arch.h"
#include "tiku_gpio_arch.h"      /* tiku_ambiq_gpio_pad_config(), gpio_set  */
#include "tiku_cpu_common.h"     /* tiku_cpu_ambiq_delay_us()               */
#include "apollo510.h"           /* CMSIS register map -- defs only         */
#include <kernel/cpu/tiku_hang.h>   /* check-ins during long waits          */
#include <kernel/shell/tiku_shell_io.h>  /* norbench reports via SHELL_PRINTF */
#include "hal/tiku_cpu.h"        /* dcache clean/invalidate around DMA      */

/*---------------------------------------------------------------------------*/
/* COMMANDS (table 2)                                                        */
/*---------------------------------------------------------------------------*/

/* serial (1-byte opcodes) */
#define NOR_CMD_READ_ID        0x9Fu
#define NOR_CMD_RESET_ENABLE   0x66u
#define NOR_CMD_RESET_MEMORY   0x99u
#define NOR_OCMD_RESET_ENABLE  0x6666u
#define NOR_OCMD_RESET_MEMORY  0x9999u
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
/* 4-byte-address opcodes, because this driver always sends a 4-byte
 * address.  0x20/0xD8 are the 3-byte forms; pairing them with a 4-byte
 * address makes the device erase somewhere else, or nowhere. */
#define NOR_CMD_SUBSEC_ERASE   0x21u
#define NOR_CMD_SECTOR_ERASE   0xDCu
/* 0xB1 (write non-volatile CR, permanent) and 0xC7 (chip erase, the whole
 * die) are not defined, so this unattended driver cannot issue them. */

/* octal DDR (2-byte duplicated opcodes) */
#define NOR_OCMD_READ          0xFDFDu
#define NOR_OCMD_PAGE_PROG     0x1212u
#define NOR_OCMD_SUBSEC_ERASE  0x2121u
#define NOR_OCMD_SECTOR_ERASE  0xDCDCu
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
 * RSTn and the load switch also come from the board header.  A board without
 * a NOR declares no NOR capability, so TIKU_DRV_NOR_ENABLE=1 is refused at
 * make time and this file is not compiled for it; a board that fits a NOR
 * declares the capability and its own pads.
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

/* As for the PSRAM: out = source / (2 * CLKDIV), and TXNEG is chosen by
 * frequency (0 at <= 62.5 MHz, 1 above). */
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

/* Read latency.  Serial fast read needs 10 dummy cycles on this part; at the
 * generic SPI-NOR value of 8, erased bytes (0xFF) still read correctly and
 * every other byte reads wrong.  Octal DDR array reads use 31, the default
 * in VCR 0x01.  Programs have no turnaround in either mode. */
#define NOR_TA_SERIAL  10u
#define NOR_TA_OCTAL  31u   /* array reads: matches VCR 0x01 default (0x1F) */
#define NOR_TA_OCTAL_ID     15u   /* octal READ_ID below 96 MHz  */
/* The vendor's value.  Octal identity reads are unreliable on this part --
 * they return `17 01 ...`, the tail of `9d 5b 17`, at both 15 and 16 -- so
 * octal entry does not gate on them.  Octal array reads are unaffected. */
#define NOR_TA_OCTAL_ID_96  16u

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

static uint8_t  s_up;          /**< controller configured                   */
static uint8_t  s_octal;       /**< device+controller in octal DDR          */
static uint8_t  s_clk_idx;
static uint32_t s_erases;      /**< erases issued since boot                */
static void   (*s_trace)(const char *step);

/** @brief Pass @p step to the installed tracer, if any. */
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
 * Follows psram_pio2 in the PSRAM driver; the two devices differ in
 * instruction width, latency and phases.  TIKU_NOR_ERR_ARG while XIP is on.
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

    /* A PIO command while the aperture is enabled deadlocks the APB until a
     * power cycle. */
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
        /* TXRX = 0 receives; 1 transmits. */
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

/** @brief Power the MSPI1 domain and wait (bounded) for its status. */
static tiku_nor_err_t nor_power_on(void)
{
    uint32_t spins = 100000u;
    PWRCTRL->DEVPWREN |= PWRCTRL_DEVPWREN_PWRENMSPI1_Msk;
    __DSB();
    while (((PWRCTRL->DEVPWRSTATUS & PWRCTRL_DEVPWRSTATUS_PWRSTMSPI1_Msk) == 0u)
           && --spins != 0u) { }
    return (spins != 0u) ? TIKU_NOR_OK : TIKU_NOR_ERR_POWER;
}

/** @brief Select and enable MSPI1's IO clock source @p sel; check it stuck. */
static tiku_nor_err_t nor_ioclk_on(uint8_t sel)
{
    uint32_t v;

    /* Set the CLKGEN.MISC clock-gate and power-on-clock bits and force the
     * HFRC oscillator, as the vendor code does for MSPI0. */
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

/** @brief Program the controller for serial or (@p octal) octal DDR. */
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
    /* SEPIO marks a device with separate MOSI and MISO, which is what a
     * 1-line SPI NOR is: the controller drives D0 and samples D1. Clear, the
     * controller samples D0 and every serial read returns zero. Octal shares
     * its eight lines in both directions and must leave it clear. */
    if (!octal) { cfg |= MSPI0_DEV0CFG_SEPIO0_Msk; }
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

    /* No DMA boundary: flash has no rows to break bursts at (the vendor
     * uses BOUNDARY_NONE for this part). */
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

/** @brief Give D0..DQS and CE to MSPI1. */
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
    /* The load switch (GP208) can take the NOR, alone among the board's
     * external memories, to zero power.  After an off/on the device is back
     * in serial mode with default config, so callers must re-run
     * tiku_nor_init_serial().
     *
     * The polarity and load of GP208 are not established: the schematic
     * names the net MSPI1_LS_EN_GP208 into a LOADSW input without its
     * sense, and driving this pad can leave the board unreachable over SWD
     * at every speed and reset type until a physical power cycle.  The
     * bring-up path does not touch it; only an explicit operator command
     * does. */
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
/* SERIAL BRING-UP AND IDENTITY                                              */
/*---------------------------------------------------------------------------*/

/** @brief Pulse RSTn low for 50 us, then wait 500 us for recovery. */
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

    /* The serial phase always runs at the 24 MHz row; @p clk is only
     * range-checked. */
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

/*---------------------------------------------------------------------------*/
/* DMA                                                                       */
/*---------------------------------------------------------------------------*/

tiku_nor_err_t tiku_nor_dma_read(uint32_t addr, void *sram, uint32_t n)
{
    uint32_t spins = 500000u;          /* x20 us = 10 s ceiling */

    if (!s_up) { return TIKU_NOR_ERR_POWER; }
    /* As on the PIO path: a DMA started while the aperture is live wedges
     * the APB. */
    if (MSPI1->DEV0XIP_b.XIPEN0 != 0u) { return TIKU_NOR_ERR_ARG; }
    if (n == 0u || (n & 3u) != 0u ||
        ((uint32_t)(uintptr_t)sram & 3u) != 0u) {
        return TIKU_NOR_ERR_ARG;
    }
    if (addr + n > TIKU_NOR_SIZE_BYTES) { return TIKU_NOR_ERR_ARG; }

    MSPI1->DMATARGADDR = (uint32_t)(uintptr_t)sram;
    MSPI1->DMADEVADDR  = addr;
    MSPI1->DMATOTCOUNT = n;
    MSPI1->INTCLR      = 0xFFFFFFFFu;
    MSPI1->DMACFG =
        ((uint32_t)MSPI0_DMACFG_DMAEN_EN << MSPI0_DMACFG_DMAEN_Pos);
    __DSB();

    while (((MSPI1->DMASTAT &
             (MSPI0_DMASTAT_DMACPL_Msk | MSPI0_DMASTAT_DMAERR_Msk)) == 0u)
           && --spins != 0u) {
        tiku_cpu_ambiq_delay_us(20u);
    }
    {
        uint32_t st = MSPI1->DMASTAT;
        MSPI1->DMACFG  = 0u;
        MSPI1->DMASTAT = 0u;
        if (spins == 0u)                     { return TIKU_NOR_ERR_TIMEOUT; }
        if ((st & MSPI0_DMASTAT_DMAERR_Msk)) { return TIKU_NOR_ERR_TIMEOUT; }
    }
    return TIKU_NOR_OK;
}

/*---------------------------------------------------------------------------*/
/* NORBENCH                                                                  */
/*---------------------------------------------------------------------------*/
/*
 * DWT-timed like psrambench.  Every leg reports the bytes it moved and whether
 * they matched the pattern: bit-exact or FAIL.  Everything runs inside the
 * scratch sector, and a run spends the two erases it announces.
 */

extern unsigned long tiku_cpu_ambiq_clock_get_hz(void);

/* A 32 KB span keeps fixed setup cost a small share of each timed leg.  The
 * prepare step erases the whole 128 KB scratch sector and times it. */
#define NORB_SPAN   32768u
#define NORB_BUF    32768u
static uint8_t s_norb_buf[NORB_BUF] __attribute__((aligned(32)));

/** @brief Save DEMCR and DWT_CTRL, start CYCCNT and return its value. */
static uint32_t norb_cyc_begin(uint32_t *demcr0, uint32_t *ctl0)
{
    volatile uint32_t *demcr  = (volatile uint32_t *)0xE000EDFCUL;
    volatile uint32_t *dwtctl = (volatile uint32_t *)0xE0001000UL;
    volatile uint32_t *cyccnt = (volatile uint32_t *)0xE0001004UL;
    *demcr0 = *demcr; *ctl0 = *dwtctl;
    *demcr |= (1u << 24);
    *dwtctl |= 1u;
    return *cyccnt;
}

/** @brief Current DWT cycle count. */
static uint32_t norb_cyc_now(void)
{
    return *(volatile uint32_t *)0xE0001004UL;
}

/** @brief Print a bandwidth leg: KB, microseconds, MB/s and the verdict. */
static void norb_report(const char *leg, uint32_t bytes, uint32_t cyc,
                        int exact)
{
    unsigned long hz = tiku_cpu_ambiq_clock_get_hz();
    unsigned long kbps;

    if (cyc == 0u) { cyc = 1u; }
    kbps = (unsigned long)(((uint64_t)bytes * hz) / ((uint64_t)cyc * 1000u));
    SHELL_PRINTF("  %-11s %6lu KB  %8lu us  %5lu.%03lu MB/s  %s\n", leg,
                 (unsigned long)(bytes / 1024u),
                 (unsigned long)(((uint64_t)cyc * 1000000u) / hz),
                 kbps / 1000u, kbps % 1000u,
                 exact ? "bit-exact" : "FAIL");
}

/** @brief Print a per-operation leg: ops, microseconds, us/op, verdict. */
static void norb_report_op(const char *leg, uint32_t ops, uint32_t cyc,
                           int exact)
{
    unsigned long hz = tiku_cpu_ambiq_clock_get_hz();
    unsigned long us = (unsigned long)(((uint64_t)cyc * 1000000u) / hz);

    if (ops == 0u) { ops = 1u; }
    SHELL_PRINTF("  %-11s %6lu ops %8lu us  %5lu us/op    %s\n", leg,
                 (unsigned long)ops, us, us / ops,
                 exact ? "bit-exact" : "FAIL");
}

/** @brief Pattern byte for device address @p a. */
static inline uint8_t norb_pat(uint32_t a)
{
    return (uint8_t)(a ^ (a >> 8) ^ (a >> 16) ^ 0x5Au);
}

/* The XIP leg is off by default: a read of a mis-decoding aperture stalls
 * the bus with no software recovery, and the board then needs a reflash.
 * `power nor xip` probes the aperture with a single word. */
static uint8_t s_xip_leg;
static uint8_t s_sector_leg;

void tiku_nor_bench_set_xip(int on) { s_xip_leg = on ? 1u : 0u; }
void tiku_nor_bench_set_sector(int on) { s_sector_leg = on ? 1u : 0u; }

int tiku_nor_xip_probe(uint32_t *out)
{
    volatile const uint32_t *ap;
    uint32_t v;

    if (!s_up) { return -1; }
    if (tiku_nor_xip_enable(1) != TIKU_NOR_OK) { return -1; }
    ap = (volatile const uint32_t *)(TIKU_NOR_XIP_BASE + TIKU_NOR_SCRATCH_ADDR);
    v = *ap;                       /* works, or hangs the bus */
    (void)tiku_nor_xip_enable(0);
    if (out != (uint32_t *)0) { *out = v; }
    return 0;
}

void tiku_nor_bench_run(void)
{
    uint32_t demcr0, ctl0, t0, cyc;
    uint32_t base = TIKU_NOR_SCRATCH_ADDR;
    uint32_t i, off;
    int exact;

    if (!s_up) {
        SHELL_PRINTF("norbench: NOR is down -- run `power nor` first\n");
        return;
    }

    SHELL_PRINTF("norbench @%lu Hz, %s, scratch %08lx, span %u KB\n",
                 tiku_nor_clock_hz(), s_octal ? "octal DDR" : "serial",
                 (unsigned long)base, NORB_SPAN / 1024u);

    (void)norb_cyc_begin(&demcr0, &ctl0);

    /* Prepare: erase the scratch sector and program the pattern, timing
     * both as the erase and program legs. */
    t0 = norb_cyc_now();
    if (tiku_nor_erase(base, 0, 0) != TIKU_NOR_OK) {
        SHELL_PRINTF("  sector-erase  FAILED\n");
        return;
    }
    cyc = norb_cyc_now() - t0;
    exact = 1;
    if (tiku_nor_read(base, s_norb_buf, NORB_SPAN) != TIKU_NOR_OK) {
        exact = 0;
    }
    for (i = 0u; i < NORB_SPAN; i++) {
        if (s_norb_buf[i] != 0xFFu) { exact = 0; }
    }
    norb_report_op("sector-eras", 1u, cyc, exact);

    for (i = 0u; i < NORB_SPAN; i++) {
        s_norb_buf[i] = norb_pat(base + i);
    }
    t0 = norb_cyc_now();
    if (tiku_nor_program(base, s_norb_buf, NORB_SPAN) != TIKU_NOR_OK) {
        SHELL_PRINTF("  page-program  FAILED\n");
        return;
    }
    cyc = norb_cyc_now() - t0;
    for (i = 0u; i < NORB_SPAN; i++) { s_norb_buf[i] = 0u; }
    exact = (tiku_nor_read(base, s_norb_buf, NORB_SPAN) == TIKU_NOR_OK);
    for (i = 0u; i < NORB_SPAN; i++) {
        if (s_norb_buf[i] != norb_pat(base + i)) { exact = 0; }
    }
    norb_report_op("page-prog", NORB_SPAN / TIKU_NOR_PAGE_SIZE, cyc, exact);

    /* Sequential PIO read of the whole span. */
    for (i = 0u; i < NORB_SPAN; i++) { s_norb_buf[i] = 0u; }
    t0 = norb_cyc_now();
    exact = (tiku_nor_read(base, s_norb_buf, NORB_SPAN) == TIKU_NOR_OK);
    cyc = norb_cyc_now() - t0;
    for (i = 0u; i < NORB_SPAN; i++) {
        if (s_norb_buf[i] != norb_pat(base + i)) { exact = 0; }
    }
    norb_report("seq-read", NORB_SPAN, cyc, exact);

    /* 512 B reads at pseudo-random offsets inside the span. */
    exact = 1;
    off = 0u;
    t0 = norb_cyc_now();
    for (i = 0u; i < 8u; i++) {
        off = (off + 1741u) % (NORB_SPAN - 512u);
        off &= ~3u;
        if (tiku_nor_read(base + off, s_norb_buf, 512u) != TIKU_NOR_OK) {
            exact = 0;
            break;
        }
        {
            uint32_t b;
            for (b = 0u; b < 512u; b++) {
                if (s_norb_buf[b] != norb_pat(base + off + b)) { exact = 0; }
            }
        }
    }
    cyc = norb_cyc_now() - t0;
    norb_report("random512", 8u * 512u, cyc, exact);

    /* DMA read: the engine moves device -> SRAM with no per-chunk command
     * from the CPU.  The engine writes physical SRAM behind the D-cache, so
     * the buffer is cleaned before and invalidated after. */
    for (i = 0u; i < NORB_SPAN; i++) { s_norb_buf[i] = 0u; }
    tiku_cpu_dcache_clean(s_norb_buf, NORB_SPAN);
    t0 = norb_cyc_now();
    exact = (tiku_nor_dma_read(base, s_norb_buf, NORB_SPAN) == TIKU_NOR_OK);
    cyc = norb_cyc_now() - t0;
    tiku_cpu_dcache_invalidate(s_norb_buf, NORB_SPAN);
    for (i = 0u; i < NORB_SPAN; i++) {
        if (s_norb_buf[i] != norb_pat(base + i)) { exact = 0; }
    }
    norb_report("dma-read", NORB_SPAN, cyc, exact);

    /*
     * XIP: the CPU reads the aperture directly, with no per-chunk command.
     * PIO is refused while the aperture is live, so this leg follows the PIO
     * reads and closes the aperture before the erase leg.
     */
    if (s_xip_leg && tiku_nor_xip_enable(1) == TIKU_NOR_OK) {
        const volatile uint8_t *ap =
            (const volatile uint8_t *)(TIKU_NOR_XIP_BASE + base);
        exact = 1;
        t0 = norb_cyc_now();
        for (i = 0u; i < NORB_SPAN; i++) { s_norb_buf[i] = ap[i]; }
        cyc = norb_cyc_now() - t0;
        for (i = 0u; i < NORB_SPAN; i++) {
            if (s_norb_buf[i] != norb_pat(base + i)) { exact = 0; }
        }
        norb_report("xip-read", NORB_SPAN, cyc, exact);
        (void)tiku_nor_xip_enable(0);
    } else if (s_xip_leg) {
        SHELL_PRINTF("  xip-read     aperture would not open\n");
    }

    /* Subsector erase, inside the sector already spent above. */
    t0 = norb_cyc_now();
    exact = (tiku_nor_erase(base, 1, 0) == TIKU_NOR_OK);
    cyc = norb_cyc_now() - t0;
    if (exact && tiku_nor_read(base, s_norb_buf, 4096u) == TIKU_NOR_OK) {
        for (i = 0u; i < 4096u; i++) {
            if (s_norb_buf[i] != 0xFFu) { exact = 0; }
        }
    } else {
        exact = 0;
    }
    norb_report_op("subsec-eras", 1u, cyc, exact);

    SHELL_PRINTF("  erases spent this run: 2 (total %lu)\n",
                 (unsigned long)s_erases);

    {   /* restore the DWT state saved at the start */
        volatile uint32_t *demcr  = (volatile uint32_t *)0xE000EDFCUL;
        volatile uint32_t *dwtctl = (volatile uint32_t *)0xE0001000UL;
        *dwtctl = ctl0; *demcr = demcr0;
    }
}

uint32_t tiku_nor_scan_turnaround(uint32_t addr, const uint8_t *want,
                                  uint32_t n)
{
    uint32_t mask = 0u;
    uint32_t saved;
    unsigned t;

    if (!s_up || n > 32u) { return 0u; }   /* either mode */
    saved = MSPI1->DEV0CFG_b.TURNAROUND0;
    for (t = 0u; t < 32u; t++) {
        uint8_t got[32];
        uint32_t i;
        int same = 1;

        MSPI1->DEV0CFG_b.TURNAROUND0 = t;
        if (tiku_nor_read(addr, got, n) != TIKU_NOR_OK) { continue; }
        for (i = 0u; i < n; i++) {
            if (got[i] != want[i]) { same = 0; }
        }
        if (same) { mask |= (1u << t); }
    }
    MSPI1->DEV0CFG_b.TURNAROUND0 = saved;
    return mask;
}

int tiku_nor_octal_hears(void)
{
    uint32_t dummy = 0u;
    tiku_nor_id_t id;

    if (!s_up || !s_octal) { return -1; }

    /* Send the octal software reset.  A part listening in octal parses it
     * and returns to serial; one that is wedged, or in another mode, does
     * not.  It is a write, so it needs no read capture: it tests the
     * command path alone, without the data-return path. */
    (void)nor_pio(NOR_OCMD_RESET_ENABLE, 0u, &dummy, 0u, 0, 0, 0);
    (void)nor_pio(NOR_OCMD_RESET_MEMORY, 0u, &dummy, 0u, 0, 0, 0);
    tiku_cpu_ambiq_delay_us(1000u);

    /* Put the controller back to serial and ask.  An answer means the device
     * executed the octal reset, so it does parse octal commands. */
    s_octal = 0u;
    nor_controller_config(&s_clk[0], 0);
    if (nor_ioclk_on(s_clk[0].ioclk_sel) != TIKU_NOR_OK) { return -1; }
    return (tiku_nor_read_id(&id) == TIKU_NOR_OK) ? 1 : 0;
}

uint32_t tiku_nor_scan_rxdqs(int with_dqs)
{
    uint32_t mask = 0u;
    uint32_t saved_d, saved_en;
    unsigned d;

    if (!s_up || !s_octal) { return 0u; }
    saved_d  = MSPI1->DEV0DDR_b.RXDQSDELAY0;
    saved_en = MSPI1->DEV0DDR_b.ENABLEDQS0;
    MSPI1->DEV0DDR_b.ENABLEDQS0 = with_dqs ? 1u : 0u;
    for (d = 0u; d < 32u; d++) {
        tiku_nor_id_t id;
        MSPI1->DEV0DDR_b.RXDQSDELAY0 = d;
        if (tiku_nor_read_id(&id) == TIKU_NOR_OK) { mask |= (1u << d); }
    }
    MSPI1->DEV0DDR_b.RXDQSDELAY0 = saved_d;
    MSPI1->DEV0DDR_b.ENABLEDQS0  = saved_en;
    return mask;
}

/* Last identity that validated, so /sys/flash/id can be read without putting
 * a command on the bus.  Only a plausible answer is cached: a zero or shifted
 * read must not become the file's contents. */
static tiku_nor_id_t s_id_cache;
static uint8_t       s_id_valid;

int tiku_nor_id_cached(tiku_nor_id_t *out)
{
    if (!s_id_valid) { return -1; }
    if (out != (tiku_nor_id_t *)0) { *out = s_id_cache; }
    return 0;
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
     * the standard address+dummy framing.
     *
     * Octal READ_ID carries its own dummy count, fixed by the device and
     * unrelated to the array dummy cycles in VCR 0x01 (default 31, which is
     * what NOR_TA_OCTAL matches for reads). It is 15, or 16 at 96 MHz. Using
     * the array count here samples long past the identity window and returns
     * zeros -- indistinguishable from a part that is not answering. */
    if (s_octal) {
        uint32_t saved_ta = MSPI1->DEV0CFG_b.TURNAROUND0;
        MSPI1->DEV0CFG_b.TURNAROUND0 =
            (tiku_nor_clock_hz() >= 96000000u) ? NOR_TA_OCTAL_ID_96
                                               : NOR_TA_OCTAL_ID;
        rc = nor_pio(nor_op(NOR_CMD_READ_ID, NOR_OCMD_READ_ID), 0u, &raw, 4u,
                     1, 1, 1);
        MSPI1->DEV0CFG_b.TURNAROUND0 = saved_ta;
    } else {
        rc = nor_pio(nor_op(NOR_CMD_READ_ID, NOR_OCMD_READ_ID), 0u, &raw, 4u,
                     1, 0, 0);
    }
    if (rc == TIKU_NOR_OK) {
        id.mfr      = (uint8_t)raw;
        id.type     = (uint8_t)(raw >> 8);
        id.capacity = (uint8_t)(raw >> 16);
    }

    {   /* status register: 1 byte; address and turnaround in octal only */
        uint32_t s = 0u;
        if (nor_pio(nor_op(NOR_CMD_READ_STATUS, NOR_OCMD_READ_STATUS), 0u,
                    &s, 1u, 1, s_octal ? 1 : 0, s_octal ? 1 : 0)
                == TIKU_NOR_OK) {
            id.status = (uint8_t)s;
        }
    }

    if (!s_octal) {
        /* Non-volatile CR[6], read only.  Octal entry needs it to be 0xFF;
         * this driver reports it and never writes it (see the header). */
        uint32_t n = 0u;
        if (nor_pio(NOR_CMD_READ_NVCR, 6u, &n, 1u, 1, 1, 1) == TIKU_NOR_OK) {
            id.ncr6 = (uint8_t)n;
        }
    }

    if (out) { *out = id; }
    if (rc != TIKU_NOR_OK) { return rc; }
    if (id.mfr == TIKU_NOR_MFR_ISSI) {
        s_id_cache = id;
        s_id_valid = 1u;
        return TIKU_NOR_OK;
    }
    return TIKU_NOR_ERR_ID;
}

/*---------------------------------------------------------------------------*/
/* OCTAL DDR                                                                 */
/*---------------------------------------------------------------------------*/

tiku_nor_err_t tiku_nor_force_octal(unsigned clk)
{
    /* Configure the controller for octal DDR without asking the device to
     * switch -- for a device that is already octal (a non-volatile IO-mode
     * default, or a mode a board power cycle did not clear).  A serial-mode
     * command is meaningless to such a part, which then looks dead from the
     * data lines' point of view. */
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

    /* Octal mode needs non-volatile CR[6] == 0xFF.  Writing the
     * non-volatile register is permanent, so the driver returns
     * TIKU_NOR_ERR_STATE instead of changing it. */
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

    /*
     * Did the device leave serial?  Ask in serial, before the controller
     * changes: a part that switched cannot parse a 1-line command and must
     * go quiet.  One that still answers 0x9D never moved,
     * which separates "the VCR write did not take" from "the octal side is
     * misconfigured" -- two faults that otherwise present identically as an
     * all-zero identity.
     */
    {
        uint32_t probe = 0u;
        if (nor_pio(NOR_CMD_READ_ID, 0u, &probe, 4u, 1, 0, 0)
                == TIKU_NOR_OK &&
            (uint8_t)probe == TIKU_NOR_MFR_ISSI) {
            trace("STILL-SERIAL: VCR write did not take");
        } else {
            trace("left-serial (device stopped answering 1-line)");
        }
    }

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

    /*
     * Identity again, now in octal, for the trace only.  Octal READ_ID is not
     * a health check on this part: octal array reads are bit-exact while the
     * octal READ_ID does not return 0x9D (see NOR_TA_OCTAL_ID_96).  So rc is
     * set to OK.
     */
    trace("id-octal");
    rc = tiku_nor_read_id(&id);
    trace(rc == TIKU_NOR_OK ? "id-octal ok" : "id-octal empty (not fatal)");
    rc = TIKU_NOR_OK;
    if (rc != TIKU_NOR_OK) {
        /* Roll back to serial so a caller that cannot talk octal is left
         * where it can.  Not reached while rc is set to OK above.
         * Diagnostics that examine the octal state use
         * tiku_nor_enter_octal_raw(). */
        s_octal = 0u;
        nor_controller_config(&s_clk[TIKU_NOR_CLK_24MHZ], 0);
        s_clk_idx = (uint8_t)TIKU_NOR_CLK_24MHZ;
        return rc;
    }
    return TIKU_NOR_OK;
}

tiku_nor_err_t tiku_nor_enter_octal_raw(unsigned clk)
{
    tiku_nor_err_t rc = tiku_nor_enter_octal(clk);

    if (rc == TIKU_NOR_ERR_ID) {
        /* Meant for an octal entry whose closing octal identity read failed:
         * put the controller back into octal.  tiku_nor_enter_octal()
         * returns TIKU_NOR_ERR_ID only from its serial identity check, before
         * the device has switched, so this sets the controller to octal while
         * the device is still serial. */
        s_octal = 1u;
        nor_controller_config(&s_clk[clk], 1);
        s_clk_idx = (uint8_t)clk;
        if (nor_ioclk_on(s_clk[clk].ioclk_sel) != TIKU_NOR_OK) {
            return TIKU_NOR_ERR_CLOCK;
        }
        return TIKU_NOR_OK;
    }
    return rc;
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
 * @brief Poll WIP every @p step_us until it clears or @p max_us passes.
 *
 * @param step_us  gap between polls -- 100 us for programs, 10 ms for
 *                 erases; each poll is APB traffic into the busy controller
 * @param max_us   bound; past it the call returns TIKU_NOR_ERR_TIMEOUT
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

/**
 * @brief Read the flag-status register and translate program/erase errors.
 *
 * Serial mode only: in octal, or when the read fails, it returns TIKU_NOR_OK.
 */
static tiku_nor_err_t nor_check_flags(void)
{
    uint32_t f = 0u;
    if (s_octal) { return TIKU_NOR_OK; }   /* serial-only opcode here */
    if (nor_pio(NOR_CMD_READ_FLAGSTAT, 0u, &f, 1u, 1, 0, 0) != TIKU_NOR_OK) {
        return TIKU_NOR_OK;                /* unreadable flags: not an error */
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

    /* Default-deny outside the scratch sector: erase endurance is finite
     * and this driver runs unattended. */
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

    /* Power-safe: reading an MSPI register while its domain is unpowered
     * stalls the APB and hangs the CPU with no fault.  The two
     * always-readable registers are reported either way; the rest read
     * 0xDEADDEAD while the domain is down. */
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
     * either sense. */
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
/* BIT-BANG IDENTITY READ                                                    */
/*---------------------------------------------------------------------------*/

/*
 * Serial SPI on GPIO, as the PSRAM driver's bit-bang reader does on MSPI0:
 * drives CE/CLK/D0 by hand and samples D1, which shows whether the device is
 * alive and speaks serial SPI with the controller's framing, latency and lane
 * assignment out of the picture.
 *
 * Serial SPI here is mode 0: data launched on the falling edge, sampled by
 * the device on the rising edge; the device returns data on D1, sampled after
 * each rising edge.  Microsecond edges -- far slower than any timing
 * requirement.
 */

#define BB_OUT  (PAD_FNCSEL_GPIO | PAD_OUTCFG_PUSHPULL | PAD_INPEN | PAD_DS_0P5X)
#define BB_IN   (PAD_FNCSEL_GPIO | PAD_INPEN)
#define NOR_PAD_D1  96u

/** @brief Short busy-wait between bit-bang edges. */
static void bb_dwell(void)
{
    uint32_t n = 60u;
    while (n--) { __asm__ volatile ("nop"); }
}

/** @brief Level on D1 (GP96). */
static uint32_t bb_read_d1(void)
{
    /* D1 = GP96: RD2 covers pads 64..95, RD3 covers 96..127 -> bit 0. */
    return ((&GPIO->RD0)[3] >> 0) & 1u;
}

void tiku_nor_bitbang_id(uint8_t *out, uint32_t n_bytes)
{
    uint32_t i, b;

    /* Deassert reset first.  GP54 is high-Z out of SoC reset, and with no
     * pull-up on RSTn the device stays in reset, answering nothing on any
     * lane. */
    tiku_ambiq_gpio_pad_config(NOR_PAD_RST, BB_OUT);
    tiku_ambiq_gpio_set(NOR_PAD_RST, 1u);
    tiku_cpu_ambiq_delay_us(500u);

    /* Claim the four pins this path drives; leave the rest of the bus. */
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
    /* Checks the bit-bang read path itself.
     *
     * tiku_nor_bitbang_id() reads D1 (GP96).  A wrong read path reports
     * all-ones forever and looks exactly like a dead device -- so D1 is
     * driven as an output, low then high, and read back each time.  Bits:
     *   b0 = value read while driving low  (want 0)
     *   b1 = value read while driving high (want 1)
     *   b2 = value read with D1 released to input (the device's own level)
     *   b3 = same for D0 (GP95), the transmit line
     * So 0x02 or 0x06 means the read path works.  0x03 or 0x07 means the
     * read is stuck high, and an all-0xFF bit-bang identity means nothing.
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
