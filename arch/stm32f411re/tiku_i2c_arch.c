/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_i2c_arch.c - STM32F411RE I2C compatibility backend
 *
 * First-stage STM32 bring-up for the blocking I2C master backend.
 * This file now locks the driver to I2C1 on PB8/PB9 and configures
 * both lines for AF4 open-drain operation with no internal pull-ups,
 * so external 4.7 kohm pull-up resistors set the bus idle-high level.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_i2c_arch.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_pinmux_arch.h"
#include "tiku_stm32f411_regs.h"
#include "tiku.h"
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Board contract                                                            */
/*---------------------------------------------------------------------------*/

#if !defined(TIKU_BOARD_I2C0_SDA_PORT) || !defined(TIKU_BOARD_I2C0_SDA_PIN) || \
    !defined(TIKU_BOARD_I2C0_SCL_PORT) || !defined(TIKU_BOARD_I2C0_SCL_PIN)
#error "STM32F411 I2C backend requires TIKU_BOARD_I2C0_* pin macros."
#endif

#if (TIKU_BOARD_I2C0_SDA_PORT != 2U) || (TIKU_BOARD_I2C0_SDA_PIN != 9U) || \
    (TIKU_BOARD_I2C0_SCL_PORT != 2U) || (TIKU_BOARD_I2C0_SCL_PIN != 8U)
#error "STM32F411 I2C backend is fixed to I2C1 on PB9/PB8."
#endif

/*---------------------------------------------------------------------------*/
/* Constants                                                                 */
/*---------------------------------------------------------------------------*/

#define TIKU_STM32F411_I2C_BASE       STM32F411_I2C1_BASE
#define TIKU_STM32F411_I2C_RCC_BIT    STM32F411_RCC_APB1_I2C1
#define TIKU_STM32F411_I2C_AF         STM32F411_GPIO_AF_I2C1_3
#define TIKU_STM32F411_I2C_PCLK1_FALLBACK_HZ  16000000UL

/* Bound every wait so a stuck bus or peripheral cannot hang forever. */
#define I2C_TIMEOUT                   100000U

#define I2C_FREQ_STANDARD_HZ          100000UL
#define I2C_FREQ_FAST_HZ              400000UL

#define I2C_ERROR_MASK  (STM32F411_I2C_SR1_BERR    \
                       | STM32F411_I2C_SR1_ARLO    \
                       | STM32F411_I2C_SR1_AF      \
                       | STM32F411_I2C_SR1_OVR     \
                       | STM32F411_I2C_SR1_TIMEOUT)

/*---------------------------------------------------------------------------*/
/* Private state                                                             */
/*---------------------------------------------------------------------------*/

static uint8_t g_i2c_ready;
static uint8_t g_i2c_speed = TIKU_I2C_SPEED_STANDARD;

/*---------------------------------------------------------------------------*/
/* Private helpers                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Program the I2C timing registers for the requested bus speed.
 *
 * Configures CR2.FREQ, CCR, TRISE, and the basic master-mode defaults
 * after the peripheral has been reset.
 *
 * @param speed  TIKU_I2C_SPEED_STANDARD or TIKU_I2C_SPEED_FAST
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
static int stm32f411_i2c_timing_init(uint8_t speed);

/**
 * @brief Divide with rounding up.
 *
 * Used for timing-register calculations so generated SCL periods do not
 * exceed the requested bus speed.
 *
 * @param numer  Numerator
 * @param denom  Denominator
 * @return Ceiling of numer / denom
 */
static uint32_t
stm32f411_i2c_div_round_up(uint32_t numer, uint32_t denom)
{
    return (numer + denom - 1U) / denom;
}

/**
 * @brief Configure PB8/PB9 for I2C1 operation.
 *
 * Sets both lines to AF4 open-drain with no internal pulls so external
 * 4.7 kohm pull-up resistors define the idle-high bus level.
 *
 * @return 0 on success, non-zero on failure
 */
