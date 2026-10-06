/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xflash_arch.c - EK-RA8P1 Octo-SPI NOR bring-up.
 *
 * Two protocols share one command table: 1S-1S-1S at reset, and 8D-8D-8D at
 * up to 125 MHz after opi_enter(), which checks itself against factory SFDP.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include "tiku_xflash_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"
#include <kernel/fs/tiku_nvm_backend.h>

/*
 * OSPI unit 0: the flash pins, on PORT1 and PORT8, carry OM_0_* functions at
 * PSEL 11100b and no OM_1_* function, so OSPI1 is wired to nothing here.
 */
#define XF_UNIT   0U
/*
 * Chip select 1: at PSEL 11100b, P104, the board's OSPI_FLASH_CS#, is
 * OM_0_CS1.  OM_0_CS0 is P107, which this board gives to Ethernet.
 */
#define XF_CS     1U

/*
 * EK-RA8P1 wiring (board manual Table 29), listed pin by pin: the SIO order
 * does not follow port-pin order (SIO1 is P803, SIO2 is P103).
 */
#define XP(port, pin)  (uint16_t)(((port) << 8) | (pin))

static const uint16_t xf_pins[] = {
    XP(1, 6),  /* RESET# */  XP(1, 5),  /* ECS#  */
    XP(8, 8),  /* SCLK   */  XP(1, 4),  /* CS#   */
    XP(8, 1),  /* DQS    */
    XP(1, 0),  /* SIO0 */ XP(8, 3), /* SIO1 */ XP(1, 3), /* SIO2 */
    XP(1, 1),  /* SIO3 */ XP(1, 2), /* SIO4 */ XP(8, 0), /* SIO5 */
    XP(8, 2),  /* SIO6 */ XP(8, 4), /* SIO7 */
};

#define XF_NPINS  (sizeof(xf_pins) / sizeof(xf_pins[0]))

static uint8_t xf_ready;
static uint8_t xf_opi;      /**< 0 = 1S-1S-1S, 1 = 8D-8D-8D */
static uint8_t xf_ddrsmpex; /**< calibrated DDR sampling extension  */
static uint8_t xf_dqs_shift; /**< calibrated OM_DQS delay cells      */
static uint8_t xf_dqs_width; /**< how many cells worked, i.e. margin */

/*
 * Per-protocol shape of a command.  In OPI, commands that carry no address in
 * SPI still send four zero address bytes, and the latency depends on the
 * command class: four cycles for a register read, the configured DC (20 by
 * default) for an array read.  A wrong address size or latency returns
 * shifted data, not an error.
 */
#define XF_ADDR_REG   (xf_opi ? 4U : 0U)
#define XF_LATE_REG   (xf_opi ? 4U : 0U)
#define XF_LATE_ARRAY (xf_opi ? 20U : 8U)

void tiku_ra8p1_xflash_init(void)
{
    unsigned i;

    if (xf_ready) {
        return;
    }

    /* Module stop is released first: no OSPI register answers while set. */
    TIKU_REG32(RA8P1_MSTPCRB) &= ~RA8P1_MSTPB_OSPI0;
    (void)TIKU_REG32(RA8P1_MSTPCRB);

    TIKU_REG8(RA8P1_PWPR_S) = 0U;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_PFSWE;
    for (i = 0; i < XF_NPINS; i++) {
        uint32_t port = (uint32_t)(xf_pins[i] >> 8);
        uint32_t pin  = (uint32_t)(xf_pins[i] & 0xFFU);

        /* High-speed high drive: an under-driven fast bus returns data that
         * is nearly right. */
        TIKU_REG32(RA8P1_PFS(port, pin)) =
            (RA8P1_PFS_PSEL_OSPI << RA8P1_PFS_PSEL_SHIFT) |
            RA8P1_PFS_DSCR_HS_HIGH | RA8P1_PFS_PMR;
    }
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_B0WI;
    __asm__ volatile ("dsb" ::: "memory");

    /* The controller starts in single-bit mode, which the device answers
     * after a reset. */
    TIKU_REG32(RA8P1_OSPI_LIOCFG(XF_UNIT, XF_CS)) =
        TIKU_REG32(RA8P1_OSPI_LIOCFG(XF_UNIT, XF_CS)) &
        ~(RA8P1_LIOCFG_PRTMD_MASK | RA8P1_LIOCFG_DDRSMPEX_MASK);

    /*
     * The flash's reset is pulsed, not only released.  LIOCTL.RSTCS0 drives
     * OM_RESET and resets to 0 (low), so muxing P106 to the OSPI function
     * holds the part in hardware reset.  An MCU reset does not reset this
     * pin's state, so after a reflash the line can already be high, and a
     * device left in octal mode by the previous session answers every
     * single-bit command with 0xFF until it is pulsed.
     */
    tiku_ra8p1_xflash_reset();

    /* CS idle time between frames: 8 cycles (CSMIN 7), also its reset
     * value. */
    TIKU_REG32(RA8P1_OSPI_LIOCFG(XF_UNIT, XF_CS)) =
        (TIKU_REG32(RA8P1_OSPI_LIOCFG(XF_UNIT, XF_CS)) &
         ~RA8P1_LIOCFG_CSMIN_MASK) | RA8P1_LIOCFG_CSMIN(7U);
    __asm__ volatile ("dsb" ::: "memory");

    xf_ready = 1U;
}

