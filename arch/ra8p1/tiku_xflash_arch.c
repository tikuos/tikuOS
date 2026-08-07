/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xflash_arch.c - EK-RA8P1 Octo-SPI NOR bring-up.
 *
 * Two protocols share one command table: 1S-1S-1S at reset, 8D-8D-8D at
 * 120 MHz after opi_enter(), which verifies itself against factory SFDP.
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
 * OSPI0, not OSPI1.  The address map lists an OSPI1 window at 0x7000_0000 and
 * it is tempting to pair that with "the flash on the octal bus", but the pin
 * function tables settle it: PORT1 and PORT8 -- every pin this board uses --
 * carry OM_0_* functions at PSEL 11100b.  There is no OM_1_* on these ports,
 * so a driver aimed at unit 1 configures a controller wired to nothing and
 * every transaction completes against an empty bus.
 */
#define XF_UNIT   0U
/*
 * CS1, and the pin table is the only place that says so.  At PSEL 11100b,
 * P104 -- the board's OSPI_FLASH_CS# -- is OM_0_CS1; OM_0_CS0 is P107, which
 * this board gives to Ethernet.  Aim at CS0 and the flash never sees a
 * command while a live bus reads back all-ones.
 */
#define XF_CS     1U

/*
 * EK-RA8P1 wiring (board manual Table 29).  Transcribed, not generated: the
 * SIO order is NOT monotonic in port-pin -- SIO1 is P803 while SIO2 is P103 --
 * so a loop that looked reasonable would be wrong.
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
 * Per-protocol shape of a command.  Octal is not just "the same frame, more
 * lanes": commands that carry no address in SPI must still send four zero
 * address bytes in OPI, and the latency differs by command class -- four
 * cycles for a register read, the configured DC (20 by default) for anything
 * that reads the array.  Getting either wrong returns data that is shifted
 * rather than absent, which is the hardest kind of wrong to spot.
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

    /* Ungate first: no OSPI register answers while the module is stopped. */
    TIKU_REG32(RA8P1_MSTPCRB) &= ~RA8P1_MSTPB_OSPI0;
    (void)TIKU_REG32(RA8P1_MSTPCRB);

    TIKU_REG8(RA8P1_PWPR_S) = 0U;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_PFSWE;
    for (i = 0; i < XF_NPINS; i++) {
        uint32_t port = (uint32_t)(xf_pins[i] >> 8);
        uint32_t pin  = (uint32_t)(xf_pins[i] & 0xFFU);

        /* High-speed high drive, as the SDRAM bus needed: the failure mode of
         * an under-driven fast bus is data that is almost right, not silence. */
        TIKU_REG32(RA8P1_PFS(port, pin)) =
            (RA8P1_PFS_PSEL_OSPI << RA8P1_PFS_PSEL_SHIFT) |
            RA8P1_PFS_DSCR_HS_HIGH | RA8P1_PFS_PMR;
    }
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_B0WI;
    __asm__ volatile ("dsb" ::: "memory");

    /*
     * The controller must start in single-bit mode, since that is what the
     * device answers after a reset. */
    TIKU_REG32(RA8P1_OSPI_LIOCFG(XF_UNIT, XF_CS)) =
        TIKU_REG32(RA8P1_OSPI_LIOCFG(XF_UNIT, XF_CS)) &
        ~(RA8P1_LIOCFG_PRTMD_MASK | RA8P1_LIOCFG_DDRSMPEX_MASK);

    /*
     * PULSE the flash's reset, do not merely release it.
     *
     * LIOCTL.RSTCS0 drives OM_RESET and resets to 0 = drive low, so muxing
     * P106 to the OSPI function puts the part into hardware reset -- which
     * reads as a live, pulled-high bus with no device answering.  Releasing
     * it is therefore necessary, but NOT sufficient: an MCU reset does not
     * reset this pin's state, so after a reflash the line is already high
     * and no edge is produced.  A device left in octal mode by the previous
     * session then stays there, and every single-bit command at boot returns
     * 0xFF -- a "dead flash" that is really a live one mid-conversation in
     * another protocol.
     */
    tiku_ra8p1_xflash_reset();

    /*
     * CS idle term, written explicitly rather than left alone.  It already
     * resets to 0x7 = 8 cycles, which is the value wanted -- but a bus whose
     * inter-frame gap depends on a reset value is one silent register change
     * away from frames the device merges, so the intent is stated.
     */
    TIKU_REG32(RA8P1_OSPI_LIOCFG(XF_UNIT, XF_CS)) =
        (TIKU_REG32(RA8P1_OSPI_LIOCFG(XF_UNIT, XF_CS)) &
         ~RA8P1_LIOCFG_CSMIN_MASK) | RA8P1_LIOCFG_CSMIN(7U);
    __asm__ volatile ("dsb" ::: "memory");

    xf_ready = 1U;
}