static int
stm32f411_i2c_pins_init(void)
{
    /* I2C relies on external 4.7 kohm pull-ups, so keep the internal
     * pull resistors disabled and drive the lines as open-drain AF4. */
    if (tiku_stm32f411_pinmux_config(TIKU_BOARD_I2C0_SCL_PORT,
                                     TIKU_BOARD_I2C0_SCL_PIN,
                                     STM32F411_GPIO_MODE_AF,
                                     STM32F411_GPIO_PUPD_NONE,
                                     STM32F411_GPIO_SPEED_HIGH) != 0
        || tiku_stm32f411_pinmux_set_drive(TIKU_BOARD_I2C0_SCL_PORT,
                                           TIKU_BOARD_I2C0_SCL_PIN,
                                           1U) != 0
        || tiku_stm32f411_pinmux_set_af(TIKU_BOARD_I2C0_SCL_PORT,
                                        TIKU_BOARD_I2C0_SCL_PIN,
                                        TIKU_STM32F411_I2C_AF) != 0
        || tiku_stm32f411_pinmux_config(TIKU_BOARD_I2C0_SDA_PORT,
                                        TIKU_BOARD_I2C0_SDA_PIN,
                                        STM32F411_GPIO_MODE_AF,
                                        STM32F411_GPIO_PUPD_NONE,
                                        STM32F411_GPIO_SPEED_HIGH) != 0
        || tiku_stm32f411_pinmux_set_drive(TIKU_BOARD_I2C0_SDA_PORT,
                                           TIKU_BOARD_I2C0_SDA_PIN,
                                           1U) != 0
        || tiku_stm32f411_pinmux_set_af(TIKU_BOARD_I2C0_SDA_PORT,
                                        TIKU_BOARD_I2C0_SDA_PIN,
                                        TIKU_STM32F411_I2C_AF) != 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief Wait for an SR1 flag while checking for I2C fault bits.
 *
 * @param mask  SR1 bit mask to wait for
 * @return 0 if the flag was observed, -1 on timeout or peripheral fault
 */
static int
stm32f411_i2c_wait_sr1_set(uint32_t mask)
{
    uint32_t timeout;

    for (timeout = 0U; timeout < I2C_TIMEOUT; timeout++) {
        uint32_t sr1 = _STM32F411_REG(STM32F411_I2C_SR1(TIKU_STM32F411_I2C_BASE));

        if (sr1 & I2C_ERROR_MASK) {
            return -1;
        }
        if (sr1 & mask) {
            return 0;
        }
    }

    return -1;
}

/**
 * @brief Wait for bits in SR2 to clear.
 *
 * Used primarily to wait until BUSY drops after STOP or bus recovery.
 *
 * @param mask  SR2 bit mask that must become 0
 * @return 0 if the bits cleared, -1 on timeout
 */
static int
stm32f411_i2c_wait_sr2_clear(uint32_t mask)
{
    uint32_t timeout;

    for (timeout = 0U; timeout < I2C_TIMEOUT; timeout++) {
        if ((_STM32F411_REG(STM32F411_I2C_SR2(TIKU_STM32F411_I2C_BASE)) & mask)
            == 0U) {
            return 0;
        }
    }

    return -1;
}

/**
 * @brief Wait until the I2C bus is no longer busy.
 *
 * @return 0 if the bus became idle, -1 on timeout
 */
static int
stm32f411_i2c_wait_idle(void)
{
    return stm32f411_i2c_wait_sr2_clear(STM32F411_I2C_SR2_BUSY);
}

/**
 * @brief Restore the default master receive state.
 *
 * Re-enables ACK and clears POS so the peripheral is ready for the next
 * transaction after a read, abort, or recovery path.
 */
static void
stm32f411_i2c_restore_master_defaults(void)
{
    uint32_t cr1;

    cr1 = _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE));
    cr1 |= STM32F411_I2C_CR1_ACK;
    cr1 &= ~STM32F411_I2C_CR1_POS;
    _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) = cr1;
}

