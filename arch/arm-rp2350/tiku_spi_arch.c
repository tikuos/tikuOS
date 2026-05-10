/*
 * Tiku Operating System v0.04
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

#define SPI_REG(off)   (RP2350_SPI0_BASE + (off))

/* Bound every spin so a stuck FIFO can't hang the kernel. ~1 ms at
 * 1 MHz SPI is comfortably above any single-byte transfer time. */
#define SPI_SPIN_LIMIT  100000UL

static uint8_t spi_initialised;

static int wait_status(uint32_t mask) {
    uint32_t spin;
    for (spin = 0U; spin < SPI_SPIN_LIMIT; spin++) {
        if (_RP2350_REG(SPI_REG(RP2350_SPI_SSPSR)) & mask) {
            return 0;
        }
    }
    return -1;
}

static int wait_status_clear(uint32_t mask) {
    uint32_t spin;
    for (spin = 0U; spin < SPI_SPIN_LIMIT; spin++) {
        if ((_RP2350_REG(SPI_REG(RP2350_SPI_SSPSR)) & mask) == 0U) {
            return 0;
        }
    }
    return -1;
}

/* Compute (CPSR, SCR) such that CPSR * (1 + SCR) approximates the
 * caller's requested divider. CPSR is clamped to [2, 254] and even,
 * SCR to [0, 255]. We bias toward the smallest CPSR (gives the
 * caller the finest tunability via SCR). */
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

void tiku_spi_arch_close(void) {
    /* Wait for any in-flight transfer to drain so we don't truncate
     * a CS-asserted byte. */
    (void)wait_status_clear(RP2350_SPI_SR_BSY);
    _RP2350_REG(SPI_REG(RP2350_SPI_SSPCR1)) = 0U;
    spi_initialised = 0U;
}

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