/*
 * The escape hatch for a mode switch that did not take.  It works because
 * the mode bits are volatile: CR2[0x00000000] resets to 0 = SPI, so a part
 * left in an octal mode this controller cannot talk to is always one pulse
 * away from answering single-bit commands again.  Bit 17 of LIOCTL is
 * preserved by read-modify-write, as at init.
 */

int tiku_ra8p1_xflash_in_opi(void)
{
    return (int)xf_opi;
}

/** @brief Drive OM_RESET low and back, then wait out recovery. */
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
 * OM_SCLK is OCTACLK/2 (UM Table 4.4) and three ceilings apply: OCTACLK
 * 333.33, OCTADIVCLK 166.67, and the fitted MX25LW's own 133.  The part
 * binds, so this targets OM_SCLK just under 125 -- where the DQS eye was
 * calibrated -- rather than the fastest legal setting.  A hardcoded 1/1 is
 * correct only while PLL1P is 240; deriving from the live rate is what stops
 * a raised core clock driving the flash past its rating.
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

/**
 * @brief Point OCTACLK at @p sel, per the UM handshake.
 *
 * The MSTPCRB dance in steps 1-2 of the manual's procedure is skipped.  It is
 * required when changing away from 1/n, n != 1 -- which the return to MOCO
 * does whenever the core runs above 250 MHz, where 1/2 or 1/4 is selected.
 */