int tiku_ra8p1_xflash_in_opi(void)
{
    return (int)xf_opi;
}

/** @brief Drive OM_RESET low for 100 us, then high, and wait 1 ms; LIOCTL's
 *         other bits are kept. */
void tiku_ra8p1_xflash_reset(void)
{
    TIKU_REG32(RA8P1_OSPI_LIOCTL(XF_UNIT)) &= ~RA8P1_OSPI_LIOCTL_RSTCS0;
    __asm__ volatile ("dsb" ::: "memory");
    tiku_cpu_ra8p1_delay_us(100U);
    TIKU_REG32(RA8P1_OSPI_LIOCTL(XF_UNIT)) |= RA8P1_OSPI_LIOCTL_RSTCS0;
    __asm__ volatile ("dsb" ::: "memory");
    tiku_cpu_ra8p1_delay_us(1000U);
}

/*
 * OM_SCLK is OCTACLK/2 (UM Table 4.4), under three ceilings: OCTACLK
 * 333.33 MHz, OCTADIVCLK 166.67 MHz and the MX25LW's own 133 MHz.  The
 * divider is chosen from the live PLL1P rate and keeps OM_SCLK at or below
 * 125 MHz.
 */

/**
 * @brief The OCTACLK divider that keeps OM_SCLK inside every limit.
 *
 * @param src_hz  Rate of the clock OCTACLK is about to be pointed at
 * @return An OCTACKDIVCR divider code
 */
static uint8_t xflash_octa_div(unsigned long src_hz)
{
    if (src_hz <= 250000000UL) {
        return (uint8_t)RA8P1_CKDIV_1;      /* <=125 MHz OM_SCLK */
    }
    if (src_hz <= 500000000UL) {
        return (uint8_t)RA8P1_CKDIV_2;
    }
    return (uint8_t)RA8P1_CKDIV_4;
}

/** @brief Both OSPI units, which the divider change has to stop together. */
#define XF_MSTPB_BOTH   (RA8P1_MSTPB_OSPI0 | RA8P1_MSTPB_OSPI1)

/** @brief Bring both OSPI units out of module stop, if they were put in. */
static void xflash_mstp_restart(int stopped)
{
    if (!stopped) {
        return;
    }
    TIKU_REG32(RA8P1_MSTPCRB) &= ~XF_MSTPB_BOTH;
    (void)TIKU_REG32(RA8P1_MSTPCRB);
    tiku_cpu_ra8p1_delay_us(30U);
}

/**
 * @brief Point OCTACLK at @p sel, per the UM request/ready handshake.
 *
 * Leaving a divider other than /1 needs both OSPI units in module stop (UM
 * 9.2.40).  Module stop keeps their internal state (UM 11.4), so the
 * protocol and DQS delay survive it.
 *
 * @param sel  OCTACKCR source select
 * @return TIKU_RA8P1_XFLASH_OK, or ERR_TIMEOUT if the handshake stalled
 */
static int xflash_set_clock(uint8_t sel)
{
    uint32_t spins;
    uint8_t  divcode;
    int      stopped = 0;

    /* /1 for MOCO.  For PLL1P, the divider for the live core rate, which is
     * PLL1P since CPUCK0 divides it by one. */
    divcode = (sel == RA8P1_OCTACKCR_SEL_MOCO)
                  ? (uint8_t)RA8P1_CKDIV_1
                  : xflash_octa_div(tiku_cpu_ra8p1_clock_get_hz());

    if ((TIKU_REG8(RA8P1_OCTACKDIVCR) & 0x0FU) !=
        (uint8_t)RA8P1_OCTACKDIV_1) {
        TIKU_REG32(RA8P1_MSTPCRB) |= XF_MSTPB_BOTH;
        (void)TIKU_REG32(RA8P1_MSTPCRB);
        /* UM Figure 11.2: 30 us of NOP after an MSTP change while CPUCLK0 is
         * above the ICLK ceiling, which it is at every rung over 240. */
        tiku_cpu_ra8p1_delay_us(30U);
        stopped = 1;
    }

    TIKU_REG16(RA8P1_PRCR_S) = RA8P1_PRCR_KEY | RA8P1_PRCR_PRC0;

    TIKU_REG8(RA8P1_OCTACKCR) |= (uint8_t)RA8P1_OCTACKCR_SREQ;
    for (spins = 100000UL; spins != 0UL; spins--) {
        if ((TIKU_REG8(RA8P1_OCTACKCR) & RA8P1_OCTACKCR_SRDY) != 0U) {
            break;
        }
    }
    if (spins == 0UL) {
        TIKU_REG16(RA8P1_PRCR_S) = RA8P1_PRCR_KEY;
        xflash_mstp_restart(stopped);
        return TIKU_RA8P1_XFLASH_ERR_TIMEOUT;
    }

    /* Source and divider are writable only while SRDY is set. */
    TIKU_REG8(RA8P1_OCTACKDIVCR) = divcode;
    TIKU_REG8(RA8P1_OCTACKCR) = (uint8_t)(RA8P1_OCTACKCR_SREQ | sel);

    TIKU_REG8(RA8P1_OCTACKCR) &= (uint8_t)~RA8P1_OCTACKCR_SREQ;
    for (spins = 100000UL; spins != 0UL; spins--) {
        if ((TIKU_REG8(RA8P1_OCTACKCR) & RA8P1_OCTACKCR_SRDY) == 0U) {
            break;
        }
    }
    TIKU_REG16(RA8P1_PRCR_S) = RA8P1_PRCR_KEY;

    xflash_mstp_restart(stopped);

    return (spins != 0UL) ? TIKU_RA8P1_XFLASH_OK
                          : TIKU_RA8P1_XFLASH_ERR_TIMEOUT;
}