/**
 * @brief Clear the ADDR event by reading SR1 then SR2.
 *
 * STM32F4 I2C requires this exact sequence after address acknowledge.
 */
static void
stm32f411_i2c_clear_addr(void)
{
    (void)_STM32F411_REG(STM32F411_I2C_SR1(TIKU_STM32F411_I2C_BASE));
    (void)_STM32F411_REG(STM32F411_I2C_SR2(TIKU_STM32F411_I2C_BASE));
}

/**
 * @brief Translate the current SR1 fault state into a Tiku error code.
 *
 * @return TIKU_I2C_ERR_NACK for AF, otherwise TIKU_I2C_ERR_TIMEOUT
 */
static int
stm32f411_i2c_error_code(void)
{
    uint32_t sr1 = _STM32F411_REG(STM32F411_I2C_SR1(TIKU_STM32F411_I2C_BASE));

    if (sr1 & STM32F411_I2C_SR1_AF) {
        return TIKU_I2C_ERR_NACK;
    }
    if (sr1 & I2C_ERROR_MASK) {
        return TIKU_I2C_ERR_TIMEOUT;
    }

    return TIKU_I2C_ERR_TIMEOUT;
}

/**
 * @brief Clear sticky SR1 fault bits and drain the SR2 side effects.
 *
 * Used before starting a new transaction and after abort/recovery paths.
 */
static void
stm32f411_i2c_clear_status(void)
{
    uint32_t sr1;

    sr1 = _STM32F411_REG(STM32F411_I2C_SR1(TIKU_STM32F411_I2C_BASE));
    sr1 &= ~I2C_ERROR_MASK;
    _STM32F411_REG(STM32F411_I2C_SR1(TIKU_STM32F411_I2C_BASE)) = sr1;

    (void)_STM32F411_REG(STM32F411_I2C_SR2(TIKU_STM32F411_I2C_BASE));
}

/**
 * @brief Reset the I2C1 peripheral block to a known hardware state.
 *
 * Enables the APB1 clock, toggles the reset bit, and clears all locally
 * used I2C registers back to their reset values.
 */
static void
stm32f411_i2c_reset_block(void)
{
    // Enable clock and reset I2C peripheral
    stm32f411_rcc_enable_apb1(TIKU_STM32F411_I2C_RCC_BIT);
    stm32f411_rcc_reset_apb1(TIKU_STM32F411_I2C_RCC_BIT);

    // Reset all registers 
    _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) = 0U;
    _STM32F411_REG(STM32F411_I2C_CR2(TIKU_STM32F411_I2C_BASE)) = 0U;
    _STM32F411_REG(STM32F411_I2C_OAR1(TIKU_STM32F411_I2C_BASE)) = 0U;
    _STM32F411_REG(STM32F411_I2C_OAR2(TIKU_STM32F411_I2C_BASE)) = 0U;
    _STM32F411_REG(STM32F411_I2C_CCR(TIKU_STM32F411_I2C_BASE)) = 0U;
    _STM32F411_REG(STM32F411_I2C_TRISE(TIKU_STM32F411_I2C_BASE)) = 0U;
    _STM32F411_REG(STM32F411_I2C_FLTR(TIKU_STM32F411_I2C_BASE)) = 0U;
}

/**
 * @brief Reinitialize the peripheral after a failed STOP or hard fault.
 *
 * Reapplies the last configured bus speed so the backend remains usable
 * for the next transaction whenever recovery succeeds.
 */
static void
stm32f411_i2c_recover_peripheral(void)
{
    stm32f411_i2c_reset_block();
    stm32f411_i2c_clear_status();

    if (stm32f411_i2c_timing_init(g_i2c_speed) == TIKU_I2C_OK) {
        stm32f411_i2c_restore_master_defaults();
        g_i2c_ready = 1U;
    } else {
        g_i2c_ready = 0U;
    }
}

/**
 * @brief Finalize a STOP condition and restore post-transaction defaults.
 *
 * Waits for BUSY to clear, clears sticky status, and restores ACK/POS.
 * If the bus does not unwind cleanly, attempts a peripheral reinitialization.
 *
 * @return TIKU_I2C_OK on success, TIKU_I2C_ERR_TIMEOUT on failure
 */