static int xflash_set_clock(uint8_t sel)
{
    uint32_t spins;
    uint8_t  divcode;

    /* MOCO when that is the selection, otherwise the live PLL1P -- which is
     * the core rate, since CPUCK0 divides PLL1P by one. */
    divcode = (sel == RA8P1_OCTACKCR_SEL_MOCO)
                  ? (uint8_t)RA8P1_CKDIV_1
                  : xflash_octa_div(tiku_cpu_ra8p1_clock_get_hz());

    TIKU_REG16(RA8P1_PRCR_S) = RA8P1_PRCR_KEY | RA8P1_PRCR_PRC0;

    TIKU_REG8(RA8P1_OCTACKCR) |= (uint8_t)RA8P1_OCTACKCR_SREQ;
    for (spins = 100000UL; spins != 0UL; spins--) {
        if ((TIKU_REG8(RA8P1_OCTACKCR) & RA8P1_OCTACKCR_SRDY) != 0U) {
            break;
        }
    }
    if (spins == 0UL) {
        TIKU_REG16(RA8P1_PRCR_S) = RA8P1_PRCR_KEY;
        return TIKU_RA8P1_XFLASH_ERR_TIMEOUT;
    }

    /* Source and divider are only writable while the ready flag is set --
     * that window is the whole point of the handshake. */
    TIKU_REG8(RA8P1_OCTACKDIVCR) = divcode;
    TIKU_REG8(RA8P1_OCTACKCR) = (uint8_t)(RA8P1_OCTACKCR_SREQ | sel);

    TIKU_REG8(RA8P1_OCTACKCR) &= (uint8_t)~RA8P1_OCTACKCR_SREQ;
    for (spins = 100000UL; spins != 0UL; spins--) {
        if ((TIKU_REG8(RA8P1_OCTACKCR) & RA8P1_OCTACKCR_SRDY) == 0U) {
            break;
        }
    }
    TIKU_REG16(RA8P1_PRCR_S) = RA8P1_PRCR_KEY;

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
     * DTR octal addresses the array in 2-byte units: the datasheet requires
     * A0 = 0, and a request that ignores it is not refused by the device --
     * it completes, reports success, and moves the wrong bytes.  Measured:
     * a 300-byte write at an odd offset returned OK with 251 bytes wrong.
     * Refusing here catches every path at once, since they all funnel
     * through this one transaction primitive.
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
     * complement, which is how the device tells a real command from a bus
     * glitch.  That relation holds for every opcode in the part's OPI
     * tables without exception -- 9F/60, 06/F9, 21/DE, EE/11 -- so the
     * second byte is computed rather than tabulated, and one command table
     * serves both protocols.
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
 * @brief Poll WIP until the device goes idle, or @p ms elapses.
 *
 * Bounded by the datasheet MAXIMUM for the operation, never by a typical:
 * a part that is merely slow must be reported as an error, not waited on
 * forever, and must not be declared finished early either.
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

/** @brief WREN, then confirm WEL actually latched before trusting it. */
static int xflash_write_enable(void)
{
    uint8_t sr;
    int rc = tiku_ra8p1_xflash_cmd(0x0600U, 0UL, 0U, 0U, NULL, 0U, 1);

    if (rc != TIKU_RA8P1_XFLASH_OK) {
        return rc;
    }
    /* Read WEL back rather than assume: a write-enable that did not latch
     * turns every following erase and program into a silent no-op, which
     * reads back as "the data did not stick" long after the cause. */
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
    /* Four-byte-address opcodes throughout: a 64 MB part is past what the
     * three-byte forms can address, and mixing the two depends on a mode
     * bit nobody set. */
    rc = tiku_ra8p1_xflash_cmd(cmd, addr, 4U, 0U, NULL, 0U, 1);
    if (rc != TIKU_RA8P1_XFLASH_OK) {
        return rc;
    }
    return xflash_wait_idle(ms);
}

/*
 * Erase budgets are 5x the datasheet maxima, deliberately.
 *
 * The reference to hand is the MX25LM (3 V); the fitted part is the MX25LW
 * (1.8 V), and measurement already shows the timings do NOT simply carry
 * across: a sector erase held WIP past 50 ms where LM quotes 25 ms typical,
 * and the first erase attempt overran a 500 ms budget built from LM's 400 ms
 * maximum.  Since the cost of a generous ceiling is nothing on the happy
 * path and the cost of a tight one is a spurious failure on a healthy
 * device, these bound "the part is broken", not "the part is slow".
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
    /* A page program that runs off the end of its page WRAPS to the start of
     * the same page rather than continuing -- silent corruption, so it is
     * refused here instead. */
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
 * Configuration Register 2 lives in a small address space of its own, and
 * one address in it is a LANDMINE: 0x40000000 holds DEFSOPI#/DEFDOPI#, which
 * are OTP.  Writing them once permanently changes which protocol the part
 * speaks at power-on -- there is no second write to undo it, and a board
 * whose flash boots in a mode its loader does not speak is a board that
 * needs rework.  So this refuses every address except the volatile ones it
 * has a reason to touch.  The guard is deliberately a whitelist: a typo in
 * a caller should fail, not reach silicon.
 */
#define XF_CR2_MODE       0x00000000UL   /* volatile: SPI / SOPI / DOPI */
#define XF_CR2_DUMMY      0x00000300UL   /* volatile: DC[2:0]           */
#define XF_CR2_MODE_SPI   0x00U
#define XF_CR2_MODE_DOPI  0x02U

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

/*
 * The expected spelling depends on the protocol.  SFDP is factory data laid
 * down in SPI order, and DOPI transfers bytes PAIR-SWAPPED (D1 D0 D3 D2 ...),
 * so the same four bytes read as "SFDP" on one link and "FSPD" on the other.
 * A checker that admits only the first spelling rejects a perfectly
 * calibrated octal bus.
 */

/** @brief True when an SFDP read returns the JESD216 signature. */
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

/*
 * Sweeps all 32 delay cells against the device's own factory SFDP signature
 * and settles on the MIDDLE of the widest run that works, not the first cell
 * that happens to pass.  The first passing cell is by definition the edge of
 * the window, where a few degrees or a few millivolts move it back out; the
 * centre is the point with margin on both sides.  The window moves with the
 * clock, so this runs at the final speed rather than being tabulated.
 */

/**
 * @brief Find the DQS delay that centres the sampling point, or fail.
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
     * DTR octal, not STR.  That is forced by the controller, not preferred:
     * LIOCFGCSn.PRTMD has an 8D-8D-8D encoding and no 8S-8S-8S one, so the
     * device's SOPI mode has no expressible counterpart on this MCU.
     *
     * The switch happens at the reset clock, before any speed-up.  In single
     * mode the part's register reads are only specified to 66 MHz, so raising
     * the clock first would mean running the very commands that perform the
     * switch out of specification -- and it would also confound a protocol
     * fault with a timing one, which is exactly the confusion that has to be
     * avoided when the whole conversation changes shape at once.
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
     * Stage one: prove the protocol at the SLOW clock, where the sampling
     * window is so wide that timing cannot be the explanation for a failure.
     * Whatever works here is a statement about the frame shape alone.
     */
    xflash_set_protocol(RA8P1_LIOCFG_PRTMD_8D8D8D, 0U);
    got_slow = (xflash_dqs_calibrate() == TIKU_RA8P1_XFLASH_OK);
    if (!got_slow) {
        /* No sampling point works even where timing cannot be the cause, so
         * the frame shape itself is wrong.  Reported as ERR_ID to separate it
         * from the speed-related failure below -- the two want completely
         * different investigations. */
        rc = TIKU_RA8P1_XFLASH_ERR_ID;
        goto fail;
    }

    /*
     * Stage two: raise the clock, then re-calibrate.  The sampling extension
     * that suited 4 MHz says nothing about 120 MHz, so the search runs again
     * rather than trusting the earlier answer.
     */
    rc = xflash_set_clock(RA8P1_OCTACKCR_SEL_PLL1P);
    if (rc != TIKU_RA8P1_XFLASH_OK) {
        goto fail;
    }

    /* Re-calibrate at speed, and widen the sampling window if the plain
     * delay sweep cannot find one -- DDRSMPEX exists precisely for the case
     * where the memory's output delay exceeds a single cycle. */
    for (ex = 0; ex < 8U; ex++) {
        xflash_set_protocol(RA8P1_LIOCFG_PRTMD_8D8D8D, (uint8_t)ex);
        if (xflash_dqs_calibrate() == TIKU_RA8P1_XFLASH_OK) {
            xf_ddrsmpex = (uint8_t)ex;
            return TIKU_RA8P1_XFLASH_OK;
        }
    }
    /* Octal is proven good at the slow clock, so the frame is right and only
     * the speed is not: a timing failure, distinct from ERR_ID above. */
    rc = TIKU_RA8P1_XFLASH_ERR_TIMEOUT;

fail:
    /*
     * Back all the way out.  Slow the clock first, then reset the device --
     * which returns it to SPI because the mode bits are volatile -- and only
     * then tell the controller to speak single again.
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
     * FAST READ 4B (0x0C): an explicit four-byte-address opcode, chosen over
     * plain READ (0x03) because 0x03 reaches only 16 MB of a 64 MB part and
     * over 0x13 because the fast form's eight dummy cycles are what lets the
     * clock rise later without changing the command.  The opcode sits in
     * CMD[15:8], the same placement the manual path needs.
     */
    if (xf_opi) {
        /* 8DTRD (0xEE/0x11) under the profile-1.0 frame: two command bytes,
         * four address bytes, DC latency.  FFMT must move with the link
         * protocol -- a normal-format frame on an octal link sends a
         * one-byte opcode the device rejects as a failed complement. */
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

    /* Prefetch on.  A mapped read without it re-sends command, address and
     * the frame's latency cycles for every burst the CPU asks for, which on a
     * bus this fast is nearly all of the time. */
    TIKU_REG32(RA8P1_OSPI_BMCFG(XF_UNIT, 0U)) =
        TIKU_REG32(RA8P1_OSPI_BMCFG(XF_UNIT, 0U)) | RA8P1_BMCFG_PREEN;

    /* Read enable only.  Leaving write disabled means a stray store into the
     * window is refused by the bridge rather than becoming a program cycle
     * on a device whose erase state nobody checked. */
    TIKU_REG32(RA8P1_OSPI_BMCTL0(XF_UNIT)) =
        TIKU_REG32(RA8P1_OSPI_BMCTL0(XF_UNIT)) | RA8P1_BMCTL0_CH0CS1_RD;
    __asm__ volatile ("dsb\n\tisb" ::: "memory");

    return TIKU_RA8P1_XFLASH_OK;
}

/*
 * Bulk write goes through the mapped window, because the manual path cannot:
 * its data buffers are two 32-bit registers, so eight bytes is the hard
 * ceiling per transaction, and eight bytes per program cycle would put a
 * 64 MB device somewhere north of an hour.  The bridge's write-combination
 * mode gathers CPU stores into one frame instead -- up to MWRSIZE, whose
 * maximum is 64 bytes, so that is the unit this works in.
 *
 * Two constraints come with it.  Stores must be 64-bit while combining (the
 * manual prohibits any other width), and WEL is not something the controller
 * knows about: it clears after every program, so each chunk needs its own
 * write-enable issued down the manual path first.
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
    /* Refuse a misaligned request rather than issue a frame that straddles a
     * page and wraps -- the device would corrupt the head of the page. */
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

    /* Close the window again.  Leaving the map writable turns any stray
     * store into a program cycle on a device nobody checked the erase state
     * of, which is a corruption that surfaces long after its cause. */
    TIKU_REG32(RA8P1_OSPI_BMCTL0(XF_UNIT)) &= ~RA8P1_BMCTL0_CH0CS1_WR;
    TIKU_REG32(RA8P1_OSPI_BMCFG(XF_UNIT, 0U)) &= ~RA8P1_BMCFG_MWRCOMB;
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
    return rc;
}