int tiku_ra8p1_xflash_cmd(uint16_t cmd, uint32_t addr, uint8_t addr_bytes,
                          uint8_t dummy, void *data, uint8_t len,
                          int is_write)
{
    uint32_t spins, d0, d1;
    uint32_t cmdbits = (uint32_t)cmd;
    uint8_t  cmdsize = 1U;
    uint8_t *b = (uint8_t *)data;
    unsigned i;

    if (len > 8U || addr_bytes > 4U) {
        return TIKU_RA8P1_XFLASH_ERR_ID;
    }
    /*
     * DTR octal addresses the array in 2-byte units and the datasheet
     * requires A0 = 0; the device accepts an odd address, reports success
     * and moves the wrong bytes.  Every manual transfer passes through here.
     */
    if (xf_opi && addr_bytes > 0U && (addr & 1UL) != 0UL) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }
    tiku_ra8p1_xflash_init();

    if (is_write && b != NULL) {
        d0 = 0UL;
        d1 = 0UL;
        for (i = 0; i < len; i++) {
            if (i < 4U) { d0 |= (uint32_t)b[i] << (8U * i); }
            else        { d1 |= (uint32_t)b[i] << (8U * (i - 4U)); }
        }
        TIKU_REG32(RA8P1_OSPI_CDD0BUF(XF_UNIT, 0)) = d0;
        TIKU_REG32(RA8P1_OSPI_CDD1BUF(XF_UNIT, 0)) = d1;
    } else {
        TIKU_REG32(RA8P1_OSPI_CDD0BUF(XF_UNIT, 0)) = 0UL;
        TIKU_REG32(RA8P1_OSPI_CDD1BUF(XF_UNIT, 0)) = 0UL;
    }

    /*
     * In octal the opcode is sent twice: the value, then its bitwise
     * complement.  Every opcode in the part's OPI tables follows this
     * (9F/60, 06/F9, 21/DE, EE/11), so the second byte is computed and one
     * command table serves both protocols.
     */
    if (xf_opi) {
        cmdbits = ((uint32_t)cmd & 0xFF00UL) |
                  ((~((uint32_t)cmd >> 8)) & 0xFFUL);
        cmdsize = 2U;
    }

    TIKU_REG32(RA8P1_OSPI_CDTBUF(XF_UNIT, 0)) =
        RA8P1_CDTBUF_CMD(cmdbits) |
        RA8P1_CDTBUF_CMDSIZE(cmdsize) |
        RA8P1_CDTBUF_ADDSIZE(addr_bytes) |
        RA8P1_CDTBUF_DATASIZE(len) |
        RA8P1_CDTBUF_LATE(dummy) |
        (is_write ? RA8P1_CDTBUF_TRTYPE_WRITE : 0UL);
    TIKU_REG32(RA8P1_OSPI_CDABUF(XF_UNIT, 0)) = addr;

    TIKU_REG32(RA8P1_OSPI_CDCTL0(XF_UNIT)) =
        ((XF_CS != 0U) ? RA8P1_CDCTL0_CSSEL : 0UL) | RA8P1_CDCTL0_TRREQ;
    __asm__ volatile ("dsb" ::: "memory");

    for (spins = 1000000UL; spins != 0UL; spins--) {
        if ((TIKU_REG32(RA8P1_OSPI_CDCTL0(XF_UNIT)) &
             RA8P1_CDCTL0_TRREQ) == 0UL) {
            break;
        }
    }
    if (spins == 0UL) {
        return TIKU_RA8P1_XFLASH_ERR_TIMEOUT;
    }

    if (!is_write && b != NULL) {
        d0 = TIKU_REG32(RA8P1_OSPI_CDD0BUF(XF_UNIT, 0));
        d1 = TIKU_REG32(RA8P1_OSPI_CDD1BUF(XF_UNIT, 0));
        for (i = 0; i < len; i++) {
            b[i] = (i < 4U) ? (uint8_t)(d0 >> (8U * i))
                            : (uint8_t)(d1 >> (8U * (i - 4U)));
        }
    }
    return TIKU_RA8P1_XFLASH_OK;
}

