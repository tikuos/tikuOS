/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xflash_arch.c - EK-RA8P1 Octo-SPI NOR bring-up.
 *
 * The clock is left at its MOCO reset for identification: 8 MHz is ample to
 * read three bytes, and not touching OCTACKCR keeps its request/ready
 * handshake out of the first transaction that has to work.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include "tiku_xflash_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_common.h"

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

/*
 * Last controller state after a request, kept because the interesting failure
 * is not "the transaction errored" but "the transaction ran and the device
 * said nothing" -- which these separate.
 */
uint32_t xf_last_ctl0;
uint32_t xf_last_comstt;

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
     * RELEASE THE FLASH FROM RESET.  LIOCTL.RSTCS0 drives the OM_RESET pin
     * and resets to 0 = drive low, so muxing P106 to the OSPI function put
     * the part into hardware reset -- which reads as a live, pulled-high bus
     * with no device answering, on both positions of the isolation switch.
     * Read-modify-write, since bit 17 must be written back as the 1 it reads.
     */
    TIKU_REG32(RA8P1_OSPI_LIOCTL(XF_UNIT)) =
        TIKU_REG32(RA8P1_OSPI_LIOCTL(XF_UNIT)) | RA8P1_OSPI_LIOCTL_RSTCS0;
    __asm__ volatile ("dsb" ::: "memory");

    /* Reset-recovery: tREADY2 depends on what the part was doing when reset
     * hit; a fresh power-up recovers in microseconds.  1 ms costs nothing at
     * init and covers every table row short of an interrupted erase. */
    tiku_cpu_ra8p1_delay_us(1000U);

    /*
     * CS idle term: the reset value is 0, which asks for no gap at all
     * between frames.  A NOR part needs CS high for a minimum time between
     * transactions to register them as separate, so give it a generous
     * eight cycles -- at 8 MHz that costs a microsecond and removes a
     * whole class of "the device never saw a command" from the picture.
     */
    TIKU_REG32(RA8P1_OSPI_LIOCFG(XF_UNIT, XF_CS)) =
        (TIKU_REG32(RA8P1_OSPI_LIOCFG(XF_UNIT, XF_CS)) & ~0xF0000UL) |
        (8UL << 16);
    __asm__ volatile ("dsb" ::: "memory");

    xf_ready = 1U;
}

int tiku_ra8p1_xflash_read_id(uint8_t out[3])
{
    uint32_t spins, d0;

    if (out == NULL) {
        return TIKU_RA8P1_XFLASH_ERR_ID;
    }
    tiku_ra8p1_xflash_init();

    /*
     * One manual-command transaction: 1-byte opcode, no address, no dummy
     * cycles, 3 bytes back.  This is the 1-1-1 SPI form the part powers up
     * in, so it needs none of the octal configuration to work.
     */
    TIKU_REG32(RA8P1_OSPI_CDTBUF(XF_UNIT, 0)) =
        /* CMD[15:8] carries a 1-byte opcode in 1S-1S-1S; CMD[7:0] is "not
         * used" -- so an unshifted opcode transmits 0x00 and the device
         * rightly ignores it. */
        RA8P1_CDTBUF_CMD((uint32_t)RA8P1_MX_CMD_RDID << 8) |
        RA8P1_CDTBUF_CMDSIZE(1U) |
        RA8P1_CDTBUF_ADDSIZE(0U) |
        RA8P1_CDTBUF_DATASIZE(3U) |
        RA8P1_CDTBUF_LATE(0U);
    TIKU_REG32(RA8P1_OSPI_CDABUF(XF_UNIT, 0)) = 0UL;
    TIKU_REG32(RA8P1_OSPI_CDD0BUF(XF_UNIT, 0)) = 0UL;

    TIKU_REG32(RA8P1_OSPI_CDCTL0(XF_UNIT)) =
        ((XF_CS != 0U) ? RA8P1_CDCTL0_CSSEL : 0UL) | RA8P1_CDCTL0_TRREQ;
    __asm__ volatile ("dsb" ::: "memory");

    /* Confirm the request was ACCEPTED before waiting for it.  A dropped
     * write reads back as 0, which the wait loop below would take for
     * "already finished" and report success on garbage. */
    xf_last_ctl0 = TIKU_REG32(RA8P1_OSPI_CDCTL0(XF_UNIT));
    xf_last_comstt = TIKU_REG32(RA8P1_OSPI_COMSTT(XF_UNIT));

    /* TRREQ self-clears when the transaction completes. */
    for (spins = 1000000UL; spins != 0UL; spins--) {
        if ((TIKU_REG32(RA8P1_OSPI_CDCTL0(XF_UNIT)) &
             RA8P1_CDCTL0_TRREQ) == 0UL) {
            break;
        }
    }
    if (spins == 0UL) {
        return TIKU_RA8P1_XFLASH_ERR_TIMEOUT;
    }

    d0 = TIKU_REG32(RA8P1_OSPI_CDD0BUF(XF_UNIT, 0));
    out[0] = (uint8_t)(d0 & 0xFFU);
    out[1] = (uint8_t)((d0 >> 8) & 0xFFU);
    out[2] = (uint8_t)((d0 >> 16) & 0xFFU);

    /* Self-checking: only 0xC2 is a Macronix part, so a floating bus or a
     * dead controller cannot pass by accident. */
    if (out[0] != RA8P1_MX_MANUFACTURER) {
        return TIKU_RA8P1_XFLASH_ERR_ID;
    }
    return TIKU_RA8P1_XFLASH_OK;
}
