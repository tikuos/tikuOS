/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_adc_arch.c - ADC driver for STM32F411RE ADC1
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_adc_arch.h"
#include "tiku_pinmux_arch.h"
#include "tiku_stm32f411_regs.h"
#include "tiku.h"

#ifdef TIKU_BOARD_ADC_AVAILABLE  /* Board supports ADC1 */

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/** Busy-wait loop iteration limit to prevent infinite hangs. */
#define ADC_TIMEOUT                   10000U
#define STM32F411_ADC_CHANNEL_COUNT   16U
#define STM32F411_ADC_CR1_RES_MASK    (0x03U << STM32F411_ADC_CR1_RES_SHIFT)
#define STM32F411_ADC_CCR_ADCPRE_MASK (0x03U << STM32F411_ADC_CCR_ADCPRE_SHIFT)

/*---------------------------------------------------------------------------*/
/* PRIVATE TYPES                                                             */
/*---------------------------------------------------------------------------*/

struct stm32f411_adc_pin {
    uint8_t port;
    uint8_t pin;
};

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/** Saved reference configuration for use during reads. */
static uint8_t adc_reference;

// GPIO pin mappings for ADC channels 0-15
static const struct stm32f411_adc_pin g_adc_channel_pins[] = {
    {1U, 0U}, {1U, 1U}, {1U, 2U}, {1U, 3U},
    {1U, 4U}, {1U, 5U}, {1U, 6U}, {1U, 7U},
    {2U, 0U}, {2U, 1U}, {3U, 0U}, {3U, 1U},
    {3U, 2U}, {3U, 3U}, {3U, 4U}, {3U, 5U}
};

static tiku_adc_config_t g_adc_cfg;
static uint8_t g_adc_ready;

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

static int
adc_resolution_bits(uint8_t resolution, uint32_t *bits)
{
    if (bits == (uint32_t *)0) {
        return TIKU_ADC_ERR_PARAM;
    }

    switch (resolution) {
    case TIKU_ADC_RES_8BIT:
        *bits = STM32F411_ADC_CR1_RES_8BIT;
        return TIKU_ADC_OK;
    case TIKU_ADC_RES_10BIT:
        *bits = STM32F411_ADC_CR1_RES_10BIT;
        return TIKU_ADC_OK;
    case TIKU_ADC_RES_12BIT:
        *bits = STM32F411_ADC_CR1_RES_12BIT;
        return TIKU_ADC_OK;
    default:
        return TIKU_ADC_ERR_PARAM;
    }
}

static uint8_t
adc_external_channel_valid(uint8_t channel)
{
    return channel < STM32F411_ADC_CHANNEL_COUNT
        && g_adc_channel_pins[channel].port != 0U;
}

static uint8_t
adc_hw_channel(uint8_t channel)
{
    if (channel == TIKU_ADC_CH_TEMP) {
        return 16U;
    }
    if (channel == TIKU_ADC_CH_BATTERY) {
        return 18U;
    }

    return channel;
}

static void
adc_set_sample_time(uint8_t hw_channel, uint32_t sample_bits)
{
    uint32_t reg;
    uint32_t shift;

    shift = STM32F411_ADC_SMPR_CH_SHIFT(hw_channel);
    if (hw_channel < 10U) {
        reg = _STM32F411_REG(STM32F411_ADC_SMPR2);
        reg &= ~(0x07U << shift);
        reg |= (sample_bits & 0x07U) << shift;
        _STM32F411_REG(STM32F411_ADC_SMPR2) = reg;
    } else {
        reg = _STM32F411_REG(STM32F411_ADC_SMPR1);
        reg &= ~(0x07U << shift);
        reg |= (sample_bits & 0x07U) << shift;
        _STM32F411_REG(STM32F411_ADC_SMPR1) = reg;
    }
}

static void
adc_select_channel(uint8_t hw_channel)
{
    _STM32F411_REG(STM32F411_ADC_SQR1) &=
        ~STM32F411_ADC_SQR1_L_MASK;
    _STM32F411_REG(STM32F411_ADC_SQR2) = 0U;
    _STM32F411_REG(STM32F411_ADC_SQR3) = STM32F411_ADC_SQR3_SQ1(hw_channel);
}

static void
adc_disable_special_channels(void)
{
    _STM32F411_REG(STM32F411_ADC_CCR) &=
        ~(STM32F411_ADC_CCR_TSVREFE | STM32F411_ADC_CCR_VBATE);
}