int tiku_ra8p1_xflash_read_sfdp(uint32_t addr, void *dst, uint8_t len)
{
    /* 0x5A with three address bytes and eight dummy cycles is the JESD216
     * form for SPI; octal widens the address to four bytes and the latency
     * to the configured DC. */
    return tiku_ra8p1_xflash_cmd(0x5A00U, addr, (uint8_t)(xf_opi ? 4U : 3U),
                                 (uint8_t)XF_LATE_ARRAY, dst, len, 0);
}

int tiku_ra8p1_xflash_read_status(uint8_t *sr)
{
    return tiku_ra8p1_xflash_cmd(0x0500U, 0UL, (uint8_t)XF_ADDR_REG,
                                 (uint8_t)XF_LATE_REG, sr, 1U, 0);
}

int tiku_ra8p1_xflash_read(uint32_t addr, void *dst, uint8_t len)
{
    if (addr >= TIKU_RA8P1_XFLASH_BYTES) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }
    /* 8DTRD (0xEE) in octal, FAST READ 4B (0x0C) in single.  Both take a
     * four-byte address; only the latency and the lane count differ. */
    return tiku_ra8p1_xflash_cmd((uint16_t)(xf_opi ? 0xEE00U : 0x0C00U),
                                 addr, 4U, (uint8_t)XF_LATE_ARRAY,
                                 dst, len, 0);
}

/**
 * @brief Poll WIP once a millisecond until the device goes idle.
 *
 * Callers pass a bound at or above the datasheet maximum for the operation.
 *
 * @return TIKU_RA8P1_XFLASH_OK when WIP clears, ERR_BUSY after @p ms polls,
 *         or ERR_TIMEOUT when a status read fails
 */
static int xflash_wait_idle(uint32_t ms)
{
    uint32_t i;
    uint8_t sr;

    for (i = 0; i < ms; i++) {
        if (tiku_ra8p1_xflash_read_status(&sr) != TIKU_RA8P1_XFLASH_OK) {
            return TIKU_RA8P1_XFLASH_ERR_TIMEOUT;
        }
        if ((sr & 0x01U) == 0U) {          /* WIP clear */
            return TIKU_RA8P1_XFLASH_OK;
        }
        tiku_cpu_ra8p1_delay_us(1000U);
    }
    return TIKU_RA8P1_XFLASH_ERR_BUSY;
}

/** @brief Send WREN and confirm WEL latched; ERR_BUSY when it did not. */
static int xflash_write_enable(void)
{
    uint8_t sr;
    int rc = tiku_ra8p1_xflash_cmd(0x0600U, 0UL, 0U, 0U, NULL, 0U, 1);

    if (rc != TIKU_RA8P1_XFLASH_OK) {
        return rc;
    }
    /* A write-enable that did not latch turns the next erase or program
     * into a no-op that reports nothing. */
    rc = tiku_ra8p1_xflash_read_status(&sr);
    if (rc != TIKU_RA8P1_XFLASH_OK) {
        return rc;
    }
    return ((sr & 0x02U) != 0U) ? TIKU_RA8P1_XFLASH_OK
                                : TIKU_RA8P1_XFLASH_ERR_BUSY;
}

/** @brief Shared body for the two erase granularities. */
static int xflash_erase(uint16_t cmd, uint32_t addr, uint32_t ms)
{
    int rc;

    if (addr >= TIKU_RA8P1_XFLASH_BYTES) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }
    rc = xflash_write_enable();
    if (rc != TIKU_RA8P1_XFLASH_OK) {
        return rc;
    }
    /* Four-byte-address opcodes throughout: three-byte addresses reach only
     * 16 MB of the 64 MB part. */
    rc = tiku_ra8p1_xflash_cmd(cmd, addr, 4U, 0U, NULL, 0U, 1);
    if (rc != TIKU_RA8P1_XFLASH_OK) {
        return rc;
    }
    return xflash_wait_idle(ms);
}

/*
 * Erase budgets are five times the MX25LM (3 V) datasheet maxima.  The
 * fitted part is the 1.8 V MX25LW, whose erases run longer than the MX25LM
 * figures; a budget that runs out means a failed part.
 */
int tiku_ra8p1_xflash_erase_sector(uint32_t addr)
{
    return xflash_erase(0x2100U, addr, 2000UL);     /* SE4B */
}

int tiku_ra8p1_xflash_erase_block(uint32_t addr)
{
    return xflash_erase(0xDC00U, addr, 10000UL);    /* BE4B */
}