static int
stm32f411_i2c_finish_stop(void)
{
    if (stm32f411_i2c_wait_idle() != 0) {
        stm32f411_i2c_recover_peripheral();
        return TIKU_I2C_ERR_TIMEOUT;
    }

    stm32f411_i2c_clear_status();
    stm32f411_i2c_restore_master_defaults();

    return TIKU_I2C_OK;
}

/**
 * @brief Abort the current transaction and map the hardware fault to Tiku.
 *
 * Generates STOP, restores the master defaults, and runs the common
 * STOP-finalization path before returning the translated error code.
 *
 * @return Negative Tiku I2C error code describing the failure
 */
static int
stm32f411_i2c_abort_transaction(void)
{
    uint32_t cr1;
    int rc;

    cr1 = _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE));
    cr1 |= STM32F411_I2C_CR1_STOP;
    cr1 |= STM32F411_I2C_CR1_ACK;
    cr1 &= ~STM32F411_I2C_CR1_POS;
    _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) = cr1;

    rc = stm32f411_i2c_error_code();
    (void)stm32f411_i2c_finish_stop();

    return rc;
}

/**
 * @brief Program timing and master-mode defaults for the requested speed.
 *
 * Derives timing values from the live APB1 clock and enables the peripheral
 * in polling-only master mode with ACK enabled by default.
 *
 * @param speed  TIKU_I2C_SPEED_STANDARD or TIKU_I2C_SPEED_FAST
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
static int
stm32f411_i2c_timing_init(uint8_t speed)
{
    unsigned long pclk1_hz;
    uint32_t freq_mhz;
    uint32_t ccr;
    uint32_t trise;
    uint32_t cr2;

    pclk1_hz = tiku_cpu_stm32f411_pclk1_get_hz();
    if (pclk1_hz == 0UL) {
        pclk1_hz = TIKU_STM32F411_I2C_PCLK1_FALLBACK_HZ;
    }

    freq_mhz = (uint32_t)(pclk1_hz / 1000000UL);
    if (freq_mhz < 2U || freq_mhz > STM32F411_I2C_CR2_FREQ_MASK) {
        return TIKU_I2C_ERR_PARAM;
    }

    cr2 = freq_mhz & STM32F411_I2C_CR2_FREQ_MASK;
    _STM32F411_REG(STM32F411_I2C_CR2(TIKU_STM32F411_I2C_BASE)) = cr2;

    if (speed == TIKU_I2C_SPEED_FAST) {
        /* Fast mode, duty = 2 (Tlow/Thigh = 2), SCL = FPCLK1 / (3 * CCR). */
        ccr = stm32f411_i2c_div_round_up((uint32_t)pclk1_hz,
                                         3U * (uint32_t)I2C_FREQ_FAST_HZ);
        if (ccr == 0U) {
            ccr = 1U;
        }
        if (ccr > STM32F411_I2C_CCR_CCR_MASK) {
            return TIKU_I2C_ERR_PARAM;
        }

        /* RM0383 fast-mode rise-time limit: 300 ns. */
        trise = ((freq_mhz * 300U) / 1000U) + 1U;
        _STM32F411_REG(STM32F411_I2C_CCR(TIKU_STM32F411_I2C_BASE)) =
            STM32F411_I2C_CCR_FS | (ccr & STM32F411_I2C_CCR_CCR_MASK);
    } else {
        /* Standard mode, SCL = FPCLK1 / (2 * CCR). CCR must be >= 4. */
        ccr = stm32f411_i2c_div_round_up((uint32_t)pclk1_hz,
                                         2U * (uint32_t)I2C_FREQ_STANDARD_HZ);
        if (ccr < 4U) {
            ccr = 4U;
        }
        if (ccr > STM32F411_I2C_CCR_CCR_MASK) {
            return TIKU_I2C_ERR_PARAM;
        }

        /* RM0383 standard-mode rise-time limit: 1000 ns. */
        trise = freq_mhz + 1U;
        _STM32F411_REG(STM32F411_I2C_CCR(TIKU_STM32F411_I2C_BASE)) =
            (ccr & STM32F411_I2C_CCR_CCR_MASK);
    }

    _STM32F411_REG(STM32F411_I2C_TRISE(TIKU_STM32F411_I2C_BASE)) = trise;
    _STM32F411_REG(STM32F411_I2C_FLTR(TIKU_STM32F411_I2C_BASE)) = 0U;

    /* Bit 14 must stay set even when we only use the peripheral as master. */
    _STM32F411_REG(STM32F411_I2C_OAR1(TIKU_STM32F411_I2C_BASE)) =
        STM32F411_BIT(14);

    /* Polling-only backend: leave DMA and interrupt enables cleared. */
    _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) =
        STM32F411_I2C_CR1_ACK;
    _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) =
        STM32F411_I2C_CR1_ACK | STM32F411_I2C_CR1_PE;

    I2C_PRINTF("timing: pclk1=%luHz speed=%s ccr=%lu trise=%lu\n",
               pclk1_hz,
               speed == TIKU_I2C_SPEED_FAST ? "400k" : "100k",
               (unsigned long)ccr,
               (unsigned long)trise);

    return TIKU_I2C_OK;
}

