/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbhs_arch.c - nRF54LM20 USB high-speed device: bring-up and report.
 *
 * Powers VREGUSB, the USB PHY and the DWC2 core, and reports the core's own
 * configuration registers.  Every wait here is bounded; a core that never
 * answers returns -1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_usbhs_arch.h"

#include <arch/nordic/tiku_device_select.h>
#include <arch/nordic/tiku_nordic_core.h>

/*---------------------------------------------------------------------------*/
/* CONFIG                                                                    */
/*---------------------------------------------------------------------------*/

/* Bounded spins.  The core's soft reset is specified in core clocks and
 * normally ends within a few thousand register reads; the bounds only limit
 * how long a dead core holds the caller. */
#define USBHS_RESET_SPINS   2000000u
#define USBHS_HFXO_SPINS    200000u
#define USBHS_SETTLE_SPINS  100000u

/* The vendored MDK carries the register maps but not the interrupt enum, so
 * the two this block needs are named here (upstream nrf54lm20a_application.h:
 * USBHS 90, VREGUSB 289). */
#define USBHS_IRQN          90
#define VREGUSB_IRQN        289

/* VREGUSB interrupt sources (VREGUSB_INTEN_*_Pos): the detect bit is 1,
 * not 0.  Bit 0 is another source whose event this driver never clears, so
 * enabling it storms the interrupt. */
#define VREGUSB_INT_VBUSDETECTED (1u << 1)
#define VREGUSB_INT_VBUSREMOVED  (1u << 4)

/* Wrapper ENABLE bits (USBHS_ENABLE_{CORE,PHY}_Pos). */
#define USBHS_EN_CORE       (1u << 0)
#define USBHS_EN_PHY        (1u << 1)

/* DWC2 GRSTCTL: core soft reset, and the AHB-idle flag that ends it. */
#define DWC2_GRSTCTL_CSFTRST (1u << 0)
#define DWC2_GRSTCTL_CSFTRSTDONE (1ul << 29)
#define DWC2_GRSTCTL_AHBIDLE (1ul << 31)

/* DWC2 GAHBCFG: the global interrupt mask to the application. */
#define DWC2_GAHBCFG_GLBLINTRMSK (1u << 0)

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

static uint8_t  s_core_up;      /* the core answered a soft reset          */
static uint8_t  s_vbus;         /* VBUS seen and not since removed         */
static uint32_t s_core_irqs;
static uint32_t s_vbus_irqs;
static uint32_t s_gintsts_seen; /* OR of the status at every interrupt     */

/*---------------------------------------------------------------------------*/
/* INTERRUPTS                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief USBHS_IRQn, the DWC2 core's interrupt (the wrapper has none).
 *
 * Records GINTSTS.  In device mode tiku_nordic_usbhs_dev_irq() services it;
 * otherwise the status is cleared and the core's mask zeroed, so an
 * unserviced source cannot keep re-entering.
 */
void tiku_nordic_usbhs_isr(void)
{
    uint32_t sts = NRF_USBHSCORE_S->GINTSTS;

    s_core_irqs++;
    s_gintsts_seen |= sts;
    if (tiku_nordic_usbhs_dev_started() != 0u) {
        tiku_nordic_usbhs_dev_irq();
        return;
    }
    NRF_USBHSCORE_S->GINTSTS = sts;          /* write-1-to-clear          */
    NRF_USBHSCORE_S->GINTMSK = 0u;           /* no device mode: mask all  */
}

/** @brief VBUS appeared or went; the flag is what the probe reports. */
void tiku_nordic_vregusb_isr(void)
{
    uint8_t served = 0u;

    s_vbus_irqs++;
    if (NRF_VREGUSB_S->EVENTS_VBUSDETECTED != 0u) {
        NRF_VREGUSB_S->EVENTS_VBUSDETECTED = 0u;
        s_vbus = 1u;
        served = 1u;
    }
    if (NRF_VREGUSB_S->EVENTS_VBUSREMOVED != 0u) {
        NRF_VREGUSB_S->EVENTS_VBUSREMOVED = 0u;
        s_vbus = 0u;
        served = 1u;
    }
    /* An interrupt with no event this driver clears re-enters for ever, so
     * the block's interrupts are disarmed. */
    if (served == 0u) {
        NRF_VREGUSB_S->INTENCLR = 0xFFFFFFFFul;
    }
}