int tiku_ra8p1_xflash_program(uint32_t addr, const void *src, uint8_t len)
{
    int rc;

    if (len == 0U || len > 8U || addr >= TIKU_RA8P1_XFLASH_BYTES) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }
    /* A page program that runs off the end of its page wraps to the start
     * of the same page, so a span that crosses a page is refused. */
    if ((addr & (TIKU_RA8P1_XFLASH_PAGE - 1UL)) + len >
        TIKU_RA8P1_XFLASH_PAGE) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }

    rc = xflash_write_enable();
    if (rc != TIKU_RA8P1_XFLASH_OK) {
        return rc;
    }
    rc = tiku_ra8p1_xflash_cmd(0x1200U, addr, 4U, 0U, (void *)src, len, 1);
    if (rc != TIKU_RA8P1_XFLASH_OK) {
        return rc;
    }
    return xflash_wait_idle(5UL);                   /* tPP max 0.75 ms */
}

/*
 * Configuration Register 2 has an address space of its own.  CR2 address
 * 0x40000000 holds DEFSOPI#/DEFDOPI#, which are OTP: one write permanently
 * changes the protocol the part speaks at power-on.  xflash_write_cr2()
 * accepts only the two volatile addresses below.
 */
#define XF_CR2_MODE       0x00000000UL   /* volatile: SPI / SOPI / DOPI */
#define XF_CR2_DUMMY      0x00000300UL   /* volatile: DC[2:0]           */
#define XF_CR2_MODE_SPI   0x00U
#define XF_CR2_MODE_DOPI  0x02U

/** @brief Write one volatile CR2 register; other addresses get ERR_RANGE. */
static int xflash_write_cr2(uint32_t cr2_addr, uint8_t val)
{
    int rc;

    if (cr2_addr != XF_CR2_MODE && cr2_addr != XF_CR2_DUMMY) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }
    rc = xflash_write_enable();
    if (rc != TIKU_RA8P1_XFLASH_OK) {
        return rc;
    }
    return tiku_ra8p1_xflash_cmd(0x7200U, cr2_addr, 4U, 0U, &val, 1U, 1);
}

/** @brief Put the controller's link layer into one protocol or the other. */
static void xflash_set_protocol(uint32_t prtmd, uint8_t ddrsmpex)
{
    uint32_t v = TIKU_REG32(RA8P1_OSPI_LIOCFG(XF_UNIT, XF_CS));

    v &= ~(RA8P1_LIOCFG_PRTMD_MASK | RA8P1_LIOCFG_DDRSMPEX_MASK);
    v |= prtmd | RA8P1_LIOCFG_DDRSMPEX(ddrsmpex);
    TIKU_REG32(RA8P1_OSPI_LIOCFG(XF_UNIT, XF_CS)) = v;
    __asm__ volatile ("dsb" ::: "memory");
}

/**
 * @brief True when an SFDP read returns the JESD216 signature.
 *
 * SFDP is factory data in SPI order, and DOPI transfers bytes pair-swapped
 * (D1 D0 D3 D2 ...), so the signature reads "SFDP" in SPI, "FSPD" in DOPI.
 */
static int xflash_sfdp_ok(void)
{
    uint8_t s[4] = { 0, 0, 0, 0 };

    if (tiku_ra8p1_xflash_read_sfdp(0UL, s, 4U) != TIKU_RA8P1_XFLASH_OK) {
        return 0;
    }
    if (xf_opi) {
        return (s[0] == 'F' && s[1] == 'S' && s[2] == 'P' && s[3] == 'D');
    }
    return (s[0] == 'S' && s[1] == 'F' && s[2] == 'D' && s[3] == 'P');
}

/** @brief Set the DQS delay-cell count for this chip select. */
static void xflash_set_dqs_shift(uint8_t cells)
{
    TIKU_REG32(RA8P1_OSPI_WRAPCFG(XF_UNIT)) =
        (TIKU_REG32(RA8P1_OSPI_WRAPCFG(XF_UNIT)) &
         ~RA8P1_WRAPCFG_DSSFTCS1_MASK) | RA8P1_WRAPCFG_DSSFTCS1(cells);
    __asm__ volatile ("dsb" ::: "memory");
}

/**
 * @brief Find the DQS delay that centres the sampling point, or fail.
 *
 * Sweeps all 32 delay cells against the factory SFDP signature and settles
 * on the middle of the widest passing run, which has margin on both sides.
 * The window moves with the clock, so the last sweep runs at the final speed.
 *
 * @return TIKU_RA8P1_XFLASH_OK, or ERR_ID when no delay cell works
 */
static int xflash_dqs_calibrate(void)
{
    unsigned sh, run = 0, best_len = 0, best_end = 0;

    for (sh = 0; sh < 32U; sh++) {
        xflash_set_dqs_shift((uint8_t)sh);
        if (xflash_sfdp_ok()) {
            run++;
            if (run > best_len) {
                best_len = run;
                best_end = sh;
            }
        } else {
            run = 0;
        }
    }
    if (best_len == 0U) {
        xflash_set_dqs_shift(0U);
        return TIKU_RA8P1_XFLASH_ERR_ID;
    }
    xf_dqs_shift = (uint8_t)(best_end - (best_len / 2U));
    xf_dqs_width = (uint8_t)best_len;
    xflash_set_dqs_shift(xf_dqs_shift);
    return TIKU_RA8P1_XFLASH_OK;
}

