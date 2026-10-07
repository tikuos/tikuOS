/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - Hardware timer configuration for MSP430
 *
 * Compile-time clock source and dividers of the Timer A1 htimer, the tick
 * rate they give (TIKU_HTIMER_ARCH_SECOND) and the register values for them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_HTIMER_CONFIG_H_
#define TIKU_HTIMER_CONFIG_H_

#include <msp430.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TIMER CLOCK SOURCE OPTIONS                                                */
/*---------------------------------------------------------------------------*/

/** @name Clock sources for Timer A
 * @{ */
#define TIKU_HTIMER_SOURCE_SMCLK     0  /**< SMCLK: fine, higher power */
#define TIKU_HTIMER_SOURCE_ACLK      1  /**< ACLK: low power, coarse */
#define TIKU_HTIMER_SOURCE_EXTERNAL  2  /**< External clock input (TACLK) */
#define TIKU_HTIMER_SOURCE_INCLK     3  /**< INCLK (device specific) */
/** @} */

/** @name ACLK sources
 * @{ */
#define TIKU_ACLK_SOURCE_VLOCLK      0  /**< Internal VLO (~10kHz, varies) */
#define TIKU_ACLK_SOURCE_XT1CLK      1  /**< External crystal (32.768kHz) */
#define TIKU_ACLK_SOURCE_REFOCLK     2  /**< Internal reference (~32.768kHz) */
/** @} */

/** @name Timer divider options
 * @{ */
#define TIKU_HTIMER_DIV_1            0  /**< No division */
#define TIKU_HTIMER_DIV_2            1  /**< Divide by 2 */
#define TIKU_HTIMER_DIV_4            2  /**< Divide by 4 */
#define TIKU_HTIMER_DIV_8            3  /**< Divide by 8 */
/** @} */

/** @name Extended divider options, applied after the divider above
 * @{ */
#define TIKU_HTIMER_EXDIV_1          0  /**< No extended division */
#define TIKU_HTIMER_EXDIV_2          1  /**< Additional divide by 2 */
#define TIKU_HTIMER_EXDIV_3          2  /**< Additional divide by 3 */
#define TIKU_HTIMER_EXDIV_4          3  /**< Additional divide by 4 */
#define TIKU_HTIMER_EXDIV_5          4  /**< Additional divide by 5 */
#define TIKU_HTIMER_EXDIV_6          5  /**< Additional divide by 6 */
#define TIKU_HTIMER_EXDIV_7          6  /**< Additional divide by 7 */
#define TIKU_HTIMER_EXDIV_8          7  /**< Additional divide by 8 */
/** @} */

/*---------------------------------------------------------------------------*/
/* USER CONFIGURATION SECTION                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Selected preset: high accuracy, a 1 MHz timer from the default
 *        8 MHz SMCLK.
 *
 * The chain below tests this preset first, so another one
 * (TIKU_HTIMER_CONFIG_BALANCED, _LOW_POWER, _ULTRA_LOW_POWER or _CUSTOM)
 * takes effect only once this define is removed.
 */
#define TIKU_HTIMER_CONFIG_HIGH_ACCURACY
/* #define TIKU_HTIMER_CONFIG_BALANCED */
/* #define TIKU_HTIMER_CONFIG_LOW_POWER */
/* #define TIKU_HTIMER_CONFIG_ULTRA_LOW_POWER */
/* #define TIKU_HTIMER_CONFIG_CUSTOM */

/*---------------------------------------------------------------------------*/
/* PRESET CONFIGURATIONS                                                     */
/*---------------------------------------------------------------------------*/

#ifdef TIKU_HTIMER_CONFIG_HIGH_ACCURACY
/* High accuracy mode: SMCLK / 8 */
#define TIKU_HTIMER_CLOCK_SOURCE     TIKU_HTIMER_SOURCE_SMCLK
#define TIKU_HTIMER_DIVIDER          TIKU_HTIMER_DIV_8
#define TIKU_HTIMER_EX_DIVIDER       TIKU_HTIMER_EXDIV_1
#define TIKU_HTIMER_BASE_FREQ        TIKU_MAIN_CPU_HZ  /* SMCLK = MCLK */
#define TIKU_ACLK_CONFIG_SOURCE      TIKU_ACLK_SOURCE_VLOCLK  /* Not used */