/*---------------------------------------------------------------------------*/
/* VBUS REGULATOR                                                            */
/*---------------------------------------------------------------------------*/

int tiku_nordic_usbhs_vbus_start(void)
{
    NRF_VREGUSB_S->EVENTS_VBUSDETECTED = 0u;
    NRF_VREGUSB_S->EVENTS_VBUSREMOVED  = 0u;
    NRF_VREGUSB_S->INTENSET = VREGUSB_INT_VBUSDETECTED |
                              VREGUSB_INT_VBUSREMOVED;
    tiku_nordic_nvic_enable(VREGUSB_IRQN);
    NRF_VREGUSB_S->TASKS_START = 1u;
    return 0;
}

void tiku_nordic_usbhs_vbus_stop(void)
{
    NRF_VREGUSB_S->TASKS_STOP = 1u;
    NRF_VREGUSB_S->INTENCLR = 0xFFFFFFFFul;
    tiku_nordic_nvic_disable(VREGUSB_IRQN);
    s_vbus = 0u;
}

int tiku_nordic_usbhs_vbus_present(void)
{
    return (int)s_vbus;
}

/*---------------------------------------------------------------------------*/
/* BRING-UP                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Start the HFXO, the PLL and PCLK24M, each wait bounded.
 *
 * The PHY's reference is the 24 MHz select in PHY.CLOCK, not the 32 MHz
 * crystal directly, so the derived clocks are started too.
 */
static void usbhs_clocks_start(void)
{
    uint32_t spin;

    NRF_CLOCK_S->EVENTS_XOSTARTED = 0u;
    NRF_CLOCK_S->TASKS_XOSTART = 1u;
    for (spin = 0u; spin < USBHS_HFXO_SPINS; spin++) {
        if (NRF_CLOCK_S->EVENTS_XOSTARTED != 0u) {
            break;
        }
    }
    NRF_CLOCK_S->EVENTS_PLLSTARTED = 0u;
    NRF_CLOCK_S->TASKS_PLLSTART = 1u;
    for (spin = 0u; spin < USBHS_HFXO_SPINS; spin++) {
        if (NRF_CLOCK_S->EVENTS_PLLSTARTED != 0u) {
            break;
        }
    }
    /* PCLK24M: the crystal clock the USB PHY's reference select names, and
     * what the DWC2 core is clocked from.  Without this request the core's
     * register window has no clock and every read of it stalls the bus. */
    NRF_CLOCK_S->TASKS_XO24MSTART = 1u;
    for (spin = 0u; spin < USBHS_HFXO_SPINS; spin++) {
        if (NRF_CLOCK_S->PLL24M.RUN != 0u) {
            break;
        }
    }
}

/** @brief Fixed settle delay: the PHY and its PLL publish no readiness. */
static void usbhs_settle(void)
{
    volatile uint32_t spin;

    for (spin = 0u; spin < USBHS_SETTLE_SPINS; spin++) {
    }
}