/**
 * @brief Execute the transmit phase of a master transaction.
 *
 * Drives START, address+write, the payload bytes, and optionally STOP.
 * Used by both pure-write and combined write-then-read transactions.
 *
 * @param addr       7-bit slave address
 * @param buf        Bytes to transmit
 * @param len        Number of bytes to send
 * @param send_stop  Non-zero to generate STOP after the final byte
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
static int
stm32f411_i2c_write_phase(uint8_t addr, const uint8_t *buf, uint16_t len,
                          uint8_t send_stop)
{
    uint16_t i;
    uint32_t cr1;

    if (buf == (const uint8_t *)0 || len == 0U) {
        return TIKU_I2C_ERR_PARAM;
    }

    stm32f411_i2c_restore_master_defaults();

    cr1 = _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE));
    _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) =
        cr1 | STM32F411_I2C_CR1_START;

    if (stm32f411_i2c_wait_sr1_set(STM32F411_I2C_SR1_SB) != 0) {
        return stm32f411_i2c_abort_transaction();
    }

    // Scale the address to 7 bits and set the LSB to 0 for writes
    _STM32F411_REG8(STM32F411_I2C_DR(TIKU_STM32F411_I2C_BASE)) =
        (uint8_t)((addr & 0x7FU) << 1);

    if (stm32f411_i2c_wait_sr1_set(STM32F411_I2C_SR1_ADDR) != 0) {
        return stm32f411_i2c_abort_transaction();
    }
    stm32f411_i2c_clear_addr();

    for (i = 0U; i < len; i++) {
        if (stm32f411_i2c_wait_sr1_set(STM32F411_I2C_SR1_TXE) != 0) {
            return stm32f411_i2c_abort_transaction();
        }

        _STM32F411_REG8(STM32F411_I2C_DR(TIKU_STM32F411_I2C_BASE)) = buf[i];
    }

    if (stm32f411_i2c_wait_sr1_set(STM32F411_I2C_SR1_TXE) != 0) {
        return stm32f411_i2c_abort_transaction();
    }
    if (stm32f411_i2c_wait_sr1_set(STM32F411_I2C_SR1_BTF) != 0) {
        return stm32f411_i2c_abort_transaction();
    }

    if (send_stop != 0U) {
        cr1 = _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE));
        _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) =
            cr1 | STM32F411_I2C_CR1_STOP;

        if (stm32f411_i2c_finish_stop() != TIKU_I2C_OK) {
            return TIKU_I2C_ERR_TIMEOUT;
        }
    }

    return TIKU_I2C_OK;
}

/**
 * @brief Execute the receive phase of a master transaction.
 *
 * Handles the STM32F4's distinct 1-byte, 2-byte, and multi-byte receive
 * sequences, including ACK/POS/STOP ordering and final STOP cleanup.
 *
 * @param addr  7-bit slave address
 * @param buf   Destination buffer
 * @param len   Number of bytes to receive
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
static int
stm32f411_i2c_read_phase(uint8_t addr, uint8_t *buf, uint16_t len)
{
    uint16_t remaining;
    uint32_t cr1;

    if (buf == (uint8_t *)0 || len == 0U) {
        return TIKU_I2C_ERR_PARAM;
    }

    stm32f411_i2c_restore_master_defaults();

    cr1 = _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE));
    if (len == 2U) {
        cr1 |= STM32F411_I2C_CR1_POS;
        _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) = cr1;
    }

    _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) =
        cr1 | STM32F411_I2C_CR1_START;

    if (stm32f411_i2c_wait_sr1_set(STM32F411_I2C_SR1_SB) != 0) {
        return stm32f411_i2c_abort_transaction();
    }

    // Scale the address to 7 bits and set the LSB to 1 for reads
    _STM32F411_REG8(STM32F411_I2C_DR(TIKU_STM32F411_I2C_BASE)) =
        (uint8_t)(((addr & 0x7FU) << 1) | 0x01U);

    if (stm32f411_i2c_wait_sr1_set(STM32F411_I2C_SR1_ADDR) != 0) {
        return stm32f411_i2c_abort_transaction();
    }

    if (len == 1U) {
        /* Single-byte read.
         * Make ACK disable before clearing ADDR, and send STOP after, for
         * receiving. */
        cr1 = _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE));
        cr1 &= ~STM32F411_I2C_CR1_ACK;
        _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) = cr1;

        stm32f411_i2c_clear_addr();

        _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) =
            cr1 | STM32F411_I2C_CR1_STOP;

        if (stm32f411_i2c_wait_sr1_set(STM32F411_I2C_SR1_RXNE) != 0) {
            return stm32f411_i2c_abort_transaction();
        }

        buf[0] = _STM32F411_REG8(STM32F411_I2C_DR(TIKU_STM32F411_I2C_BASE));

        if (stm32f411_i2c_finish_stop() != TIKU_I2C_OK) {
            return TIKU_I2C_ERR_TIMEOUT;
        }

        return TIKU_I2C_OK;
    }

    if (len == 2U) {
        /* 2-byte read.
         * Set ACK low before clearing ADDR, and set STOP high after BTF=1. */
        cr1 = _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE));
        cr1 &= ~STM32F411_I2C_CR1_ACK;
        _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) = cr1;

        stm32f411_i2c_clear_addr();

        if (stm32f411_i2c_wait_sr1_set(STM32F411_I2C_SR1_BTF) != 0) {
            return stm32f411_i2c_abort_transaction();
        }

        _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) =
            cr1 | STM32F411_I2C_CR1_STOP;

        buf[0] = _STM32F411_REG8(STM32F411_I2C_DR(TIKU_STM32F411_I2C_BASE));
        buf[1] = _STM32F411_REG8(STM32F411_I2C_DR(TIKU_STM32F411_I2C_BASE));

        if (stm32f411_i2c_finish_stop() != TIKU_I2C_OK) {
            return TIKU_I2C_ERR_TIMEOUT;
        }

        return TIKU_I2C_OK;
    }

    stm32f411_i2c_clear_addr();

    /* Multi-byte read. */
    remaining = len;
    while (remaining > 3U) {
        if (stm32f411_i2c_wait_sr1_set(STM32F411_I2C_SR1_RXNE) != 0) {
            return stm32f411_i2c_abort_transaction();
        }

        *buf++ = _STM32F411_REG8(STM32F411_I2C_DR(TIKU_STM32F411_I2C_BASE));
        remaining--;
    }

    /* Read for byte N-2 (after BTF=1 and setting ACK low). */
    if (stm32f411_i2c_wait_sr1_set(STM32F411_I2C_SR1_BTF) != 0) {
        return stm32f411_i2c_abort_transaction();
    }

    cr1 = _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE));
    cr1 &= ~STM32F411_I2C_CR1_ACK;
    _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) = cr1;

    *buf++ = _STM32F411_REG8(STM32F411_I2C_DR(TIKU_STM32F411_I2C_BASE));

    /* Read for bytes N-1 and N (after BTF=1 and setting STOP high). */
    if (stm32f411_i2c_wait_sr1_set(STM32F411_I2C_SR1_BTF) != 0) {
        return stm32f411_i2c_abort_transaction();
    }

    _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) =
        cr1 | STM32F411_I2C_CR1_STOP;

    *buf++ = _STM32F411_REG8(STM32F411_I2C_DR(TIKU_STM32F411_I2C_BASE));
    *buf = _STM32F411_REG8(STM32F411_I2C_DR(TIKU_STM32F411_I2C_BASE));

    if (stm32f411_i2c_finish_stop() != TIKU_I2C_OK) {
        return TIKU_I2C_ERR_TIMEOUT;
    }

    return TIKU_I2C_OK;
}