#elif defined(TIKU_HTIMER_CONFIG_BALANCED)
/* Balanced mode: SMCLK / 64 */
#define TIKU_HTIMER_CLOCK_SOURCE     TIKU_HTIMER_SOURCE_SMCLK
#define TIKU_HTIMER_DIVIDER          TIKU_HTIMER_DIV_8
#define TIKU_HTIMER_EX_DIVIDER       TIKU_HTIMER_EXDIV_8
#define TIKU_HTIMER_BASE_FREQ        TIKU_MAIN_CPU_HZ  /* SMCLK = MCLK */
#define TIKU_ACLK_CONFIG_SOURCE      TIKU_ACLK_SOURCE_VLOCLK  /* Not used */

#elif defined(TIKU_HTIMER_CONFIG_LOW_POWER)
/* Low power mode: the system timer's 32.768 kHz ACLK. */
#define TIKU_HTIMER_CLOCK_SOURCE     TIKU_HTIMER_SOURCE_ACLK
#define TIKU_HTIMER_DIVIDER          TIKU_HTIMER_DIV_1
#define TIKU_HTIMER_EX_DIVIDER       TIKU_HTIMER_EXDIV_1
#define TIKU_HTIMER_BASE_FREQ        32768UL
#if defined(TIKU_DEVICE_MSP430FR2433)
#define TIKU_ACLK_CONFIG_SOURCE      TIKU_ACLK_SOURCE_REFOCLK
#else
#define TIKU_ACLK_CONFIG_SOURCE      TIKU_ACLK_SOURCE_XT1CLK
#endif

#elif defined(TIKU_HTIMER_CONFIG_ULTRA_LOW_POWER)
/* Ultra-low power: shared ACLK divided by eight. */
#define TIKU_HTIMER_CLOCK_SOURCE     TIKU_HTIMER_SOURCE_ACLK
#define TIKU_HTIMER_DIVIDER          TIKU_HTIMER_DIV_8
#define TIKU_HTIMER_EX_DIVIDER       TIKU_HTIMER_EXDIV_1
#define TIKU_HTIMER_BASE_FREQ        32768UL
#if defined(TIKU_DEVICE_MSP430FR2433)
#define TIKU_ACLK_CONFIG_SOURCE      TIKU_ACLK_SOURCE_REFOCLK
#else
#define TIKU_ACLK_CONFIG_SOURCE      TIKU_ACLK_SOURCE_XT1CLK
#endif


#elif defined(TIKU_HTIMER_CONFIG_CUSTOM)
/* Custom configuration: the build defines the values below */
#ifndef TIKU_HTIMER_CLOCK_SOURCE
#error "Please define TIKU_HTIMER_CLOCK_SOURCE"
#endif
#ifndef TIKU_HTIMER_DIVIDER
#error "Please define TIKU_HTIMER_DIVIDER"
#endif
#ifndef TIKU_HTIMER_EX_DIVIDER
#error "Please define TIKU_HTIMER_EX_DIVIDER"
#endif
#ifndef TIKU_HTIMER_BASE_FREQ
#error "Please define TIKU_HTIMER_BASE_FREQ"
#endif
#ifndef TIKU_ACLK_CONFIG_SOURCE
#define TIKU_ACLK_CONFIG_SOURCE TIKU_ACLK_SOURCE_VLOCLK
#endif

#else
/* Default to high accuracy if nothing selected */
#define TIKU_HTIMER_CLOCK_SOURCE     TIKU_HTIMER_SOURCE_SMCLK
#define TIKU_HTIMER_DIVIDER          TIKU_HTIMER_DIV_8
#define TIKU_HTIMER_EX_DIVIDER       TIKU_HTIMER_EXDIV_1
#define TIKU_HTIMER_BASE_FREQ        TIKU_MAIN_CPU_HZ  /* SMCLK = MCLK */
#define TIKU_ACLK_CONFIG_SOURCE      TIKU_ACLK_SOURCE_VLOCLK
#endif

/*---------------------------------------------------------------------------*/
/* CALCULATED TIMER FREQUENCY                                                */
/*---------------------------------------------------------------------------*/

/** @name Division factors of the selected divider codes
 * @{ */
#define TIKU_HTIMER_DIV_VALUE    (1 << TIKU_HTIMER_DIVIDER)
#define TIKU_HTIMER_EXDIV_VALUE  (TIKU_HTIMER_EX_DIVIDER + 1)
/** @} */