int tiku_nordic_usbhs_up(void (*note)(const char *stage))
{
    uint32_t spin;

    if (note != (void (*)(const char *))0) { note("clocks"); }
    usbhs_clocks_start();

    /* Both enables in one write, then start: the vendor HAL's own order.
     * The core is clocked from the PHY, and a core register read before
     * that clock runs stalls the bus rather than faulting. */
    if (note != (void (*)(const char *))0) { note("enable"); }
    NRF_USBHS_S->ENABLE = USBHS_EN_PHY | USBHS_EN_CORE;
    usbhs_settle();

    if (note != (void (*)(const char *))0) { note("start"); }
    NRF_USBHS_S->TASKS_START = 1u;
    usbhs_settle();
    usbhs_settle();

    /* The enable resets the PHY block, so its clock settings are written
     * after it: clearing COMMONONN keeps the reference logic, bias and PLL
     * powered in suspend. */
    if (note != (void (*)(const char *))0) { note("phy-clock"); }
    NRF_USBHS_S->PHY.CLOCK &= ~(1u << 4);
    usbhs_settle();

    /* The first core read of the session; it needs the clocks, enables
     * and settles above. */
    if (note != (void (*)(const char *))0) { note("core-read"); }
    (void)NRF_USBHSCORE_S->GSNPSID;
    s_core_up = 1u;                 /* it answered: the dump is readable */

    if (note != (void (*)(const char *))0) { note("ahb-idle"); }
    for (spin = 0u; spin < USBHS_RESET_SPINS; spin++) {
        if ((NRF_USBHSCORE_S->GRSTCTL & DWC2_GRSTCTL_AHBIDLE) != 0u) {
            break;
        }
    }
    if (spin >= USBHS_RESET_SPINS) {
        return -1;
    }

    if (note != (void (*)(const char *))0) { note("soft-reset"); }
    NRF_USBHSCORE_S->GRSTCTL = DWC2_GRSTCTL_CSFTRST;
    for (spin = 0u; spin < USBHS_RESET_SPINS; spin++) {
        uint32_t rst = NRF_USBHSCORE_S->GRSTCTL;

        /* This core revision reports completion in its own bit and leaves
         * the request bit standing; waiting for that to clear never ends. */
        if ((rst & DWC2_GRSTCTL_CSFTRSTDONE) != 0u ||
            (rst & DWC2_GRSTCTL_CSFTRST) == 0u) {
            break;
        }
    }
    /* The done flag is write-one-to-clear and the request bit stands until
     * it is taken down; a core left asserted reads every identification
     * register as zero. */
    NRF_USBHSCORE_S->GRSTCTL = DWC2_GRSTCTL_CSFTRSTDONE;
    usbhs_settle();
    if (spin >= USBHS_RESET_SPINS) {
        return -1;
    }
    for (spin = 0u; spin < USBHS_RESET_SPINS; spin++) {
        if ((NRF_USBHSCORE_S->GRSTCTL & DWC2_GRSTCTL_AHBIDLE) != 0u) {
            break;
        }
    }
    if (spin >= USBHS_RESET_SPINS) {
        return -1;
    }

    /* Interrupts reach the application only through this bit; the mask is
     * still empty, so the line stays quiet until a driver arms it. */
    if (note != (void (*)(const char *))0) { note("arm"); }
    NRF_USBHSCORE_S->GINTSTS = 0xFFFFFFFFul;
    NRF_USBHSCORE_S->GINTMSK = 0u;
    NRF_USBHSCORE_S->GAHBCFG |= DWC2_GAHBCFG_GLBLINTRMSK;
    tiku_nordic_nvic_enable(USBHS_IRQN);

    s_core_up = 1u;
    return 0;
}

void tiku_nordic_usbhs_live(uint32_t settles)
{
    uint32_t i;

    usbhs_clocks_start();
    NRF_USBHS_S->ENABLE = USBHS_EN_PHY | USBHS_EN_CORE;
    NRF_USBHS_S->TASKS_START = 1u;
    for (i = 0u; i < settles; i++) {
        usbhs_settle();
    }
}