int
tiku_i2c_arch_init(const tiku_i2c_config_t *config)
{
    if (config == (const tiku_i2c_config_t *)0) {
        return TIKU_I2C_ERR_PARAM;
    }
    (void)config;

    if (stm32f411_i2c_pins_init() != 0) {
        return TIKU_I2C_ERR_PARAM;
    }

    stm32f411_i2c_reset_block();
    stm32f411_i2c_clear_status();
    if (stm32f411_i2c_timing_init(config->speed) != TIKU_I2C_OK) {
        return TIKU_I2C_ERR_PARAM;
    }

    g_i2c_speed = config->speed;
    g_i2c_ready = 1U;
    I2C_PRINTF("init: I2C1 on PB8/PB9, external pull-ups expected\n");
    return TIKU_I2C_OK;
}

void
tiku_i2c_arch_close(void)
{
    _STM32F411_REG(STM32F411_I2C_CR1(TIKU_STM32F411_I2C_BASE)) = 0U;
    g_i2c_ready = 0U;
}

int
tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    if (g_i2c_ready == 0U) {
        return TIKU_I2C_ERR_BUSY;
    }
    if (buf == (const uint8_t *)0 || len == 0U) {
        return TIKU_I2C_ERR_PARAM;
    }

    if (stm32f411_i2c_wait_idle() != 0) {
        return TIKU_I2C_ERR_BUSY;
    }

    stm32f411_i2c_clear_status();
    return stm32f411_i2c_write_phase(addr, buf, len, 1U);
}

