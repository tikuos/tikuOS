/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbhs_arch.c - ESP32-P4 USB 2.0 OTG core bring-up and register report.
 *
 * Clocks, resets and PHY controls follow ESP-IDF 4d59230 usb_utmi_ll.h,
 * usb_utmi_hal.c and usb_dwc_ll.h (hw_ver1); the core's soft reset follows
 * the sequence for releases from 4.20a.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_usbhs_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"

#define USB_DWC           0x50000000u
#define GOTGCTL           (USB_DWC + 0x000u)
#define GAHBCFG           (USB_DWC + 0x008u)
#define GUSBCFG           (USB_DWC + 0x00Cu)
#define GRSTCTL           (USB_DWC + 0x010u)
#define GINTSTS           (USB_DWC + 0x014u)
#define GINTMSK           (USB_DWC + 0x018u)
#define GSNPSID           (USB_DWC + 0x040u)
#define GHWCFG2           (USB_DWC + 0x048u)
#define GHWCFG3           (USB_DWC + 0x04Cu)
#define GHWCFG4           (USB_DWC + 0x050u)
#define DSTS              (USB_DWC + 0x808u)
#define DCTL              (USB_DWC + 0x804u)
#define GUSBCFG_TOUTCAL   7u
#define GUSBCFG_PHYIF16   (1u << 3)
#define GUSBCFG_ULPI_UTMI (1u << 4)
#define GUSBCFG_PHYSEL    (1u << 6)
#define GRSTCTL_CSFTRST   (1u << 0)
#define GRSTCTL_DONE      (1u << 29)
#define GRSTCTL_AHBIDLE   (1u << 31)
#define GSNPSID_4_20A     0x4F54420Au
#define DCTL_SFTDISCON    (1u << 1)
/* HP_SYS_CLKRST, LP_CLKRST and HP_SYSTEM: the core's system clock, the
 * PHY's reference clock, their resets, and the PHY's suspend input. */
#define CLK_SYS           0x500E6018u   /* SOC_CLK_CTRL1, bit 16 */
#define CLK_SYS_USB       (1u << 16)
#define USB_CLKRST        0x50111048u   /* HP_USB_CLKRST_CTRL1 */
#define RST_PHY           (1u << 1)
#define RST_CORE          (1u << 2)
#define PHYREF_CLK        (1u << 30)
#define OTG_CTRL          0x500E515Cu   /* HP_SYSTEM USBOTG20_CTRL */
#define OTG_SUSPENDM      (1u << 21)
#define UTMI_FC06         0x5009C018u   /* USB_UTMI fc_06 */
#define UTMI_LS_PAR       (1u << 0)
#define UTMI_LS_KPALV     (1u << 3)
#define USB_IRQ_LINE      6u
#define USB_IRQ_SOURCE    93u           /* ETS_USB_OTG_INTR_SOURCE */
#define USB_SPINS         2000000u

static uint8_t core_up;

/** @brief Set @p bits of register @p reg to their values in @p value. */
static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

/** @brief Wait until @p bits of @p reg read as @p value; 0, or -1 at the
 *         end of USB_SPINS polls. */
static int wait_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    uint32_t n;

    for (n = 0; n < USB_SPINS; n++) {
        if ((TIKU_P4_REG_READ(reg) & bits) == value) {
            return 0;
        }
    }
    return -1;
}

/** @brief The core's interrupt: device mode answers it. */
static void usb_isr(void)
{
    tiku_p4_usbhs_msc_irq();
}