static void
adc_enable_temp_channel(void)
{
    adc_disable_special_channels();
    _STM32F411_REG(STM32F411_ADC_CCR) |= STM32F411_ADC_CCR_TSVREFE;
}

static void
adc_enable_battery_channel(void)
{
    adc_disable_special_channels();
    _STM32F411_REG(STM32F411_ADC_CCR) |= STM32F411_ADC_CCR_VBATE;
}

static void
adc_reset_pin_if_analog(uint8_t channel)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;
    uint32_t moder;
    uint8_t port;
    uint8_t pin;

    if (!adc_external_channel_valid(channel)) {
        return;
    }

    port = g_adc_channel_pins[channel].port;
    pin  = g_adc_channel_pins[channel].pin;
    if (tiku_stm32f411_pinmux_resolve(port, pin, &gpio_base, &rcc_bit) != 0) {
        return;
    }

    stm32f411_rcc_enable_ahb1(rcc_bit);

    moder = _STM32F411_REG(STM32F411_GPIO_MODER(gpio_base));
    if (((moder >> (2U * (uint32_t)pin)) & 0x03U)
        == STM32F411_GPIO_MODE_ANALOG) {
        (void)tiku_stm32f411_pinmux_init_input(port, pin,
                                               STM32F411_GPIO_PUPD_NONE);
    }
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

int
tiku_adc_arch_init(const tiku_adc_config_t *config)
{
    uint32_t cr1;
    uint32_t cr2;
    uint32_t ccr;
    uint32_t resolution_bits;
    int status;

    if (config == (const tiku_adc_config_t *)0) {
        return TIKU_ADC_ERR_PARAM;
    }
    if (config->reference != TIKU_ADC_REF_AVCC) {
        // Only one reference option (AVCC) supported on the STM32F411xE
        return TIKU_ADC_ERR_PARAM;
    }

    status = adc_resolution_bits(config->resolution, &resolution_bits);
    if (status != TIKU_ADC_OK) {
        return status;
    }

    stm32f411_rcc_enable_apb2(STM32F411_RCC_APB2_ADC1);

    /*
    * CCR register default configuration:
    *   - Analog clock frequency: PCLK2 divided by 6
    *   - VBATE and TSVREFE (battery and temp sensor channels) disabled
    */
    ccr = _STM32F411_REG(STM32F411_ADC_CCR);
    ccr &= ~STM32F411_ADC_CCR_ADCPRE_MASK;
    ccr |= STM32F411_ADC_CCR_ADCPRE_PCLK2_DIV6;
    _STM32F411_REG(STM32F411_ADC_CCR) = ccr;
    adc_disable_special_channels();

    /*
    * CR1 register default configuration:
    * - Scan mode disabled (SCAN=0)
    * - Resolution: set according to config
    */
    cr1 = _STM32F411_REG(STM32F411_ADC_CR1);
    cr1 &= ~(STM32F411_ADC_CR1_SCAN | STM32F411_ADC_CR1_RES_MASK);
    cr1 |= resolution_bits;
    _STM32F411_REG(STM32F411_ADC_CR1) = cr1;

    /*
    * CR2 register default configuration:
    *  - Single conversion mode (CONT=0)
    *  - No hardware trigger (EXTEN=00)
    *  - Data right-aligned (ALIGN=0)
    *  - EOC flag set at end of each conversion (EOCS=1)
    *  - DMA disabled (DMA=0)
    */
    cr2 = _STM32F411_REG(STM32F411_ADC_CR2);
    cr2 &= ~(STM32F411_ADC_CR2_CONT
          | STM32F411_ADC_CR2_DMA
          | STM32F411_ADC_CR2_DDS
          | STM32F411_ADC_CR2_ALIGN
          | STM32F411_ADC_CR2_SWSTART);
    cr2 |= STM32F411_ADC_CR2_EOCS;
    _STM32F411_REG(STM32F411_ADC_CR2) = cr2;

    // Reset conversion sequences upon startup
    _STM32F411_REG(STM32F411_ADC_SQR1) &=
        ~STM32F411_ADC_SQR1_L_MASK;
    _STM32F411_REG(STM32F411_ADC_SQR2) = 0U;
    _STM32F411_REG(STM32F411_ADC_SQR3) = 0U;
    
    // Start ADC
    _STM32F411_REG(STM32F411_ADC_CR2) |= STM32F411_ADC_CR2_ADON;

    adc_reference = config->reference;
    g_adc_cfg = *config;
    g_adc_ready = 1U;

    return TIKU_ADC_OK;
}