int tiku_nordic_usbhs_try(uint32_t enable, int start_first, uint32_t fsel,
                          void (*note)(const char *stage))
{
    uint32_t spin;

    if (note != (void (*)(const char *))0) { note("clocks"); }
    usbhs_clocks_start();

    if (fsel <= 7u) {
        if (note != (void (*)(const char *))0) { note("fsel"); }
        NRF_USBHS_S->PHY.CLOCK = (NRF_USBHS_S->PHY.CLOCK & ~7u) | fsel;
        usbhs_settle();
    }
    if (start_first != 0) {
        if (note != (void (*)(const char *))0) { note("start"); }
        NRF_USBHS_S->TASKS_START = 1u;
        usbhs_settle();
    }
    if (note != (void (*)(const char *))0) { note("enable"); }
    NRF_USBHS_S->ENABLE = enable;
    usbhs_settle();
    if (start_first == 0) {
        if (note != (void (*)(const char *))0) { note("start"); }
        NRF_USBHS_S->TASKS_START = 1u;
        usbhs_settle();
    }
    usbhs_settle();

    if (note != (void (*)(const char *))0) { note("core-read"); }
    (void)NRF_USBHSCORE_S->GSNPSID;

    if (note != (void (*)(const char *))0) { note("ahb-idle"); }
    for (spin = 0u; spin < USBHS_RESET_SPINS; spin++) {
        if ((NRF_USBHSCORE_S->GRSTCTL & DWC2_GRSTCTL_AHBIDLE) != 0u) {
            break;
        }
    }
    s_core_up = 1u;
    return (spin < USBHS_RESET_SPINS) ? 0 : -1;
}

void tiku_nordic_usbhs_down(void)
{
    tiku_nordic_nvic_disable(USBHS_IRQN);
    NRF_USBHS_S->TASKS_STOP = 1u;
    NRF_USBHS_S->ENABLE = 0u;
    s_core_up = 0u;
}

/*---------------------------------------------------------------------------*/
/* REPORTING                                                                 */
/*---------------------------------------------------------------------------*/

void tiku_nordic_usbhs_read(tiku_nordic_usbhs_regs_t *out)
{
    if (out == (tiku_nordic_usbhs_regs_t *)0) {
        return;
    }
    out->enable     = NRF_USBHS_S->ENABLE;
    out->phy_clock  = NRF_USBHS_S->PHY.CLOCK;
    out->phy_config = NRF_USBHS_S->PHY.CONFIG;
    out->phy_status = NRF_USBHS_S->PHY.BATTCHRGSTATUS;
    out->core_up    = s_core_up;
    out->xo_run     = NRF_CLOCK_S->XO.RUN;
    out->pll_run    = NRF_CLOCK_S->PLL.RUN;
    out->pclk24m    = NRF_CLOCK_S->PLL24M.RUN;

    /* The core's registers answer only once it is powered and out of
     * reset; a read of a dark core stalls the bus. */
    out->snpsid = out->hwcfg1 = out->hwcfg2 = out->hwcfg3 = out->hwcfg4 = 0u;
    out->gintsts = out->grstctl = out->gahbcfg = out->gusbcfg = 0u;
    out->dcfg = out->dsts = out->dctl = 0u;
    if (s_core_up == 0u) {
        return;
    }
    out->snpsid  = NRF_USBHSCORE_S->GSNPSID;
    out->hwcfg1  = NRF_USBHSCORE_S->GHWCFG1;
    out->hwcfg2  = NRF_USBHSCORE_S->GHWCFG2;
    out->hwcfg3  = NRF_USBHSCORE_S->GHWCFG3;
    out->hwcfg4  = NRF_USBHSCORE_S->GHWCFG4;
    out->gintsts = NRF_USBHSCORE_S->GINTSTS;
    out->grstctl = NRF_USBHSCORE_S->GRSTCTL;
    out->gahbcfg = NRF_USBHSCORE_S->GAHBCFG;
    out->gusbcfg = NRF_USBHSCORE_S->GUSBCFG;
    out->dcfg    = NRF_USBHSCORE_S->DCFG;
    out->dsts    = NRF_USBHSCORE_S->DSTS;
    out->dctl    = NRF_USBHSCORE_S->DCTL;
}

void tiku_nordic_usbhs_counts(uint32_t *core_irqs, uint32_t *vbus_irqs,
                              uint32_t *gintsts_seen)
{
    if (core_irqs != (uint32_t *)0) {
        *core_irqs = s_core_irqs;
    }
    if (vbus_irqs != (uint32_t *)0) {
        *vbus_irqs = s_vbus_irqs;
    }
    if (gintsts_seen != (uint32_t *)0) {
        *gintsts_seen = s_gintsts_seen;
    }
}
