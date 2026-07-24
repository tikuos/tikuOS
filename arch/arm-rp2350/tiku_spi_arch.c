/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.c - SPI bus driver for RP2350 (ARM PrimeCell PL022 on SPI0)
 *
 * Master-only, 8-bit Motorola frames, blocking polled I/O. Pin
 * assignment from the board header (TIKU_BOARD_SPI0_SCK_PIN /
 * MOSI_PIN / MISO_PIN). CS / SS is left to the caller — drive any
 * free GPIO around each transaction.
 *
 * Bit order: PL022 only supports MSB-first natively. The kernel
 * config's TIKU_SPI_LSB_FIRST is rejected with TIKU_SPI_ERR_PARAM
 * rather than silently bit-reversing on the CPU (callers that
 * actually need LSB-first usually want a SW shifter so they can
 * spot the misconfiguration rather than absorb it).
 *
 * Clock: SCLK = clk_peri / (CPSR * (1 + SCR)).  We pick the smallest
 * even CPSR (>= 2) such that SCR fits in 8 bits, and floor SCR.
 * Effective rate is therefore <= the requested clk_peri/prescaler;
 * never higher.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_spi_arch.h"
#include "tiku_rp2350_regs.h"
#include "tiku_cpu_freq_boot_arch.h"
#include <stdint.h>

#ifndef TIKU_BOARD_SPI0_SCK_PIN
#define TIKU_BOARD_SPI0_SCK_PIN   18U
#endif
#ifndef TIKU_BOARD_SPI0_MOSI_PIN
#define TIKU_BOARD_SPI0_MOSI_PIN  19U
#endif
#ifndef TIKU_BOARD_SPI0_MISO_PIN
#define TIKU_BOARD_SPI0_MISO_PIN  16U
#endif

/** @brief Address of an SPI0 register at byte offset @p off. */
#define SPI_REG(off)   (RP2350_SPI0_BASE + (off))

/**
 * @defgroup spi_private SPI private constants
 * @brief Spin limit for FIFO polling.
 *
 * Bounds every SSPSR spin so a stuck FIFO cannot hang the kernel.
 * At 1 MHz SPI, ~1 ms of headroom is well above any single-byte
 * transfer time.
 * @{
 */
#define SPI_SPIN_LIMIT  100000UL
/** @} */

/** @brief Non-zero once tiku_spi_arch_init() has succeeded. */
static uint8_t spi_initialised;

/**
 * @brief Wait until a status flag is set in SSPSR.
 *
 * Polls SSPSR until @p mask has at least one bit set or the spin
 * limit is exhausted.
 *
 * @param mask  Bit mask to wait for (e.g. RP2350_SPI_SR_TNF).
 * @return 0 on success, -1 on timeout.
 */
static int wait_status(uint32_t mask) {
    uint32_t spin;
    for (spin = 0U; spin < SPI_SPIN_LIMIT; spin++) {
        if (_RP2350_REG(SPI_REG(RP2350_SPI_SSPSR)) & mask) {
            return 0;
        }
    }
    return -1;
}

/**
 * @brief Wait until a status flag is cleared in SSPSR.
 *
 * Polls SSPSR until @p mask has all bits clear or the spin limit is
 * exhausted.  Used to drain BSY before closing the controller.
 *
 * @param mask  Bit mask to wait for clearance.
 * @return 0 on success, -1 on timeout.
 */
static int wait_status_clear(uint32_t mask) {
    uint32_t spin;
    for (spin = 0U; spin < SPI_SPIN_LIMIT; spin++) {
        if ((_RP2350_REG(SPI_REG(RP2350_SPI_SSPSR)) & mask) == 0U) {
            return 0;
        }
    }
    return -1;
}