int tiku_ra8p1_xflash_opi_enter(void)
{
    int rc;
    unsigned ex;
    int got_slow = 0;

    tiku_ra8p1_xflash_init();
    if (xf_opi) {
        return TIKU_RA8P1_XFLASH_OK;
    }

    /*
     * DTR octal: LIOCFGCSn.PRTMD has an 8D-8D-8D encoding and no 8S-8S-8S
     * one.  The switch runs at the reset clock, since in single-bit mode the
     * part's register reads are specified only to 66 MHz.
     */
    rc = xflash_write_cr2(XF_CR2_MODE, XF_CR2_MODE_DOPI);
    if (rc != TIKU_RA8P1_XFLASH_OK) {
        return rc;
    }
    tiku_cpu_ra8p1_delay_us(100U);

    /* From here the device answers only octal, so the controller must follow
     * before any other command is issued. */
    xf_opi = 1U;

    /*
     * Stage one: calibrate at the slow clock, where the sampling window is
     * wide enough that a failure means a wrong frame shape, not timing.
     */
    xflash_set_protocol(RA8P1_LIOCFG_PRTMD_8D8D8D, 0U);
    got_slow = (xflash_dqs_calibrate() == TIKU_RA8P1_XFLASH_OK);
    if (!got_slow) {
        /* No delay works at the slow clock: the frame shape is wrong.  This
         * is ERR_ID, apart from the at-speed ERR_TIMEOUT below. */
        rc = TIKU_RA8P1_XFLASH_ERR_ID;
        goto fail;
    }

    /* Stage two: raise the clock and calibrate again; the slow-clock result
     * does not carry over to the full rate. */
    rc = xflash_set_clock(RA8P1_OCTACKCR_SEL_PLL1P);
    if (rc != TIKU_RA8P1_XFLASH_OK) {
        goto fail;
    }

    /* At speed, DDRSMPEX widens the sampling window, from 0 up to 7, until
     * a delay sweep passes; it covers a memory output delay longer than one
     * cycle. */
    for (ex = 0; ex < 8U; ex++) {
        xflash_set_protocol(RA8P1_LIOCFG_PRTMD_8D8D8D, (uint8_t)ex);
        if (xflash_dqs_calibrate() == TIKU_RA8P1_XFLASH_OK) {
            xf_ddrsmpex = (uint8_t)ex;
            return TIKU_RA8P1_XFLASH_OK;
        }
    }
    /* Octal passed at the slow clock, so this is a timing failure. */
    rc = TIKU_RA8P1_XFLASH_ERR_TIMEOUT;

fail:
    /*
     * Back out: slow the clock, reset the device, which returns it to SPI as
     * the mode bits are volatile, then set the controller to single-bit.
     */
    (void)xflash_set_clock(RA8P1_OCTACKCR_SEL_MOCO);
    tiku_ra8p1_xflash_reset();
    xf_opi = 0U;
    xflash_set_protocol(RA8P1_LIOCFG_PRTMD_1S1S1S, 0U);
    return rc;
}

int tiku_ra8p1_xflash_opi_exit(void)
{
    if (!xf_opi) {
        return TIKU_RA8P1_XFLASH_OK;
    }
    (void)xflash_set_clock(RA8P1_OCTACKCR_SEL_MOCO);
    tiku_ra8p1_xflash_reset();
    xf_opi = 0U;
    xflash_set_protocol(RA8P1_LIOCFG_PRTMD_1S1S1S, 0U);
    return TIKU_RA8P1_XFLASH_OK;
}

int tiku_ra8p1_xflash_opi_active(void)
{
    return (int)xf_opi;
}

int tiku_ra8p1_xflash_ddrsmpex(void)
{
    return (int)xf_ddrsmpex;
}

int tiku_ra8p1_xflash_dqs_shift(void)
{
    return (int)xf_dqs_shift;
}

int tiku_ra8p1_xflash_dqs_margin(void)
{
    return (int)xf_dqs_width;
}