void
tiku_adc_arch_close(void)
{
    uint8_t channel;

    // Disable ADC
    _STM32F411_REG(STM32F411_ADC_CR2) &= ~STM32F411_ADC_CR2_ADON;
    
    // Disable battery monitor and temperature sensor channels
    adc_disable_special_channels();
    _STM32F411_REG(STM32F411_RCC_APB2ENR) &= ~STM32F411_RCC_APB2_ADC1;

    // Reset any external channel pins that are in analog mode
    // This assumes that the ADC driver is the only user of those pins, which is true for the current board configuration
    for (channel = 0U; channel < STM32F411_ADC_CHANNEL_COUNT; ++channel) {
        adc_reset_pin_if_analog(channel);
    }

    adc_reference = TIKU_ADC_REF_AVCC;
    g_adc_ready = 0U;
}

int
tiku_adc_arch_channel_init(uint8_t channel)
{
    uint8_t hw_channel;

    if (g_adc_ready == 0U) {
        return TIKU_ADC_ERR_TIMEOUT;
    }

    if (channel == TIKU_ADC_CH_TEMP) {
        /* Enable the internal temperature sensor path on ADC channel 16. */
        adc_enable_temp_channel();
        hw_channel = adc_hw_channel(channel);
        adc_select_channel(hw_channel);
        adc_set_sample_time(hw_channel, STM32F411_ADC_SAMPLE_480CYCLES);
        return TIKU_ADC_OK;
    }

    if (channel == TIKU_ADC_CH_BATTERY) {
        /* Enable the internal battery monitor path on ADC channel 18. */
        adc_enable_battery_channel();
        hw_channel = adc_hw_channel(channel);
        adc_select_channel(hw_channel);
        adc_set_sample_time(hw_channel, STM32F411_ADC_SAMPLE_480CYCLES);
        return TIKU_ADC_OK;
    }

    if (!adc_external_channel_valid(channel)) {
        return TIKU_ADC_ERR_PARAM;
    }

    adc_disable_special_channels();

    if (tiku_stm32f411_pinmux_config(g_adc_channel_pins[channel].port,
                                     g_adc_channel_pins[channel].pin,
                                     STM32F411_GPIO_MODE_ANALOG,
                                     STM32F411_GPIO_PUPD_NONE,
                                     STM32F411_GPIO_SPEED_HIGH) != 0) {
        return TIKU_ADC_ERR_PARAM;
    }

    hw_channel = adc_hw_channel(channel);
    adc_select_channel(hw_channel);
    adc_set_sample_time(hw_channel, STM32F411_ADC_SAMPLE_84CYCLES);

    return TIKU_ADC_OK;
}

int
tiku_adc_arch_read(uint8_t channel, uint16_t *value)
{
    uint32_t cr2;
    uint8_t hw_channel;
    uint32_t timeout;

    if (value == (uint16_t *)0) {
        return TIKU_ADC_ERR_PARAM;
    }

    if (channel == TIKU_ADC_CH_TEMP) {
        adc_enable_temp_channel();
    } else if (channel == TIKU_ADC_CH_BATTERY) {
        adc_enable_battery_channel();
    } else if (adc_external_channel_valid(channel)) {
        adc_disable_special_channels();
    } else {
        return TIKU_ADC_ERR_PARAM;
    }

    hw_channel = adc_hw_channel(channel);
    adc_select_channel(hw_channel);

    if (channel >= TIKU_ADC_CH_TEMP) {
        adc_set_sample_time(hw_channel, STM32F411_ADC_SAMPLE_480CYCLES);
    } else {
        adc_set_sample_time(hw_channel, STM32F411_ADC_SAMPLE_84CYCLES);
    }

    _STM32F411_REG(STM32F411_ADC_SR) = 0U;

    cr2 = _STM32F411_REG(STM32F411_ADC_CR2);
    cr2 |= STM32F411_ADC_CR2_ADON;
    _STM32F411_REG(STM32F411_ADC_CR2) = cr2;
    _STM32F411_REG(STM32F411_ADC_CR2) = cr2 | STM32F411_ADC_CR2_SWSTART;

    timeout = ADC_TIMEOUT;
    while ((_STM32F411_REG(STM32F411_ADC_SR) & STM32F411_ADC_SR_EOC) == 0U) {
        if (--timeout == 0U) {
            return TIKU_ADC_ERR_TIMEOUT;
        }
    }

    *value = (uint16_t)_STM32F411_REG(STM32F411_ADC_DR);
    (void)adc_reference;
    (void)g_adc_cfg;
    return TIKU_ADC_OK;
}

#endif /* TIKU_BOARD_ADC_AVAILABLE */