/**
 * @brief Compute PL022 clock divider fields (CPSR, SCR).
 *
 * Finds the smallest even CPSR in [2, 254] such that
 * CPSR * (1 + SCR) approximates @p prescaler with SCR in [0, 255].
 * Biasing toward a small CPSR gives the caller finer tunability via
 * SCR.  On failure (prescaler too large) falls back to the slowest
 * possible setting (CPSR=254, SCR=255).
 *
 * @param prescaler  Desired total divider (clk_peri / SCLK).
 * @param cpsr_out   Output: SSPCPSR value to write.
 * @param scr_out    Output: SCR field for SSPCR0.
 */
static void compute_clk(uint16_t prescaler, uint8_t *cpsr_out,
                        uint8_t *scr_out) {
    if (prescaler < 2U) {
        prescaler = 2U;
    }

    uint16_t cpsr;
    for (cpsr = 2U; cpsr <= 254U; cpsr += 2U) {
        uint16_t scr_plus_1 = (uint16_t)(prescaler / cpsr);
        if (scr_plus_1 == 0U) {
            scr_plus_1 = 1U;
        }
        if (scr_plus_1 <= 256U) {
            *cpsr_out = (uint8_t)cpsr;
            *scr_out  = (uint8_t)(scr_plus_1 - 1U);
            return;
        }
    }
    /* Fallback: slowest possible. */
    *cpsr_out = 254U;
    *scr_out  = 255U;
}

/**
 * @brief Initialize SPI0 with the given configuration.
 *
 * Brings SPI0 out of reset, muxes the SCK/MOSI/MISO pads to the SPI
 * function, programs the clock divider (CPSR + SCR), CR0 (8-bit
 * Motorola, mode bits), and enables the controller.
 *
 * LSB-first bit order is rejected with TIKU_SPI_ERR_PARAM because the
 * PL022 has no hardware LSB-first mode and a silent CPU-side bit-reversal
 * would hide misconfiguration.
 *
 * @param config  SPI bus parameters (mode, prescaler, bit_order).
 * @return TIKU_SPI_OK on success, TIKU_SPI_ERR_PARAM on NULL config,
 *         unsupported mode, or LSB-first bit order.
 */
int tiku_spi_arch_init(const tiku_spi_config_t *config) {
    if (config == (const tiku_spi_config_t *)0) {
        return TIKU_SPI_ERR_PARAM;
    }
    if (config->mode > TIKU_SPI_MODE_3) {
        return TIKU_SPI_ERR_PARAM;
    }
    if (config->bit_order == TIKU_SPI_LSB_FIRST) {
        /* PL022 has no LSB-first mode — fail loud rather than silent. */
        return TIKU_SPI_ERR_PARAM;
    }

    /* Bring SPI0 out of reset, then disable while we program. */
    rp2350_unreset(RP2350_RESETS_SPI0);
    _RP2350_REG(SPI_REG(RP2350_SPI_SSPCR1)) = 0U;

    /* Pad config: function 1 = SPI, input enable on (SPI controller
     * needs to read MISO), no pulls. */
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(TIKU_BOARD_SPI0_SCK_PIN)) =
        RP2350_PADS_IE | RP2350_PADS_DRIVE_4MA;
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(TIKU_BOARD_SPI0_MOSI_PIN)) =
        RP2350_PADS_IE | RP2350_PADS_DRIVE_4MA;
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(TIKU_BOARD_SPI0_MISO_PIN)) =
        RP2350_PADS_IE | RP2350_PADS_DRIVE_4MA;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(TIKU_BOARD_SPI0_SCK_PIN)) =
        RP2350_IO_FUNC_SPI;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(TIKU_BOARD_SPI0_MOSI_PIN)) =
        RP2350_IO_FUNC_SPI;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(TIKU_BOARD_SPI0_MISO_PIN)) =
        RP2350_IO_FUNC_SPI;

    /* SCLK = clk_peri / (CPSR * (1 + SCR)). */
    uint8_t cpsr, scr;
    compute_clk(config->prescaler, &cpsr, &scr);
    _RP2350_REG(SPI_REG(RP2350_SPI_SSPCPSR)) = (uint32_t)cpsr;

    /* CR0: 8-bit, Motorola format, mode bits, SCR. Mode 0..3 maps
     * directly to SPO/SPH per ARM PL022 TRM:
     *   mode 0 (CPOL=0, CPHA=0): SPO=0, SPH=0
     *   mode 1 (CPOL=0, CPHA=1): SPO=0, SPH=1
     *   mode 2 (CPOL=1, CPHA=0): SPO=1, SPH=0
     *   mode 3 (CPOL=1, CPHA=1): SPO=1, SPH=1 */
    uint32_t cr0 = (uint32_t)RP2350_SPI_CR0_DSS_8BIT
                 | (uint32_t)RP2350_SPI_CR0_FRF_MOTOROLA
                 | ((uint32_t)scr << RP2350_SPI_CR0_SCR_SHIFT);
    if (config->mode & 0x2U) cr0 |= RP2350_SPI_CR0_SPO;
    if (config->mode & 0x1U) cr0 |= RP2350_SPI_CR0_SPH;
    _RP2350_REG(SPI_REG(RP2350_SPI_SSPCR0)) = cr0;

    /* CR1: enable, master mode (MS=0), no loopback. */
    _RP2350_REG(SPI_REG(RP2350_SPI_SSPCR1)) = RP2350_SPI_CR1_SSE;

    spi_initialised = 1U;
    return TIKU_SPI_OK;
}

