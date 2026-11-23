/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_tikucomputer_msp.c - TikuComputer-nRF board controls (MSP430).
 *
 * interfaces/board/tiku_board_ctl.h on the board's control lines, which the
 * LINES block lists.  The state is read back from the pin registers, never
 * cached.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/board/tiku_board_ctl.h>
#include <interfaces/gpio/tiku_gpio_owner.h>
#include "tiku.h"

/*---------------------------------------------------------------------------*/
/* LINES                                                                     */
/*---------------------------------------------------------------------------*/

#define CTL_RAIL      BIT3              /* P3.3 SENS_EN */
#define CTL_PWR       BIT4              /* P3.4 nRF supply enable */
#define CTL_RST       BIT5              /* P3.5 nRF nRESET */
#define CTL_WAKE_IN   BIT6              /* P3.6 wake from the nRF */
#define CTL_WAKE_OUT  BIT7              /* P3.7 wake to the nRF */
#define CTL_SEL       BIT2              /* P4.2 MUX_SEL: 1 = nRF */
#define CTL_EN_N      BIT3              /* P4.3 MUX_EN_N: 1 = switches open */
#define CTL_LINK_TX   BIT5              /* P2.5 UCA1TXD to the nRF */
#define CTL_LINK_RX   BIT6              /* P2.6 UCA1RXD from the nRF */
#define CTL_LINK      (CTL_LINK_TX | CTL_LINK_RX)

/** @brief Length of the reset pulse, in microseconds. */
#define CTL_RESET_US  100u

/** @brief Owner name of the claimed control pins. */
static const char ctl_owner[] = "board";

/** @brief Port 2 settings of the link pins at the last coprocessor off. */
static struct {
    uint8_t sel0, sel1, dir, ren, out;
    uint8_t valid;
} link_saved;

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Claim the control pins for "board" once, so the gpio command
 *         leaves them alone. */
static void
ctl_claim(void)
{
    static const uint8_t pins[][2] = {
        {3u, 3u}, {3u, 4u}, {3u, 5u}, {3u, 6u}, {3u, 7u}, {4u, 2u}, {4u, 3u}
    };
    static uint8_t claimed;
    uint8_t i;

    if (claimed != 0u) {
        return;
    }
    claimed = 1u;
    for (i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        (void)tiku_gpio_claim(pins[i][0], pins[i][1], ctl_owner);
    }
}

/** @brief 1 while the sensor rail is driven on. */
static uint8_t
ctl_rail_on(void)
{
    return ((P3DIR & P3OUT & CTL_RAIL) != 0u) ? 1u : 0u;
}

/** @brief 1 while the nRF supply is driven on. */
static uint8_t
ctl_power_on(void)
{
    return ((P3DIR & P3OUT & CTL_PWR) != 0u) ? 1u : 0u;
}

/** @brief The bus owner the switch control lines select. */
static uint8_t
ctl_bus_now(void)
{
    if ((P4OUT & CTL_EN_N) != 0u) {
        return TIKU_BOARD_BUS_OFF;
    }
    return ((P4OUT & CTL_SEL) != 0u) ? TIKU_BOARD_BUS_COPROC
                                     : TIKU_BOARD_BUS_HOST;
}

/**
 * @brief Set the switch control lines for @p owner: open the switches, move
 *        the select line, then close them unless @p owner is OFF.
 */
