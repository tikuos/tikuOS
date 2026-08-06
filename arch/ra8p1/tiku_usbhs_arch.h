/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbhs_arch.h - EK-RA8P1 USB 2.0 high-speed device controller.
 *
 * Power, clock and PHY; the pull-up presented on demand; enumeration over the
 * default control pipe; and mass storage across two bulk pipes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_USBHS_ARCH_H_
#define TIKU_RA8P1_USBHS_ARCH_H_

#include <stdint.h>

#define TIKU_RA8P1_USBHS_OK          0
#define TIKU_RA8P1_USBHS_ERR_CLOCK  -1  /**< the PHY PLL never locked      */
#define TIKU_RA8P1_USBHS_ERR_STATE  -2  /**< called in the wrong order     */

/** @brief Speed the chirp handshake settled on, read from DVSTCTR0.RHST. */
typedef enum {
    TIKU_RA8P1_USBHS_SPEED_NONE = 0, /**< no connection, or still resetting */
    TIKU_RA8P1_USBHS_SPEED_FULL,
    TIKU_RA8P1_USBHS_SPEED_HIGH,
} tiku_ra8p1_usbhs_speed_t;

/** @brief Device state, from INTSTS0.DVSQ. */
typedef enum {
    TIKU_RA8P1_USBHS_DEV_POWERED = 0,
    TIKU_RA8P1_USBHS_DEV_DEFAULT,     /**< a bus reset has been seen        */
    TIKU_RA8P1_USBHS_DEV_ADDRESS,
    TIKU_RA8P1_USBHS_DEV_CONFIGURED,
    TIKU_RA8P1_USBHS_DEV_SUSPEND,
} tiku_ra8p1_usbhs_devstate_t;

/*
 * Brought up DETACHED on purpose.  Presenting the pull-up is what starts a
 * conversation with deadlines set by the other end, and being able to look
 * at the controller before that happens is the difference between debugging
 * bring-up and debugging enumeration.
 *
 * NOTE ON THIS BOARD: VBUS is not routed to the MCU -- USBHS_VBUS would be
 * P408, which the EK-RA8P1 gives to a Pmod header -- so INTSTS0.VBSTS does
 * not report whether a cable is plugged in.  Attachment has to be inferred
 * from bus activity instead.
 */

/**
 * @brief Power, clock and release the PHY; leave the device detached.
 *
 * @param want_high non-zero to enable high-speed operation
 * @return TIKU_RA8P1_USBHS_OK, or a negative error code
 */
int tiku_ra8p1_usbhs_up(int want_high);

/**
 * @brief Present or remove the D+ pull-up (SYSCFG.DPRPU).
 *
 * @param on non-zero to attach
 * @return TIKU_RA8P1_USBHS_OK, or a negative error code
 */
int tiku_ra8p1_usbhs_attach(int on);

/** @brief Detach, disable the controller and power the PHY back down. */
void tiku_ra8p1_usbhs_down(void);

/** @brief Speed the host and device settled on. */
tiku_ra8p1_usbhs_speed_t tiku_ra8p1_usbhs_speed(void);

/** @brief Where the device is in the USB state machine. */
tiku_ra8p1_usbhs_devstate_t tiku_ra8p1_usbhs_devstate(void);

/** @brief Non-zero once the PHY's internal PLL reports lock. */
int tiku_ra8p1_usbhs_pll_locked(void);

/**
 * @brief USB interrupt handler; enumeration runs entirely from here.
 *
 * @note Vector index 16 + RA8P1_ICU_SLOT_USBHS, linked to USBHS_USBIR.
 */
void tiku_ra8p1_usbhs_handler(void);

/** @brief Interrupts taken, and device-state transitions seen. */
void tiku_ra8p1_usbhs_irq_stats(uint32_t *irqs, uint32_t *dvst);

/** @brief Address the host assigned; 0 until SET_ADDRESS. */
uint8_t tiku_ra8p1_usbhs_address(void);

/** @brief Configuration the host selected; 0 until SET_CONFIGURATION. */
uint8_t tiku_ra8p1_usbhs_configured(void);

/** @brief EP0 counters: setups decoded, requests stalled, most recent. */
void tiku_ra8p1_usbhs_ep0_stats(uint32_t *setup, uint32_t *stall,
                                uint16_t *last);

/**
 * @brief Read trace entry @p i (9 words: req val len ctsq dcp/ctr spins pre post).
 * @return total entries recorded, or 0 when @p i is past the end
 */
unsigned tiku_ra8p1_usbhs_ep0_trace(unsigned i, uint16_t *out9);

/**
 * @brief Service one mass-storage command, if the host has sent one.
 *
 * Poll alongside ep0_poll().  Bulk transfers have no deadline of their own,
 * but the host will not send the next command until this one is answered.
 */
void tiku_ra8p1_usbhs_msc_poll(void);

/** @brief OUT-pipe counters: packets taken, and stalls that ended a transfer. */
void tiku_ra8p1_usbhs_msc_out_stats(uint32_t *pkts, uint32_t *stalls);

/**
 * @brief The last four SCSI opcodes, plus recovery counters.
 *
 * @param resets  receives Bulk-Only Reset requests served
 * @param cswfail receives status wrappers that could not be delivered
 * @return the four opcodes packed little-endian, oldest in the low byte
 */
uint32_t tiku_ra8p1_usbhs_msc_trace(uint32_t *resets, uint32_t *cswfail);

/**
 * @brief Where the most recent WRITE(10) landed, and how many have happened.
 *
 * @param lba    receives the first block of the last write
 * @param blocks receives its length in blocks
 * @return total WRITE(10) commands served
 */
uint32_t tiku_ra8p1_usbhs_msc_last_write(uint32_t *lba, uint32_t *blocks);

/** @brief Pipe geometry read back from hardware: cfg/buf/maxp per pipe, last DTLN. */
void tiku_ra8p1_usbhs_pipe_regs(uint16_t *out7);

/** @brief MSC counters: wrappers seen, reads, writes, and failures. */
void tiku_ra8p1_usbhs_msc_stats(uint32_t *cbw, uint32_t *rd, uint32_t *wr,
                                uint32_t *bad);

/**
 * @brief FNV-1a over the first @p nblocks of the staging disk (0 = all).
 *
 * @param nblocks blocks to hash
 * @return the hash, to be compared against one computed on the host
 */
uint32_t tiku_ra8p1_usbhs_msc_hash(uint32_t nblocks);

/** @brief OTG ID pin: 1 = device role strap, 0 = host, -1 = not up. */
int tiku_ra8p1_usbhs_id_high(void);

/** @brief Non-zero while the controller is enabled. */
int tiku_ra8p1_usbhs_up_state(void);

/**
 * @brief Snapshot the registers worth seeing, in a fixed order.
 *
 * @param out receives SYSCFG, SYSSTS0, PLLSTA, DVSTCTR0, PHYSET, INTSTS0,
 *            LPSTS, FRMNUM
 * @param n   how many words @p out holds
 */
void tiku_ra8p1_usbhs_regs(uint16_t *out, unsigned n);

#endif /* TIKU_RA8P1_USBHS_ARCH_H_ */