int tiku_p4_usbhs_up(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE(), id;

    if (!tiku_p4_irq_owned(USB_IRQ_LINE, USB_IRQ_SOURCE, usb_isr) &&
        tiku_p4_irq_attach(USB_IRQ_LINE, USB_IRQ_SOURCE, 1u, usb_isr) != 0) {
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_P4_USBHS_ERR_IRQ;
    }
    put_bits(CLK_SYS, CLK_SYS_USB, CLK_SYS_USB);
    put_bits(USB_CLKRST, PHYREF_CLK, PHYREF_CLK);
    put_bits(USB_CLKRST, RST_CORE | RST_PHY, RST_CORE | RST_PHY);
    put_bits(USB_CLKRST, RST_PHY, 0u);
    put_bits(USB_CLKRST, RST_CORE, 0u);
    /* The PHY's suspend input is not tied high on this silicon (IDF-9953). */
    put_bits(OTG_CTRL, OTG_SUSPENDM, OTG_SUSPENDM);
    put_bits(UTMI_FC06, UTMI_LS_PAR | UTMI_LS_KPALV, UTMI_LS_PAR | UTMI_LS_KPALV);
    TIKU_P4_IRQ_RESTORE(state);

    /* The HS PHY on a 16-bit UTMI+ interface, 5 PHY clocks of timeout. */
    TIKU_P4_REG_WRITE(GUSBCFG, (TIKU_P4_REG_READ(GUSBCFG) &
                                ~(GUSBCFG_TOUTCAL | GUSBCFG_ULPI_UTMI |
                                  GUSBCFG_PHYSEL)) |
                                   GUSBCFG_PHYIF16 | 5u);
    if (wait_bits(GRSTCTL, GRSTCTL_AHBIDLE, GRSTCTL_AHBIDLE) != 0) {
        return TIKU_P4_USBHS_ERR_IDLE;
    }
    id = TIKU_P4_REG_READ(GSNPSID);
    TIKU_P4_REG_WRITE(GRSTCTL, GRSTCTL_CSFTRST);
    if (id < GSNPSID_4_20A) {
        if (wait_bits(GRSTCTL, GRSTCTL_CSFTRST, 0u) != 0) {
            return TIKU_P4_USBHS_ERR_RESET;
        }
    } else {
        if (wait_bits(GRSTCTL, GRSTCTL_DONE, GRSTCTL_DONE) != 0) {
            return TIKU_P4_USBHS_ERR_RESET;
        }
        TIKU_P4_REG_WRITE(GRSTCTL, (TIKU_P4_REG_READ(GRSTCTL) &
                                    ~GRSTCTL_CSFTRST) | GRSTCTL_DONE);
    }
    if (wait_bits(GRSTCTL, GRSTCTL_AHBIDLE, GRSTCTL_AHBIDLE) != 0) {
        return TIKU_P4_USBHS_ERR_IDLE;
    }
    core_up = 1u;
    tiku_p4_irq_enable(USB_IRQ_LINE, 1);
    return TIKU_P4_USBHS_OK;
}

void tiku_p4_usbhs_down(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();

    if (core_up) {
        put_bits(DCTL, DCTL_SFTDISCON, DCTL_SFTDISCON);
        TIKU_P4_REG_WRITE(GINTMSK, 0u);
        TIKU_P4_REG_WRITE(GAHBCFG, 0u);
        core_up = 0u;
    }
    if (tiku_p4_irq_owned(USB_IRQ_LINE, USB_IRQ_SOURCE, usb_isr)) {
        tiku_p4_irq_detach(USB_IRQ_LINE);
    }
    put_bits(USB_CLKRST, RST_CORE | RST_PHY, RST_CORE | RST_PHY);
    TIKU_P4_IRQ_RESTORE(state);
}

void tiku_p4_usbhs_read(tiku_p4_usbhs_regs_t *out)
{
    uint32_t rst = TIKU_P4_REG_READ(USB_CLKRST);

    if (out == NULL) {
        return;
    }
    out->clocks = ((TIKU_P4_REG_READ(CLK_SYS) & CLK_SYS_USB) ? 1u : 0u) |
                  ((rst & PHYREF_CLK) ? 2u : 0u);
    out->resets = (rst >> 1) & 3u;
    out->otg_ctrl = TIKU_P4_REG_READ(OTG_CTRL);
    if (!core_up) {
        out->snpsid = out->hwcfg2 = out->hwcfg3 = out->hwcfg4 = 0u;
        out->gotgctl = out->gusbcfg = out->grstctl = out->gintsts = 0u;
        out->dsts = out->dctl = 0u;
        return;
    }
    out->snpsid = TIKU_P4_REG_READ(GSNPSID);
    out->hwcfg2 = TIKU_P4_REG_READ(GHWCFG2);
    out->hwcfg3 = TIKU_P4_REG_READ(GHWCFG3);
    out->hwcfg4 = TIKU_P4_REG_READ(GHWCFG4);
    out->gotgctl = TIKU_P4_REG_READ(GOTGCTL);
    out->gusbcfg = TIKU_P4_REG_READ(GUSBCFG);
    out->grstctl = TIKU_P4_REG_READ(GRSTCTL);
    out->gintsts = TIKU_P4_REG_READ(GINTSTS);
    out->dsts = TIKU_P4_REG_READ(DSTS);
    out->dctl = TIKU_P4_REG_READ(DCTL);
}