int tiku_ra8p1_xflash_mmap_enable(void)
{
    tiku_ra8p1_xflash_init();

    /*
     * Single-bit mode uses FAST READ 4B (0x0C): its four-byte address reaches
     * all 64 MB, and its eight dummy cycles let the clock rise without a
     * change of command.  The opcode sits in CMD[15:8], as on the manual
     * path.
     */
    if (xf_opi) {
        /* 8DTRD (0xEE/0x11) in the profile-1.0 frame: two command bytes,
         * four address bytes, DC latency.  FFMT follows the link protocol:
         * a normal-format frame on an octal link sends a one-byte opcode,
         * which the device rejects. */
        TIKU_REG32(RA8P1_OSPI_CMCFG0(XF_UNIT, XF_CS)) =
            RA8P1_CMCFG0_FFMT_8D | RA8P1_CMCFG0_ADDSIZE(4U);
        TIKU_REG32(RA8P1_OSPI_CMCFG1(XF_UNIT, XF_CS)) =
            RA8P1_CMCFG1_RDCMD(0xEE11U) | RA8P1_CMCFG1_RDLATE(20U);
    } else {
        TIKU_REG32(RA8P1_OSPI_CMCFG0(XF_UNIT, XF_CS)) =
            RA8P1_CMCFG0_FFMT_NORMAL | RA8P1_CMCFG0_ADDSIZE(4U);
        TIKU_REG32(RA8P1_OSPI_CMCFG1(XF_UNIT, XF_CS)) =
            RA8P1_CMCFG1_RDCMD(0x0C00U) | RA8P1_CMCFG1_RDLATE(8U);
    }

    /* Prefetch on: without it every burst re-sends the command, address and
     * latency cycles. */
    TIKU_REG32(RA8P1_OSPI_BMCFG(XF_UNIT, 0U)) =
        TIKU_REG32(RA8P1_OSPI_BMCFG(XF_UNIT, 0U)) | RA8P1_BMCFG_PREEN;

    /* Read enable only: the bridge refuses a stray store into the window,
     * so it cannot start a program cycle. */
    TIKU_REG32(RA8P1_OSPI_BMCTL0(XF_UNIT)) =
        TIKU_REG32(RA8P1_OSPI_BMCTL0(XF_UNIT)) | RA8P1_BMCTL0_CH0CS1_RD;
    __asm__ volatile ("dsb\n\tisb" ::: "memory");

    return TIKU_RA8P1_XFLASH_OK;
}

/*
 * Bulk writes go through the mapped window: a manual transaction carries at
 * most eight bytes.  The bridge's write combination gathers CPU stores into
 * one frame of up to MWRSIZE, at most 64 bytes, the unit used here.
 *
 * Stores must be 64-bit while combining (the manual prohibits other widths),
 * and WEL clears after every program, so each chunk gets its own
 * write-enable on the manual path first.
 */
#define XF_WRCHUNK  64UL

int tiku_ra8p1_xflash_write(uint32_t addr, const void *src, uint32_t len)
{
    const uint64_t *s = (const uint64_t *)src;
    uint32_t done;
    int rc;

    if (src == NULL || len == 0UL) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }
    /* A misaligned request is refused: a frame that straddles a page wraps
     * and overwrites the head of the page. */
    if (((addr | len) & (XF_WRCHUNK - 1UL)) != 0UL ||
        ((uintptr_t)src & 7U) != 0U ||
        addr > TIKU_RA8P1_XFLASH_BYTES ||
        len > TIKU_RA8P1_XFLASH_BYTES - addr) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }

    tiku_ra8p1_xflash_mmap_enable();

    TIKU_REG32(RA8P1_OSPI_CMCFG2(XF_UNIT, XF_CS)) =
        RA8P1_CMCFG2_WRCMD((uint32_t)(xf_opi ? 0x12EDU : 0x1200U)) |
        RA8P1_CMCFG2_WRLATE(0U);
    TIKU_REG32(RA8P1_OSPI_BMCFG(XF_UNIT, 0U)) =
        (TIKU_REG32(RA8P1_OSPI_BMCFG(XF_UNIT, 0U)) & ~0xFF00UL) |
        RA8P1_BMCFG_MWRCOMB | RA8P1_BMCFG_MWRSIZE(RA8P1_BMCFG_MWRSIZE_64);
    TIKU_REG32(RA8P1_OSPI_BMCTL0(XF_UNIT)) |= RA8P1_BMCTL0_CH0CS1_WR;
    __asm__ volatile ("dsb\n\tisb" ::: "memory");

    rc = TIKU_RA8P1_XFLASH_OK;
    for (done = 0UL; done < len; done += XF_WRCHUNK) {
        volatile uint64_t *d = (volatile uint64_t *)
            (TIKU_RA8P1_XFLASH_ADDR + addr + done);
        unsigned k;

        rc = xflash_write_enable();
        if (rc != TIKU_RA8P1_XFLASH_OK) {
            break;
        }
        for (k = 0; k < XF_WRCHUNK / 8UL; k++) {
            d[k] = s[k];
        }
        __asm__ volatile ("dsb" ::: "memory");

        rc = xflash_wait_idle(50UL);
        if (rc != TIKU_RA8P1_XFLASH_OK) {
            break;
        }
        s += XF_WRCHUNK / 8UL;
    }

    /* Write access and write combination close again, so a stray store
     * into the window cannot start a program cycle. */
    TIKU_REG32(RA8P1_OSPI_BMCTL0(XF_UNIT)) &= ~RA8P1_BMCTL0_CH0CS1_WR;
    TIKU_REG32(RA8P1_OSPI_BMCFG(XF_UNIT, 0U)) &= ~RA8P1_BMCFG_MWRCOMB;
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
    return rc;
}