int
tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    if (g_i2c_ready == 0U) {
        return TIKU_I2C_ERR_BUSY;
    }
    if (buf == (uint8_t *)0 || len == 0U) {
        return TIKU_I2C_ERR_PARAM;
    }

    if (stm32f411_i2c_wait_idle() != 0) {
        return TIKU_I2C_ERR_BUSY;
    }

    stm32f411_i2c_clear_status();
    return stm32f411_i2c_read_phase(addr, buf, len);
}

int
tiku_i2c_arch_write_read(uint8_t addr,
                         const uint8_t *tx_buf, uint16_t tx_len,
                         uint8_t *rx_buf, uint16_t rx_len)
{
    int rc;

    if (g_i2c_ready == 0U) {
        return TIKU_I2C_ERR_BUSY;
    }
    if (tx_buf == (const uint8_t *)0 || tx_len == 0U
        || rx_buf == (uint8_t *)0 || rx_len == 0U) {
        return TIKU_I2C_ERR_PARAM;
    }

    if (stm32f411_i2c_wait_idle() != 0) {
        return TIKU_I2C_ERR_BUSY;
    }

    stm32f411_i2c_clear_status();

    rc = stm32f411_i2c_write_phase(addr, tx_buf, tx_len, 0U);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }

    return stm32f411_i2c_read_phase(addr, rx_buf, rx_len);
}