/**
 * @brief Disable the SPI0 controller.
 *
 * Waits for any in-flight transfer to drain (BSY clear) before
 * clearing SSPCR1.SSE, preventing a truncated CS-asserted byte.
 * Sets spi_initialised to 0 so subsequent calls return early.
 */
void tiku_spi_arch_close(void) {
    /* Wait for any in-flight transfer to drain so we don't truncate
     * a CS-asserted byte. */
    (void)wait_status_clear(RP2350_SPI_SR_BSY);
    _RP2350_REG(SPI_REG(RP2350_SPI_SSPCR1)) = 0U;
    spi_initialised = 0U;
}

/**
 * @brief Perform a full-duplex single-byte SPI transfer.
 *
 * Writes @p tx_byte into the TX FIFO and returns the byte shifted in
 * on MISO during the same clock cycles.  The PL022 always shifts a
 * byte in both directions; there is no write-only or read-only path
 * at the hardware level.
 *
 * Returns 0xFF on any error (not initialized or FIFO timeout) so the
 * caller can detect a communication anomaly without a separate error
 * path.
 *
 * @param tx_byte  Byte to transmit.
 * @return Received byte, or 0xFF on timeout or if not initialized.
 */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte) {
    if (spi_initialised == 0U) {
        return 0xFFU;
    }
    /* Push the TX byte; PL022 always shifts a byte both directions. */
    if (wait_status(RP2350_SPI_SR_TNF) < 0) {
        return 0xFFU;
    }
    _RP2350_REG(SPI_REG(RP2350_SPI_SSPDR)) = (uint32_t)tx_byte;

    /* Wait for the matching RX byte. */
    if (wait_status(RP2350_SPI_SR_RNE) < 0) {
        return 0xFFU;
    }
    return (uint8_t)(_RP2350_REG(SPI_REG(RP2350_SPI_SSPDR)) & 0xFFU);
}

/**
 * @brief Write a buffer over SPI, discarding received bytes.
 *
 * Sends each byte in @p buf and drains the RX FIFO to prevent
 * overflow.  The received data is uninteresting in write-only
 * transactions and is discarded.
 *
 * @param buf  Source buffer (may be NULL only when len == 0).
 * @param len  Number of bytes to transmit.
 * @return TIKU_SPI_OK on success, TIKU_SPI_ERR_PARAM if buf is NULL
 *         with a non-zero length or SPI is not initialized,
 *         TIKU_SPI_ERR_TIMEOUT on FIFO stall.
 */
