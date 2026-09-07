/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbhs_arch.h - nRF54LM20 USB high-speed device: bring-up and recon.
 *
 * A Synopsys DWC2 core behind a Nordic wrapper with its own VBUS regulator.
 * This layer powers the three and reports what the core says about itself.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_USBHS_ARCH_H_
#define TIKU_USBHS_ARCH_H_

#include <stdint.h>

/** @brief A snapshot of the wrapper, the regulator and the DWC2 core. */
typedef struct {
    uint32_t enable;        /**< wrapper ENABLE (CORE, PHY bits)       */
    uint32_t phy_clock;     /**< PHY.CLOCK: FSEL, PLLBTUNE, COMMONONN  */
    uint32_t phy_config;    /**< PHY.CONFIG: the UTMI tuning fields    */
    uint32_t core_up;       /**< non-zero once the core answered       */
    uint32_t phy_status;    /**< PHY.BATTCHRGSTATUS: the D+/D- line state,
                             *   which only a powered PHY reports         */
    uint32_t xo_run;        /**< HFXO running                          */
    uint32_t pll_run;       /**< the system PLL running                */
    uint32_t pclk24m;       /**< PCLK24M requested: the core's clock   */
    uint32_t snpsid;        /**< GSNPSID: the Synopsys core release    */
    uint32_t hwcfg1;        /**< GHWCFG1..4: what this core was built  */
    uint32_t hwcfg2;        /**<   with -- DMA architecture, endpoint  */
    uint32_t hwcfg3;        /**<   count, FIFO depth                   */
    uint32_t hwcfg4;
    uint32_t gintsts;       /**< live interrupt status                 */
    uint32_t grstctl;
    uint32_t gahbcfg;
    uint32_t gusbcfg;
    uint32_t dcfg;
    uint32_t dsts;          /**< DSTS: enumerated speed, suspend       */
    uint32_t dctl;
} tiku_nordic_usbhs_regs_t;

/**
 * @brief Start VREGUSB so VBUS can be detected, and arm its interrupt.
 * @return 0 always; the regulator has no readiness to wait on here.
 */
int tiku_nordic_usbhs_vbus_start(void);

/** @brief Stop VREGUSB and disarm its interrupt. */
void tiku_nordic_usbhs_vbus_stop(void);

/**
 * @brief Whether a cable has been seen since the regulator started.
 * @return 1 while VBUS is present, 0 once it has been removed.
 */
int tiku_nordic_usbhs_vbus_present(void);

/**
 * @brief Power the PHY and the core, start the block and soft-reset the
 *        core, leaving it idle with its interrupt armed.
 *
 * @param note  called with each stage's name BEFORE that stage acts, or
 *              NULL.  A core register read against an unclocked core stalls
 *              the bus with no fault to catch, so the last name reported is
 *              the step that did not return.
 * @return 0 on success, -1 if the core never left reset or went idle.
 */
int tiku_nordic_usbhs_up(void (*note)(const char *stage));

/**
 * @brief Enable and start the block, wait @p settles times, and touch no
 *        core register: this cannot stall, so it is the safe way to ask
 *        whether the wrapper and PHY came up at all.
 */
void tiku_nordic_usbhs_live(uint32_t settles);

/**
 * @brief One bring-up variant, for bisecting what actually clocks the core.
 *
 * @param enable      value written to the wrapper ENABLE (1 core, 2 PHY).
 * @param start_first non-zero to trigger START before ENABLE, not after.
 * @param fsel        0..7 written into PHY.CLOCK's reference select, or a
 *                    value above 7 to leave the field alone.
 * @param note        stage reporter, as tiku_nordic_usbhs_up().
 * @return 0 if the core answered a read, -1 if it never went idle.
 */
int tiku_nordic_usbhs_try(uint32_t enable, int start_first, uint32_t fsel,
                          void (*note)(const char *stage));

/** @brief Disable the core and the PHY; the regulator is left alone. */
void tiku_nordic_usbhs_down(void);

/**
 * @brief Fill @p out.  The wrapper and the regulator always read; the core
 *        registers read as zero until a successful up().
 */
void tiku_nordic_usbhs_read(tiku_nordic_usbhs_regs_t *out);

/**
 * @brief Interrupt tallies since boot: the core's, the regulator's, and
 *        the OR of every GINTSTS bit an interrupt has been taken for.
 */
void tiku_nordic_usbhs_counts(uint32_t *core_irqs, uint32_t *vbus_irqs,
                              uint32_t *gintsts_seen);

#endif /* TIKU_USBHS_ARCH_H_ */