/*
 * A filesystem writes whatever length it likes at whatever offset it likes,
 * so the backend cannot inherit the combined path's 64-byte alignment rule.
 * It splits instead: unaligned head and tail go down the manual path eight
 * bytes at a time, and only the aligned middle uses the fast combined frames.
 * Slow at the edges, fast where it matters, correct everywhere.
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
            /* Up to eight bytes, and never across a page: the device wraps a
             * program that would leave its page rather than continuing. */
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

static int xflash_be_write(struct tiku_nvm_backend *be, size_t off,
                           const void *src, size_t len)
{
    if (be == NULL || src == NULL || off + len > be->size) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }
    /* Stated up front rather than discovered eight bytes in, so a caller
     * gets a clean refusal instead of a partially written span. */
    if (xf_opi && ((off | len) & 1U) != 0U) {
        return TIKU_RA8P1_XFLASH_ERR_RANGE;
    }
    return xflash_write_any((uint32_t)off, (const uint8_t *)src,
                            (uint32_t)len);
}

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
     * OCTAL FIRST, ALWAYS, AND THAT IS A CORRECTNESS REQUIREMENT RATHER THAN
     * A SPEED ONE.  DOPI moves bytes pair-swapped relative to single-bit SPI,
     * so content written through this backend in one protocol reads back as
     * nonsense in the other -- a stored object's header magic simply fails to
     * match and the slot reports itself empty.  Entering here makes the
     * protocol a property of the backend instead of a property of whatever
     * the caller happened to do to the bus earlier.
     */
    (void)tiku_ra8p1_xflash_opi_enter();     /* idempotent once entered */

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

    /* Self-checking: only 0xC2 is a Macronix part, so a floating bus or a
     * dead controller cannot pass by accident. */
    if (out[0] != RA8P1_MX_MANUFACTURER) {
        return TIKU_RA8P1_XFLASH_ERR_ID;
    }
    return TIKU_RA8P1_XFLASH_OK;
}