int tiku_spi_arch_write(const uint8_t *buf, uint16_t len) {
    if (spi_initialised == 0U || (buf == (const uint8_t *)0 && len > 0U)) {
        return TIKU_SPI_ERR_PARAM;
    }
    uint16_t i;
    for (i = 0U; i < len; i++) {
        if (wait_status(RP2350_SPI_SR_TNF) < 0) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
        _RP2350_REG(SPI_REG(RP2350_SPI_SSPDR)) = (uint32_t)buf[i];
        /* Drain the RX side so the FIFO doesn't overrun. The byte
         * itself is uninteresting — caller asked for write-only. */
        if (wait_status(RP2350_SPI_SR_RNE) < 0) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
        (void)_RP2350_REG(SPI_REG(RP2350_SPI_SSPDR));
    }
    return TIKU_SPI_OK;
}

/**
 * @brief Read a buffer over SPI, transmitting 0xFF filler bytes.
 *
 * Sends 0xFF for each byte position so the slave receives clock
 * cycles and returns data, which is captured into @p buf.
 *
 * @param buf  Destination buffer (may be NULL only when len == 0).
 * @param len  Number of bytes to receive.
 * @return TIKU_SPI_OK on success, TIKU_SPI_ERR_PARAM if buf is NULL
 *         with a non-zero length or SPI is not initialized,
 *         TIKU_SPI_ERR_TIMEOUT on FIFO stall.
 */
int tiku_spi_arch_read(uint8_t *buf, uint16_t len) {
    if (spi_initialised == 0U || (buf == (uint8_t *)0 && len > 0U)) {
        return TIKU_SPI_ERR_PARAM;
    }
    uint16_t i;
    for (i = 0U; i < len; i++) {
        if (wait_status(RP2350_SPI_SR_TNF) < 0) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
        /* Send 0xFF as filler so the slave gets clock cycles. */
        _RP2350_REG(SPI_REG(RP2350_SPI_SSPDR)) = 0xFFU;
        if (wait_status(RP2350_SPI_SR_RNE) < 0) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
        buf[i] = (uint8_t)(_RP2350_REG(SPI_REG(RP2350_SPI_SSPDR)) & 0xFFU);
    }
    return TIKU_SPI_OK;
}

/**
 * @brief Full-duplex SPI transfer over two equal-length buffers.
 *
 * Simultaneously transmits from @p tx_buf and receives into @p rx_buf
 * for @p len bytes.  Both pointers must be non-NULL when len > 0.
 *
 * @param tx_buf  Source buffer for transmitted data.
 * @param rx_buf  Destination buffer for received data.
 * @param len     Number of bytes to exchange.
 * @return TIKU_SPI_OK on success, TIKU_SPI_ERR_PARAM if SPI is not
 *         initialized or either buffer is NULL with len > 0,
 *         TIKU_SPI_ERR_TIMEOUT on FIFO stall.
 */
int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                             uint16_t len) {
    if (spi_initialised == 0U) {
        return TIKU_SPI_ERR_PARAM;
    }
    if (len > 0U && (tx_buf == (const uint8_t *)0 ||
                     rx_buf == (uint8_t *)0)) {
        return TIKU_SPI_ERR_PARAM;
    }
    uint16_t i;
    for (i = 0U; i < len; i++) {
        if (wait_status(RP2350_SPI_SR_TNF) < 0) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
        _RP2350_REG(SPI_REG(RP2350_SPI_SSPDR)) = (uint32_t)tx_buf[i];
        if (wait_status(RP2350_SPI_SR_RNE) < 0) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
        rx_buf[i] = (uint8_t)(_RP2350_REG(SPI_REG(RP2350_SPI_SSPDR)) & 0xFFU);
    }
    return TIKU_SPI_OK;
}