static void
ctl_bus_set(uint8_t owner)
{
    P4DIR |= CTL_SEL | CTL_EN_N;
    P4OUT |= CTL_EN_N;
    if (owner == TIKU_BOARD_BUS_OFF) {
        return;
    }
    if (owner == TIKU_BOARD_BUS_COPROC) {
        P4OUT |= CTL_SEL;
    } else {
        P4OUT &= (uint8_t)~CTL_SEL;
    }
    P4OUT &= (uint8_t)~CTL_EN_N;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

int
tiku_board_ctl_rail(uint8_t on)
{
    ctl_claim();
    if (on != 0u) {
        /* Both chip selects idle high before the parts get power. */
        P4OUT |= BIT0;
        P4DIR |= BIT0;
        P5OUT |= BIT3;
        P5DIR |= BIT3;
        P3DIR |= CTL_RAIL;
        P3OUT |= CTL_RAIL;
        return TIKU_BOARD_CTL_OK;
    }
    /* The switch controls go low with the rail: the bus back to the MSP430,
     * and no level driven into the unpowered switches. */
    P4DIR |= CTL_SEL | CTL_EN_N;
    P4OUT &= (uint8_t)~CTL_SEL;
    P4OUT &= (uint8_t)~CTL_EN_N;
    P3DIR |= CTL_RAIL;
    P3OUT &= (uint8_t)~CTL_RAIL;
    return TIKU_BOARD_CTL_OK;
}

int
tiku_board_ctl_bus(uint8_t owner)
{
    ctl_claim();
    if (owner > TIKU_BOARD_BUS_COPROC) {
        return TIKU_BOARD_CTL_ERR_PARAM;
    }
    if (owner != TIKU_BOARD_BUS_HOST && ctl_rail_on() == 0u) {
        return TIKU_BOARD_CTL_ERR_ORDER;   /* switches unpowered */
    }
    if (owner == TIKU_BOARD_BUS_COPROC && ctl_power_on() == 0u) {
        return TIKU_BOARD_CTL_ERR_ORDER;
    }
    if (owner != ctl_bus_now()) {
        ctl_bus_set(owner);
    }
    return TIKU_BOARD_CTL_OK;
}

int
tiku_board_ctl_coproc_power(uint8_t on)
{
    ctl_claim();
    if (on != 0u) {
        if (ctl_power_on() != 0u) {
            return TIKU_BOARD_CTL_OK;
        }
        P3REN &= (uint8_t)~CTL_RST;
        P3DIR &= (uint8_t)~CTL_RST;          /* reset released */
        P3DIR |= CTL_PWR;
        P3OUT |= CTL_PWR;
        if (link_saved.valid != 0u) {
            P2OUT  = (uint8_t)((P2OUT  & ~CTL_LINK) | link_saved.out);
            P2REN  = (uint8_t)((P2REN  & ~CTL_LINK) | link_saved.ren);
            P2DIR  = (uint8_t)((P2DIR  & ~CTL_LINK) | link_saved.dir);
            P2SEL1 = (uint8_t)((P2SEL1 & ~CTL_LINK) | link_saved.sel1);
            P2SEL0 = (uint8_t)((P2SEL0 & ~CTL_LINK) | link_saved.sel0);
            link_saved.valid = 0u;
        }
        return TIKU_BOARD_CTL_OK;
    }
    if (ctl_bus_now() == TIKU_BOARD_BUS_COPROC) {
        ctl_bus_set(TIKU_BOARD_BUS_HOST);
    }
    if (ctl_power_on() != 0u && link_saved.valid == 0u) {
        link_saved.out  = (uint8_t)(P2OUT  & CTL_LINK);
        link_saved.ren  = (uint8_t)(P2REN  & CTL_LINK);
        link_saved.dir  = (uint8_t)(P2DIR  & CTL_LINK);
        link_saved.sel1 = (uint8_t)(P2SEL1 & CTL_LINK);
        link_saved.sel0 = (uint8_t)(P2SEL0 & CTL_LINK);
        link_saved.valid = 1u;
    }
    /* Link outputs low and the link input pulled down, as GPIO. */
    P2OUT  &= (uint8_t)~CTL_LINK;
    P2SEL0 &= (uint8_t)~CTL_LINK;
    P2SEL1 &= (uint8_t)~CTL_LINK;
    P2DIR  |= CTL_LINK_TX;
    P2DIR  &= (uint8_t)~CTL_LINK_RX;
    P2REN  |= CTL_LINK_RX;
    P3OUT  &= (uint8_t)~CTL_WAKE_OUT;
    P3DIR  |= CTL_WAKE_OUT;
    P3REN  &= (uint8_t)~CTL_RST;
    P3DIR  &= (uint8_t)~CTL_RST;
    P3DIR  |= CTL_PWR;
    P3OUT  &= (uint8_t)~CTL_PWR;
    return TIKU_BOARD_CTL_OK;
}

int
tiku_board_ctl_coproc_reset(void)
{
    ctl_claim();
    if (ctl_power_on() == 0u) {
        return TIKU_BOARD_CTL_ERR_ORDER;
    }
    if (ctl_bus_now() == TIKU_BOARD_BUS_COPROC) {
        ctl_bus_set(TIKU_BOARD_BUS_HOST);
    }
    P3OUT &= (uint8_t)~CTL_RST;
    P3DIR |= CTL_RST;
    tiku_cpu_msp430_delay_us(CTL_RESET_US);
    P3DIR &= (uint8_t)~CTL_RST;
    return TIKU_BOARD_CTL_OK;
}

int
tiku_board_ctl_wake(uint8_t level)
{
    ctl_claim();
    if (level != 0u && ctl_power_on() == 0u) {
        return TIKU_BOARD_CTL_ERR_ORDER;
    }
    P3DIR |= CTL_WAKE_OUT;
    if (level != 0u) {
        P3OUT |= CTL_WAKE_OUT;
    } else {
        P3OUT &= (uint8_t)~CTL_WAKE_OUT;
    }
    return TIKU_BOARD_CTL_OK;
}

void
tiku_board_ctl_state(tiku_board_ctl_state_t *s)
{
    if (s == (tiku_board_ctl_state_t *)0) {
        return;
    }
    s->rail         = ctl_rail_on();
    s->bus          = ctl_bus_now();
    s->coproc_power = ctl_power_on();
    s->coproc_reset = ((P3DIR & CTL_RST) != 0u && (P3OUT & CTL_RST) == 0u)
                      ? 1u : 0u;
    s->wake_out     = ((P3OUT & CTL_WAKE_OUT) != 0u) ? 1u : 0u;
    s->wake_in      = ((P3IN & CTL_WAKE_IN) != 0u) ? 1u : 0u;
}
