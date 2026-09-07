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

/*---------------------------------------------------------------------------*/
/* DEVICE MODE (tiku_usbhs_dev.c)                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure device mode on a core already brought up, arm EP0 and
 *        present the pull-up so the host begins enumeration.
 * @return 0, or -1 if the core is not idle (bring it up first).
 */
int tiku_nordic_usbhs_dev_start(void);

/**
 * @brief As tiku_nordic_usbhs_dev_start(), choosing the PHY's data width
 *        and turnaround time: @p phyif16 below zero keeps the reset value,
 *        @p trdtim above 15 keeps it.
 */
int tiku_nordic_usbhs_dev_start_cfg(int phyif16, uint32_t trdtim,
                                    uint32_t devspd);

/** @brief Remove the pull-up and mask the core's interrupts. */
void tiku_nordic_usbhs_dev_stop(void);

/** @brief Service the core's interrupt in device mode. */
void tiku_nordic_usbhs_dev_irq(void);

/** @brief Whether device mode is running. */
uint8_t tiku_nordic_usbhs_dev_started(void);

/*---------------------------------------------------------------------------*/
/* CDC-ACM DATA PATH                                                         */
/*---------------------------------------------------------------------------*/

/** @brief Bytes the host wrote, delivered from the interrupt. */
typedef void (*tiku_nordic_usbhs_cdc_rx_fn)(const uint8_t *data, uint32_t len);
/** @brief The last packet given to _cdc_send() has left; from the interrupt. */
typedef void (*tiku_nordic_usbhs_cdc_done_fn)(void);

/** @brief Register the class layer's callbacks; either may be NULL. */
void tiku_nordic_usbhs_dev_cdc_bind(tiku_nordic_usbhs_cdc_rx_fn on_rx,
                                    tiku_nordic_usbhs_cdc_done_fn on_tx_done);

/** @brief Configured by the host: the bulk endpoints are live. */
uint8_t tiku_nordic_usbhs_dev_cdc_configured(void);

/** @brief Configured by the host AND a terminal holds DTR: the port is open. */
uint8_t tiku_nordic_usbhs_dev_cdc_open(void);

/**
 * @brief Send one packet (at most 64 bytes) on the bulk IN endpoint.
 * @return 0 when taken, -1 while the previous one is still on the wire or
 *         the device is not configured.  Completion arrives on_tx_done.
 */
int tiku_nordic_usbhs_dev_cdc_send(const uint8_t *data, uint32_t len);

/** @brief Whether a bulk IN packet is still on the wire. */
uint8_t tiku_nordic_usbhs_dev_cdc_sending(void);

/** @brief What enumeration has reached: the counts, the speed the host
 *         settled on, the assigned address and the chosen configuration. */
void tiku_nordic_usbhs_dev_stats(uint32_t *setup, uint32_t *reset,
                                 uint32_t *enum_done, uint32_t *speed,
                                 uint8_t *address, uint8_t *configured);

/** @brief The last control request seen and what the IN endpoint did with
 *         its answer: for telling a request that never arrived from one
 *         whose data never left. */
void tiku_nordic_usbhs_dev_trace(uint8_t *setup8, uint32_t *tx,
                                 uint32_t *in_done, uint32_t *out_done,
                                 uint32_t *tsiz, uint32_t *ctl, uint32_t *iint);

/** @brief Where the control buffer is, what it held when a transfer was
 *         armed, and whether the core's counter drained: for telling bad
 *         data from data the core never fetched. */
void tiku_nordic_usbhs_dev_dma(uint32_t *addr, uint32_t *tsiz_after,
                               uint32_t *armed_len, uint8_t *armed8);

/** @brief One entry of the control-request log, oldest at index 0; the
 *         answer is the byte count sent, or 0xFFFF for a stall. */
void tiku_nordic_usbhs_dev_log(uint8_t index, uint8_t *req8, uint16_t *ans);

#endif /* TIKU_USBHS_ARCH_H_ */