/**
 * @brief Write any span: 64-byte-aligned runs from an 8-byte-aligned source
 *        through the combined path, the rest eight bytes at a time.
 */
static int xflash_write_any(uint32_t addr, const uint8_t *p, uint32_t len)
{
    int rc;

    while (len != 0UL) {
        uint32_t n;

        if (((addr & (XF_WRCHUNK - 1UL)) == 0UL) && len >= XF_WRCHUNK &&
            (((uintptr_t)p & 7U) == 0U)) {
            n = len & ~(XF_WRCHUNK - 1UL);
            rc = tiku_ra8p1_xflash_write(addr, p, n);
        } else {
            /* Up to eight bytes, never across a page: a program that leaves
             * its page wraps to the page's start. */
            n = (len < 8UL) ? len : 8UL;
            if ((addr & (TIKU_RA8P1_XFLASH_PAGE - 1UL)) + n >
                TIKU_RA8P1_XFLASH_PAGE) {
                n = TIKU_RA8P1_XFLASH_PAGE -
                    (addr & (TIKU_RA8P1_XFLASH_PAGE - 1UL));
            }
            rc = tiku_ra8p1_xflash_program(addr, p, (uint8_t)n);
        }
        if (rc != TIKU_RA8P1_XFLASH_OK) {
            return rc;
        }
        addr += n;
        p    += n;
        len  -= n;
    }
    return TIKU_RA8P1_XFLASH_OK;
}

/** @brief Backend write: bounds and parity checks, then xflash_write_any(). */
static int xflash_be_write(struct tiku_nvm_backend *be, size_t off,
                           const void *src, size_t len)
{
    if (be == NULL || src == NULL || off + len > be->size) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }
    /* In octal mode an odd offset or length is refused before anything is
     * written, so no span is left half written. */
    if (xf_opi && ((off | len) & 1U) != 0U) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }
    return xflash_write_any((uint32_t)off, (const uint8_t *)src,
                            (uint32_t)len);
}

/** @brief Backend erase from a sector-aligned @p off, by block or sector. */
static int xflash_be_erase(struct tiku_nvm_backend *be, size_t off,
                           size_t len)
{
    size_t done;

    if (be == NULL || off + len > be->size ||
        (off & (TIKU_RA8P1_XFLASH_SECTOR - 1UL)) != 0U) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }
    for (done = 0; done < len; ) {
        int rc;

        /* Prefer the 64 KB opcode when a whole block is in range: sixteen
         * sector erases cost far more than one block erase. */
        if (((off + done) & (TIKU_RA8P1_XFLASH_BLOCK - 1UL)) == 0U &&
            (len - done) >= TIKU_RA8P1_XFLASH_BLOCK) {
            rc = tiku_ra8p1_xflash_erase_block((uint32_t)(off + done));
            done += TIKU_RA8P1_XFLASH_BLOCK;
        } else {
            rc = tiku_ra8p1_xflash_erase_sector((uint32_t)(off + done));
            done += TIKU_RA8P1_XFLASH_SECTOR;
        }
        if (rc != TIKU_RA8P1_XFLASH_OK) {
            return rc;
        }
    }
    return TIKU_RA8P1_XFLASH_OK;
}

static tiku_nvm_backend_t xf_backend = {
    (uint8_t *)TIKU_RA8P1_XFLASH_ADDR,
    (size_t)TIKU_RA8P1_XFLASH_BYTES,
    xflash_be_write,
    xflash_be_erase,
    NULL
};

tiku_nvm_backend_t *tiku_ra8p1_xflash_backend(void)
{
    /*
     * Octal is entered first so every access through the backend uses one
     * protocol: DOPI moves bytes pair-swapped relative to SPI, and data
     * written in one protocol reads back scrambled in the other, where a
     * stored object's header magic fails to match.  With octal not entered
     * there is no backend.
     */
    if (tiku_ra8p1_xflash_opi_enter() != TIKU_RA8P1_XFLASH_OK) {
        return NULL;
    }

    /* Reads through this backend are pointer dereferences into the mapped
     * window, so the map has to be open before anyone holds the pointer. */
    if (tiku_ra8p1_xflash_mmap_enable() != TIKU_RA8P1_XFLASH_OK) {
        return NULL;
    }
    return &xf_backend;
}

int tiku_ra8p1_xflash_read_id(uint8_t out[3])
{
    int rc;

    if (out == NULL) {
        return TIKU_RA8P1_XFLASH_ERR_ID;
    }
    rc = tiku_ra8p1_xflash_cmd((uint16_t)(RA8P1_MX_CMD_RDID << 8), 0UL, 0U,
                               0U, out, 3U, 0);
    if (rc != TIKU_RA8P1_XFLASH_OK) {
        return rc;
    }

    /* Only a Macronix part answers 0xC2; a floating bus reads otherwise. */
    if (out[0] != RA8P1_MX_MANUFACTURER) {
        return TIKU_RA8P1_XFLASH_ERR_ID;
    }
    return TIKU_RA8P1_XFLASH_OK;
}
