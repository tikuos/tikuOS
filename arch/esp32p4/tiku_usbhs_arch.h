/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbhs_arch.h - ESP32-P4 USB 2.0 high-speed device on the OTG port.
 *
 * The DWC2 core on the UTMI PHY: bring-up and register report
 * (tiku_usbhs_arch.c), vendor bulk records and a mass-storage RAM disk.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_USBHS_ARCH_H_
#define TIKU_ESP32P4_USBHS_ARCH_H_
#include <stdint.h>
struct tiku_process;

#define TIKU_P4_USBHS_OK          0
#define TIKU_P4_USBHS_ERR_RESET  -1   /**< the core never finished its reset */
#define TIKU_P4_USBHS_ERR_IDLE   -2   /**< the core's AHB master never idled */
#define TIKU_P4_USBHS_ERR_IRQ    -3   /**< interrupt line 6 is taken         */
#define TIKU_P4_USBHS_ERR_FIFO   -4   /**< the FIFO layout exceeds the core's */
#define TIKU_P4_USBHS_ERR_MEM    -5   /**< no SRAM for the disk              */
#define TIKU_P4_USBHS_ERR_BUSY   -6   /**< another service owns the device   */
#define TIKU_P4_USBHS_ERR_PARAM  -7   /**< invalid owner, length or buffer   */

#define TIKU_P4_USBHS_BULK_CAP 8192u /**< maximum bulk record bytes */
/** @brief Claim the OTG device for vendor bulk records; owner must be nonzero. */
int tiku_p4_usbhs_bulk_start(uint32_t owner);
/** @brief Detach and cancel DMA for the matching owner; refuse other owners. */
int tiku_p4_usbhs_bulk_stop(uint32_t owner);
/** @brief Set the matching owner's process polled after USB interrupts.
 * @note The process must outlive the claim; NULL disables notifications.
 */
int tiku_p4_usbhs_bulk_set_process(uint32_t owner,struct tiku_process *process);
/** @brief Copy one received record, zero if none is ready, or a negative error.
 * @note Scheduler context. Records end with a short packet or at BULK_CAP bytes.
 * A nonzero undersized destination discards the record and returns ERR_PARAM.
 */
int tiku_p4_usbhs_bulk_receive(uint32_t owner,void *out,uint32_t capacity);
/** @brief Copy and queue one record, zero under backpressure, or a negative error. */
int tiku_p4_usbhs_bulk_send(uint32_t owner,const void *data,uint32_t length);
/** @brief Return the bus generation, or zero without a matching configured owner. */
uint32_t tiku_p4_usbhs_bulk_generation(uint32_t owner);

/** @brief A snapshot of the PHY controls and the DWC2 core. */
typedef struct {
    uint32_t clocks;        /**< bit 0 system clock, bit 1 PHY reference  */
    uint32_t resets;        /**< HP_USB_CLKRST_CTRL1 bits 2:1 (core, PHY)  */
    uint32_t otg_ctrl;      /**< HP_SYSTEM USBOTG20_CTRL                   */
    uint32_t snpsid;        /**< GSNPSID: the Synopsys core release        */
    uint32_t hwcfg2;        /**< GHWCFG2: DMA architecture, endpoints      */
    uint32_t hwcfg3;        /**< GHWCFG3: data FIFO depth in bits 31:16    */
    uint32_t hwcfg4;        /**< GHWCFG4: PHY width, IN endpoints          */
    uint32_t gotgctl;       /**< GOTGCTL: session valid bits               */
    uint32_t gusbcfg;       /**< GUSBCFG: PHY interface, mode              */
    uint32_t grstctl;       /**< GRSTCTL: soft reset, AHB idle             */
    uint32_t gintsts;       /**< GINTSTS: live interrupt status            */
    uint32_t dsts;          /**< DSTS: enumerated speed, suspend           */
    uint32_t dctl;          /**< DCTL: soft disconnect                     */
} tiku_p4_usbhs_regs_t;

/**
 * @brief Clock the core and the PHY, pulse their reset and soft-reset the
 *        core on a 16-bit UTMI+ interface, leaving it idle.
 *
 * @return TIKU_P4_USBHS_OK, _ERR_RESET or _ERR_IDLE
 */
int tiku_p4_usbhs_up(void);
/** @brief Detach, mask the core and hold the core and PHY in reset. */
void tiku_p4_usbhs_down(void);
/** @brief Fill @p out; the core's registers read 0 while it is down. */
void tiku_p4_usbhs_read(tiku_p4_usbhs_regs_t *out);

/**
 * @brief Present a 64 KB RAM disk holding an empty FAT12 volume "TIKUOS":
 *        core up, device mode, EP0, pull-up.
 *
 * The disk's memory comes from the SRAM tier and goes back at stop.
 *
 * @return TIKU_P4_USBHS_OK or a negative TIKU_P4_USBHS_ERR_*
 */
int tiku_p4_usbhs_msc_start(void);
/** @brief Drop the pull-up, take the core down and release the disk. */
void tiku_p4_usbhs_msc_stop(void);
/** @brief Copy up to @p n bytes (at most a block) of block @p lba; returns
 *         the count, 0 past the disk or with no disk. */
uint32_t tiku_p4_usbhs_msc_peek(uint32_t lba, uint8_t *dst, uint32_t n);
/** @brief Command, read, write, bad-CBW and interrupt counts, the
 *         configuration value and DSTS's speed (0 high, 1 or 3 full, 2 low;
 *         7 before enumeration). */
void tiku_p4_usbhs_msc_stats(uint32_t *cbw, uint32_t *rd, uint32_t *wr,
                             uint32_t *bad, uint32_t *irq, uint8_t *cfg,
                             uint32_t *speed);
/** @brief The core's interrupt in device mode; tiku_usbhs_arch.c calls it. */
void tiku_p4_usbhs_msc_irq(void);

#endif /* TIKU_ESP32P4_USBHS_ARCH_H_ */
