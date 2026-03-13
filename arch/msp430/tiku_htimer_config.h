/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - Hardware timer configuration for MSP430
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file tiku_htimer_config.h
 * @brief Hardware timer configuration for MSP430FR5969
 */

#ifndef TIKU_HTIMER_CONFIG_H_
#define TIKU_HTIMER_CONFIG_H_

#include <msp430.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TIMER CLOCK SOURCE OPTIONS                                                */
/*---------------------------------------------------------------------------*/

/** Available clock sources for Timer A */
#define TIKU_HTIMER_SOURCE_SMCLK     0  /* SMCLK - Most accurate, higher power */
#define TIKU_HTIMER_SOURCE_ACLK      1  /* ACLK - Low power, less accurate */
#define TIKU_HTIMER_SOURCE_EXTERNAL  2  /* External clock input */
#define TIKU_HTIMER_SOURCE_INCLK     3  /* INCLK (device specific) */

/** Available ACLK sources */
#define TIKU_ACLK_SOURCE_VLOCLK      0  /* Internal VLO (~10kHz, varies widely) */
#define TIKU_ACLK_SOURCE_XT1CLK      1  /* External crystal (32.768kHz typical) */
#define TIKU_ACLK_SOURCE_REFOCLK     2  /* Internal reference (~32.768kHz) */

/** Timer divider options */
#define TIKU_HTIMER_DIV_1            0  /* No division */
#define TIKU_HTIMER_DIV_2            1  /* Divide by 2 */
#define TIKU_HTIMER_DIV_4            2  /* Divide by 4 */
#define TIKU_HTIMER_DIV_8            3  /* Divide by 8 */

/** Extended divider options (additional division) */
#define TIKU_HTIMER_EXDIV_1          0  /* No extended division */
#define TIKU_HTIMER_EXDIV_2          1  /* Additional divide by 2 */
#define TIKU_HTIMER_EXDIV_3          2  /* Additional divide by 3 */
#define TIKU_HTIMER_EXDIV_4          3  /* Additional divide by 4 */
#define TIKU_HTIMER_EXDIV_5          4  /* Additional divide by 5 */
#define TIKU_HTIMER_EXDIV_6          5  /* Additional divide by 6 */
#define TIKU_HTIMER_EXDIV_7          6  /* Additional divide by 7 */
#define TIKU_HTIMER_EXDIV_8          7  /* Additional divide by 8 */

/*---------------------------------------------------------------------------*/
/* USER CONFIGURATION SECTION                                                */
/*---------------------------------------------------------------------------*/

/**
 * SELECT YOUR CONFIGURATION HERE
 * Uncomment one of the preset configurations or create your own
 */

/* Default: High Accuracy Mode - 1 MHz timer */
#define TIKU_HTIMER_CONFIG_HIGH_ACCURACY
// #define TIKU_HTIMER_CONFIG_BALANCED
// #define TIKU_HTIMER_CONFIG_LOW_POWER
// #define TIKU_HTIMER_CONFIG_ULTRA_LOW_POWER
// #define TIKU_HTIMER_CONFIG_CUSTOM

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
/* Low power mode: ACLK from VLO @ ~10 kHz */
#define TIKU_HTIMER_CLOCK_SOURCE     TIKU_HTIMER_SOURCE_ACLK
#define TIKU_HTIMER_DIVIDER          TIKU_HTIMER_DIV_1
#define TIKU_HTIMER_EX_DIVIDER       TIKU_HTIMER_EXDIV_1
#define TIKU_HTIMER_BASE_FREQ        10000UL  /* ~10 kHz VLO (nominal) */
#define TIKU_ACLK_CONFIG_SOURCE      TIKU_ACLK_SOURCE_VLOCLK

#elif defined(TIKU_HTIMER_CONFIG_ULTRA_LOW_POWER)
/* Ultra-low power: ACLK from VLO @ ~10 kHz (varies!) */
#define TIKU_HTIMER_CLOCK_SOURCE     TIKU_HTIMER_SOURCE_ACLK
#define TIKU_HTIMER_DIVIDER          TIKU_HTIMER_DIV_1
#define TIKU_HTIMER_EX_DIVIDER       TIKU_HTIMER_EXDIV_1
#define TIKU_HTIMER_BASE_FREQ        10000UL  /* ~10 kHz VLO (nominal) */
#define TIKU_ACLK_CONFIG_SOURCE      TIKU_ACLK_SOURCE_VLOCLK
#warning "VLO frequency varies widely (4-20kHz). Timing will be inaccurate!"

#elif defined(TIKU_HTIMER_CONFIG_CUSTOM)
/* Custom configuration - define your own values */
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

/** Calculate divider values */
#define TIKU_HTIMER_DIV_VALUE    (1 << TIKU_HTIMER_DIVIDER)
#define TIKU_HTIMER_EXDIV_VALUE  (TIKU_HTIMER_EX_DIVIDER + 1)

/** Calculate final timer frequency */
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

/* Convert configuration to MSP430 register values */
#if TIKU_HTIMER_CLOCK_SOURCE == TIKU_HTIMER_SOURCE_SMCLK
#define TIKU_HTIMER_TASSEL_VALUE TASSEL__SMCLK
#elif TIKU_HTIMER_CLOCK_SOURCE == TIKU_HTIMER_SOURCE_ACLK
#define TIKU_HTIMER_TASSEL_VALUE TASSEL__ACLK
#elif TIKU_HTIMER_CLOCK_SOURCE == TIKU_HTIMER_SOURCE_EXTERNAL
#define TIKU_HTIMER_TASSEL_VALUE TASSEL__TACLK
#elif TIKU_HTIMER_CLOCK_SOURCE == TIKU_HTIMER_SOURCE_INCLK
#define TIKU_HTIMER_TASSEL_VALUE TASSEL__INCLK
#endif

/* Divider register values */
#if TIKU_HTIMER_DIVIDER == TIKU_HTIMER_DIV_1
#define TIKU_HTIMER_ID_VALUE ID__1
#elif TIKU_HTIMER_DIVIDER == TIKU_HTIMER_DIV_2
#define TIKU_HTIMER_ID_VALUE ID__2
#elif TIKU_HTIMER_DIVIDER == TIKU_HTIMER_DIV_4
#define TIKU_HTIMER_ID_VALUE ID__4
#elif TIKU_HTIMER_DIVIDER == TIKU_HTIMER_DIV_8
#define TIKU_HTIMER_ID_VALUE ID__8
#endif

/* Extended divider register values */
#define TIKU_HTIMER_TAIDEX_VALUE TIKU_HTIMER_EX_DIVIDER

/*---------------------------------------------------------------------------*/
/* RUNTIME CONFIGURATION STRUCTURE                                           */
/*---------------------------------------------------------------------------*/

typedef struct {
    uint8_t  clock_source;     /* TIKU_HTIMER_SOURCE_* */
    uint8_t  divider;          /* TIKU_HTIMER_DIV_* */
    uint8_t  ex_divider;       /* TIKU_HTIMER_EXDIV_* */
    uint8_t  aclk_source;      /* TIKU_ACLK_SOURCE_* */
    uint32_t base_frequency;   /* Base clock frequency in Hz */
    uint32_t timer_frequency;  /* Calculated timer frequency in Hz */
} tiku_htimer_config_t;

/** Get current configuration */
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
