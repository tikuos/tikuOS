/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_spi_arch.c - STM32F411RE SPI backend
 *
 * Implements blocking SPI master transactions on SPI1 using the
 * STM32F411's 2-line full-duplex Motorola SPI block. Chip select is
 * managed by the caller via GPIO.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_spi_arch.h"
#include "tiku_pinmux_arch.h"
#include "tiku_stm32f411_regs.h"
#include "tiku.h"
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

#define TIKU_STM32F411_SPI_BASE       STM32F411_SPI1_BASE
#define TIKU_STM32F411_SPI_RCC_BIT    STM32F411_RCC_APB2_SPI1

/* Bound every busy-wait loop so a wedged peripheral cannot hang forever. */
#define SPI_TIMEOUT                   100000U
#define SPI_DUMMY_BYTE                0xFFU

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

static uint8_t g_spi_ready;

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

static int
stm32f411_spi_wait_set(uint32_t mask)
{
    uint32_t timeout;

    for (timeout = 0U; timeout < SPI_TIMEOUT; timeout++) {
        if (_STM32F411_REG(STM32F411_SPI_SR(TIKU_STM32F411_SPI_BASE)) & mask) {
            return 0;
        }
    }

    return -1;
}

static int
stm32f411_spi_wait_clear(uint32_t mask)
{
    uint32_t timeout;

    for (timeout = 0U; timeout < SPI_TIMEOUT; timeout++) {
        if ((_STM32F411_REG(STM32F411_SPI_SR(TIKU_STM32F411_SPI_BASE)) & mask)
            == 0U) {
            return 0;
        }
    }

    return -1;
}

static void
stm32f411_spi_clear_rx_state(void)
{
    while (_STM32F411_REG(STM32F411_SPI_SR(TIKU_STM32F411_SPI_BASE))
           & STM32F411_SPI_SR_RXNE) {
        (void)_STM32F411_REG8(STM32F411_SPI_DR(TIKU_STM32F411_SPI_BASE));
    }

    if (_STM32F411_REG(STM32F411_SPI_SR(TIKU_STM32F411_SPI_BASE))
        & STM32F411_SPI_SR_OVR) {
        (void)_STM32F411_REG8(STM32F411_SPI_DR(TIKU_STM32F411_SPI_BASE));
        (void)_STM32F411_REG(STM32F411_SPI_SR(TIKU_STM32F411_SPI_BASE));
    }
}

static int
stm32f411_spi_wait_idle(void)
{
    if (stm32f411_spi_wait_set(STM32F411_SPI_SR_TXE) != 0) {
        return -1;
    }

    if (stm32f411_spi_wait_clear(STM32F411_SPI_SR_BSY) != 0) {
        return -1;
    }

    return 0;
}


static uint32_t
stm32f411_spi_mode_bits(uint8_t mode)
{
    uint32_t cr1;

    cr1 = 0U;
    if (mode & 0x1U) {
        cr1 |= STM32F411_SPI_CR1_CPHA;
    }
    if (mode & 0x2U) {
        cr1 |= STM32F411_SPI_CR1_CPOL;
    }

    return cr1;
}

static int
stm32f411_spi_prescaler_bits(uint16_t prescaler, uint32_t *br_bits)
{
    // Sets the SPI baud rates in the CR1 register based on the prescaler.
    // The STM32F411's SPI only supports baud rates of f_PCLK / 2, 4, 8, ..., 256.
    switch (prescaler) {
    case 2U:
        *br_bits = (0x0U << STM32F411_SPI_CR1_BR_SHIFT);
        return 0;
    case 4U:
        *br_bits = (0x1U << STM32F411_SPI_CR1_BR_SHIFT);
        return 0;
    case 8U:
        *br_bits = (0x2U << STM32F411_SPI_CR1_BR_SHIFT);
        return 0;
    case 16U:
        *br_bits = (0x3U << STM32F411_SPI_CR1_BR_SHIFT);
        return 0;
    case 32U:
        *br_bits = (0x4U << STM32F411_SPI_CR1_BR_SHIFT);
        return 0;
    case 64U:
        *br_bits = (0x5U << STM32F411_SPI_CR1_BR_SHIFT);
        return 0;
    case 128U:
        *br_bits = (0x6U << STM32F411_SPI_CR1_BR_SHIFT);
        return 0;
    case 256U:
        *br_bits = (0x7U << STM32F411_SPI_CR1_BR_SHIFT);
        return 0;
    default:
        return -1;
    }
}

