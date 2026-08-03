/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pwm_arch.c - STM32N6 PWM on TIM1 channels 1 to 4.
 *
 * The four channels share one counter, so they share a frequency; the last
 * requested rate wins and each channel keeps its own duty.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pwm_arch.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_gpio_arch.h"
#include "tiku_stm32n6_regs.h"

#define TIM         STM32N6_TIM1_BASE
#define PWM_CHANS   4U

/** @brief Pin to TIM1 channel, from ST's mapping for this package. */
static const uint8_t pwm_pins[PWM_CHANS] = { 9U, 11U, 13U, 14U };

/** @brief Duty last requested per channel, so a read-back matches. */
static uint16_t pwm_duty[PWM_CHANS];

/** @brief Counter wrap shared by every channel. */
static uint16_t pwm_top;

/**
 * @brief Map a port E pin to a TIM1 channel number.
 *
 * @param pin  Port E pin
 * @return Channel 1..4, or 0 when the pin has no TIM1 output
 */
static unsigned pwm_channel_of(uint8_t pin) {
    for (unsigned i = 0U; i < PWM_CHANS; i++) {
        if (pwm_pins[i] == pin) {
            return i + 1U;
        }
    }
    return 0U;
}

/**
 * @brief Clock reaching the timer, in Hz.
 *
 * TIM1 hangs off APB2, which this port programs undivided from AHB, so the
 * timer sees the AHB rate the clock driver reports.
 */
static unsigned long pwm_tim_clock_hz(void) {
    tiku_stm32n6_clock_t c;
    tiku_cpu_stm32n6_clock_probe(&c);
    if (c.pll1_hz == 0UL || c.ic2_div == 0U || c.ahb_div == 0UL) {
        return 0UL;
    }
    return (c.pll1_hz / c.ic2_div) / c.ahb_div;
}

/** @brief Write one channel's compare value from a 0..65535 duty. */
static void pwm_write_duty(unsigned ch, uint16_t duty_u16) {
    uint32_t cmp = ((uint32_t)duty_u16 * ((uint32_t)pwm_top + 1UL)) / 65536UL;
    TIKU_REG32(STM32N6_TIM_CCR(TIM, ch)) = cmp;
}

int tiku_pwm_arch_init(uint8_t gpio_pin, uint32_t freq_hz, uint16_t duty_u16) {
    unsigned ch = pwm_channel_of(gpio_pin);
    if (ch == 0U || freq_hz == 0U) {
        return TIKU_PWM_ERR_INVALID;
    }

    unsigned long tim_hz = pwm_tim_clock_hz();
    if (tim_hz == 0UL) {
        return TIKU_PWM_ERR_FREQ;
    }

    /* Pick the smallest prescaler that fits the period in the 16-bit counter,
     * which keeps the duty resolution as high as the rate allows. */
    unsigned long ticks = tim_hz / freq_hz;
    unsigned long psc   = 0UL;
    while ((ticks / (psc + 1UL)) > 65536UL) {
        psc++;
        if (psc > 0xFFFFUL) {
            return TIKU_PWM_ERR_FREQ;
        }
    }
    unsigned long top = (ticks / (psc + 1UL));
    if (top < 2UL) {
        return TIKU_PWM_ERR_FREQ;
    }
    top -= 1UL;

    TIKU_REG32(STM32N6_RCC_APB2ENR) |= STM32N6_RCC_APB2ENR_TIM1;
    (void)TIKU_REG32(STM32N6_RCC_APB2ENR);

    tiku_stm32n6_gpio_init_alt(STM32N6_TIM1_PWM_PORT, gpio_pin, STM32N6_TIM1_AF);

    pwm_top = (uint16_t)top;
    TIKU_REG32(STM32N6_TIM_PSC(TIM)) = (uint32_t)psc;
    TIKU_REG32(STM32N6_TIM_ARR(TIM)) = (uint32_t)pwm_top;

    /* PWM mode 1 with the compare register preloaded, so a duty change lands
     * on a period boundary rather than mid-pulse. */
    uint32_t ccmr_shift = ((ch - 1U) & 1U) ? 8U : 0U;
    uint32_t ccmr_reg   = (ch <= 2U) ? STM32N6_TIM_CCMR1(TIM)
                                     : STM32N6_TIM_CCMR2(TIM);
    uint32_t ccmr = TIKU_REG32(ccmr_reg);
    ccmr &= ~(0xFFUL << ccmr_shift);
    ccmr |= ((6UL << 4) | (1UL << 3)) << ccmr_shift;   /* OCxM=110, OCxPE */
    TIKU_REG32(ccmr_reg) = ccmr;

    pwm_duty[ch - 1U] = duty_u16;
    pwm_write_duty(ch, duty_u16);

    TIKU_REG32(STM32N6_TIM_CCER(TIM)) |= (1UL << ((ch - 1U) * 4U));
    TIKU_REG32(STM32N6_TIM_CR1(TIM))  |= STM32N6_TIM_CR1_ARPE;
    /* TIM1 is an advanced timer: its outputs stay high-impedance until the
     * master output enable is set. */
    TIKU_REG32(STM32N6_TIM_BDTR(TIM)) |= STM32N6_TIM_BDTR_MOE;
    TIKU_REG32(STM32N6_TIM_EGR(TIM))   = STM32N6_TIM_EGR_UG;
    TIKU_REG32(STM32N6_TIM_CR1(TIM))  |= STM32N6_TIM_CR1_CEN;
    return TIKU_PWM_OK;
}

int tiku_pwm_arch_set_duty(uint8_t gpio_pin, uint16_t duty_u16) {
    unsigned ch = pwm_channel_of(gpio_pin);
    if (ch == 0U) {
        return TIKU_PWM_ERR_INVALID;
    }
    pwm_duty[ch - 1U] = duty_u16;
    pwm_write_duty(ch, duty_u16);
    return TIKU_PWM_OK;
}

int tiku_pwm_arch_close(uint8_t gpio_pin) {
    unsigned ch = pwm_channel_of(gpio_pin);
    if (ch == 0U) {
        return TIKU_PWM_ERR_INVALID;
    }
    TIKU_REG32(STM32N6_TIM_CCER(TIM)) &= ~(1UL << ((ch - 1U) * 4U));
    pwm_duty[ch - 1U] = 0U;

    /* Park the counter once no channel is still driving a pin. */
    if ((TIKU_REG32(STM32N6_TIM_CCER(TIM)) & 0x1111UL) == 0UL) {
        TIKU_REG32(STM32N6_TIM_CR1(TIM))  &= ~STM32N6_TIM_CR1_CEN;
        TIKU_REG32(STM32N6_TIM_BDTR(TIM)) &= ~STM32N6_TIM_BDTR_MOE;
    }
    tiku_stm32n6_gpio_init_output(STM32N6_TIM1_PWM_PORT, gpio_pin);
    return TIKU_PWM_OK;
}

uint16_t tiku_pwm_arch_get_duty(uint8_t gpio_pin) {
    unsigned ch = pwm_channel_of(gpio_pin);
    return (ch == 0U) ? 0U : pwm_duty[ch - 1U];
}

uint16_t tiku_pwm_arch_get_top(uint8_t gpio_pin) {
    return (pwm_channel_of(gpio_pin) == 0U) ? 0U : pwm_top;
}

int tiku_pwm_arch_is_enabled(uint8_t gpio_pin) {
    unsigned ch = pwm_channel_of(gpio_pin);
    if (ch == 0U) {
        return 0;
    }
    return (TIKU_REG32(STM32N6_TIM_CCER(TIM)) & (1UL << ((ch - 1U) * 4U))) ? 1 : 0;
}