/** Timer clock in Hz: the base frequency over both dividers. */
#define TIKU_HTIMER_CALCULATED_FREQ  (TIKU_HTIMER_BASE_FREQ / \
                                     (TIKU_HTIMER_DIV_VALUE * TIKU_HTIMER_EXDIV_VALUE))

/** Hardware timer ticks per second */
#ifndef TIKU_HTIMER_ARCH_SECOND
#define TIKU_HTIMER_ARCH_SECOND TIKU_HTIMER_CALCULATED_FREQ
#endif

/*---------------------------------------------------------------------------*/
/* CONFIGURATION VALIDATION                                                  */
/*---------------------------------------------------------------------------*/

#if TIKU_HTIMER_CALCULATED_FREQ > 16000000UL
#error "Timer frequency too high (>16MHz)"
#endif

#if TIKU_HTIMER_CALCULATED_FREQ < 100UL
#error "Timer frequency too low (<100Hz)"
#endif

/*---------------------------------------------------------------------------*/
/* HELPER MACROS FOR REGISTER CONFIGURATION                                  */
/*---------------------------------------------------------------------------*/

/** TASSEL field value (TA1CTL) for the selected clock source. */
#if TIKU_HTIMER_CLOCK_SOURCE == TIKU_HTIMER_SOURCE_SMCLK
#define TIKU_HTIMER_TASSEL_VALUE TASSEL__SMCLK
#elif TIKU_HTIMER_CLOCK_SOURCE == TIKU_HTIMER_SOURCE_ACLK
#define TIKU_HTIMER_TASSEL_VALUE TASSEL__ACLK
#elif TIKU_HTIMER_CLOCK_SOURCE == TIKU_HTIMER_SOURCE_EXTERNAL
#define TIKU_HTIMER_TASSEL_VALUE TASSEL__TACLK
#elif TIKU_HTIMER_CLOCK_SOURCE == TIKU_HTIMER_SOURCE_INCLK
#define TIKU_HTIMER_TASSEL_VALUE TASSEL__INCLK
#endif

/** ID field value (TA1CTL) for the selected divider. */
#if TIKU_HTIMER_DIVIDER == TIKU_HTIMER_DIV_1
#define TIKU_HTIMER_ID_VALUE ID__1
#elif TIKU_HTIMER_DIVIDER == TIKU_HTIMER_DIV_2
#define TIKU_HTIMER_ID_VALUE ID__2
#elif TIKU_HTIMER_DIVIDER == TIKU_HTIMER_DIV_4
#define TIKU_HTIMER_ID_VALUE ID__4
#elif TIKU_HTIMER_DIVIDER == TIKU_HTIMER_DIV_8
#define TIKU_HTIMER_ID_VALUE ID__8
#endif

/** TA1EX0 value: the extended divider code. */
#define TIKU_HTIMER_TAIDEX_VALUE TIKU_HTIMER_EX_DIVIDER

/*---------------------------------------------------------------------------*/
/* RUNTIME CONFIGURATION STRUCTURE                                           */
/*---------------------------------------------------------------------------*/

/** @brief The compile-time htimer settings, as tiku_htimer_get_config()
 *         reports them. */
typedef struct {
    uint8_t  clock_source;     /**< TIKU_HTIMER_SOURCE_* */
    uint8_t  divider;          /**< TIKU_HTIMER_DIV_* */
    uint8_t  ex_divider;       /**< TIKU_HTIMER_EXDIV_* */
    uint8_t  aclk_source;      /**< TIKU_ACLK_SOURCE_* */
    uint32_t base_frequency;   /**< Base clock frequency in Hz */
    uint32_t timer_frequency;  /**< Calculated timer frequency in Hz */
} tiku_htimer_config_t;

/** @brief Fill @p config with the compile-time htimer settings. */
static inline void tiku_htimer_get_config(tiku_htimer_config_t *config)
{
    config->clock_source = TIKU_HTIMER_CLOCK_SOURCE;
    config->divider = TIKU_HTIMER_DIVIDER;
    config->ex_divider = TIKU_HTIMER_EX_DIVIDER;
    config->aclk_source = TIKU_ACLK_CONFIG_SOURCE;
    config->base_frequency = TIKU_HTIMER_BASE_FREQ;
    config->timer_frequency = TIKU_HTIMER_CALCULATED_FREQ;
}

#endif /* TIKU_HTIMER_CONFIG_H_ */