static int
stm32f411_spi_xfer_byte(uint8_t tx, uint8_t *rx)
{
    if (stm32f411_spi_wait_set(STM32F411_SPI_SR_TXE) != 0) {
        return TIKU_SPI_ERR_TIMEOUT;
    }

    _STM32F411_REG8(STM32F411_SPI_DR(TIKU_STM32F411_SPI_BASE)) = tx;

    if (stm32f411_spi_wait_set(STM32F411_SPI_SR_RXNE) != 0) {
        return TIKU_SPI_ERR_TIMEOUT;
    }

    *rx = _STM32F411_REG8(STM32F411_SPI_DR(TIKU_STM32F411_SPI_BASE));
    return TIKU_SPI_OK;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

int
tiku_spi_arch_init(const tiku_spi_config_t *config)
{
    uint32_t cr1;
    uint32_t br_bits;

    if (config == (const tiku_spi_config_t *)0) {
        return TIKU_SPI_ERR_PARAM;
    }
    if (config->mode > TIKU_SPI_MODE_3) {
        return TIKU_SPI_ERR_PARAM;
    }
    if (config->bit_order > TIKU_SPI_LSB_FIRST) {
        return TIKU_SPI_ERR_PARAM;
    }

    // Convert prescaler value to CR1 bits
    if (stm32f411_spi_prescaler_bits(config->prescaler, &br_bits) != 0) {
        return TIKU_SPI_ERR_PARAM;
    }

    // Check that the SCK/MISO/MOSI pins can be configured for SPI1 and are not used elsewhere.
    if (tiku_stm32f411_pinmux_config(TIKU_BOARD_SPI1_SCK_PORT,
                                     TIKU_BOARD_SPI1_SCK_PIN,
                                     STM32F411_GPIO_MODE_AF,
                                     STM32F411_GPIO_PUPD_NONE,
                                     STM32F411_GPIO_SPEED_HIGH,
                                     STM32F411_GPIO_AF_SPI1_2_4_5) != 0
        || tiku_stm32f411_pinmux_config(TIKU_BOARD_SPI1_MISO_PORT,
                                        TIKU_BOARD_SPI1_MISO_PIN,
                                        STM32F411_GPIO_MODE_AF,
                                        STM32F411_GPIO_PUPD_NONE,
                                        STM32F411_GPIO_SPEED_HIGH,
                                        STM32F411_GPIO_AF_SPI1_2_4_5) != 0
        || tiku_stm32f411_pinmux_config(TIKU_BOARD_SPI1_MOSI_PORT,
                                        TIKU_BOARD_SPI1_MOSI_PIN,
                                        STM32F411_GPIO_MODE_AF,
                                        STM32F411_GPIO_PUPD_NONE,
                                        STM32F411_GPIO_SPEED_HIGH,
                                        STM32F411_GPIO_AF_SPI1_2_4_5) != 0) {
        return TIKU_SPI_ERR_PARAM;
    }

    // Enable peripheral clock and reset SPI peripheral
    stm32f411_rcc_enable_apb2(TIKU_STM32F411_SPI_RCC_BIT);
    stm32f411_rcc_reset_apb2(TIKU_STM32F411_SPI_RCC_BIT);

    // Reset SPI control registers for configuration
    _STM32F411_REG(STM32F411_SPI_CR1(TIKU_STM32F411_SPI_BASE)) = 0U;
    _STM32F411_REG(STM32F411_SPI_CR2(TIKU_STM32F411_SPI_BASE)) = 0U;
    _STM32F411_REG(STM32F411_SPI_I2SCFGR(TIKU_STM32F411_SPI_BASE)) = 0U;
    stm32f411_spi_clear_rx_state();

    cr1 = STM32F411_SPI_CR1_MSTR
        | STM32F411_SPI_CR1_SSM
        | STM32F411_SPI_CR1_SSI
        | br_bits
        | stm32f411_spi_mode_bits(config->mode);

    // Set up LSB first order if specified (default is MSB first)
    if (config->bit_order == TIKU_SPI_LSB_FIRST) {
        cr1 |= STM32F411_SPI_CR1_LSBFIRST;
    }

    _STM32F411_REG(STM32F411_SPI_CR1(TIKU_STM32F411_SPI_BASE)) = cr1;
    _STM32F411_REG(STM32F411_SPI_CR1(TIKU_STM32F411_SPI_BASE)) =
        cr1 | STM32F411_SPI_CR1_SPE;

    g_spi_ready = 1U;

    SPI_PRINTF("init: mode=%u order=%s prescaler=%u\n",
               config->mode,
               config->bit_order == TIKU_SPI_MSB_FIRST ? "MSB" : "LSB",
               config->prescaler);

    return TIKU_SPI_OK;
}

void
tiku_spi_arch_close(void)
{
    uint32_t cr1;

    if (g_spi_ready == 0U) {
        return;
    }

    // Wait for any ongoing transfers to finish
    (void)stm32f411_spi_wait_idle();
    stm32f411_spi_clear_rx_state();

    cr1 = _STM32F411_REG(STM32F411_SPI_CR1(TIKU_STM32F411_SPI_BASE));
    cr1 &= ~STM32F411_SPI_CR1_SPE;
    _STM32F411_REG(STM32F411_SPI_CR1(TIKU_STM32F411_SPI_BASE)) = cr1;

    g_spi_ready = 0U;
    SPI_PRINTF("close\n");
}

uint8_t
tiku_spi_arch_transfer(uint8_t tx_byte)
{
    uint8_t rx_byte;

    if (g_spi_ready == 0U) {
        return 0xFFU;
    }
    if (stm32f411_spi_xfer_byte(tx_byte, &rx_byte) != TIKU_SPI_OK) {
        return 0xFFU;
    }
    if (stm32f411_spi_wait_idle() != 0) {
        return 0xFFU;
    }

    return rx_byte;
}

int
tiku_spi_arch_write(const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint8_t rx_discard;

    if (g_spi_ready == 0U || buf == (const uint8_t *)0) {
        return TIKU_SPI_ERR_PARAM;
    }

    for (i = 0U; i < len; i++) {
        // Transmit bytes in buffer and discard received bytes
        if (stm32f411_spi_xfer_byte(buf[i], &rx_discard) != TIKU_SPI_OK) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
    }

    if (stm32f411_spi_wait_idle() != 0) {
        return TIKU_SPI_ERR_TIMEOUT;
    }

    return TIKU_SPI_OK;
}

int
tiku_spi_arch_read(uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if (g_spi_ready == 0U || buf == (uint8_t *)0) {
        return TIKU_SPI_ERR_PARAM;
    }

    // Transmit dummy bytes and receive bytes from slave
    for (i = 0U; i < len; i++) {
        if (stm32f411_spi_xfer_byte(SPI_DUMMY_BYTE, &buf[i])
            != TIKU_SPI_OK) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
    }

    if (stm32f411_spi_wait_idle() != 0) {
        return TIKU_SPI_ERR_TIMEOUT;
    }

    return TIKU_SPI_OK;
}

int
tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len)
{
    uint16_t i;

    if (g_spi_ready == 0U || tx_buf == (const uint8_t *)0
        || rx_buf == (uint8_t *)0) {
        return TIKU_SPI_ERR_PARAM;
    }

    for (i = 0U; i < len; i++) {
        if (stm32f411_spi_xfer_byte(tx_buf[i], &rx_buf[i]) != TIKU_SPI_OK) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
    }

    if (stm32f411_spi_wait_idle() != 0) {
        return TIKU_SPI_ERR_TIMEOUT;
    }

    return TIKU_SPI_OK;
}
