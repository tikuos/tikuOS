/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku.h - Main system header with platform and device configuration
 *
 * Top-level configuration header for the Tiku Operating System. Defines
 * platform selection, device configuration, debug flags, and test enables.
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

#ifndef TIKU_H_
#define TIKU_H_

/*---------------------------------------------------------------------------*/
/* PLATFORM CONFIGURATION                                                   */
/*---------------------------------------------------------------------------*/

#ifndef PLATFORM_MSP430
#define PLATFORM_MSP430 1
#endif

/*---------------------------------------------------------------------------*/
/* DEVICE SELECTION                                                          */
/*---------------------------------------------------------------------------*/

/**
 * Device selection: passed via -DTIKU_DEVICE_MSP430FRxxxx=1 from the Makefile.
 * CCS passes -D__MSP430FRxxxx__ instead, so map those automatically.
 * Default to FR2433 if nothing is defined.
 */
#if defined(__MSP430FR5969__) && !defined(TIKU_DEVICE_MSP430FR5969)
#define TIKU_DEVICE_MSP430FR5969 1
#elif defined(__MSP430FR5994__) && !defined(TIKU_DEVICE_MSP430FR5994)
#define TIKU_DEVICE_MSP430FR5994 1
#elif defined(__MSP430FR2433__) && !defined(TIKU_DEVICE_MSP430FR2433)
#define TIKU_DEVICE_MSP430FR2433 1
#endif

#if !defined(TIKU_DEVICE_MSP430FR5969) && \
    !defined(TIKU_DEVICE_MSP430FR5994) && \
    !defined(TIKU_DEVICE_MSP430FR2433)
#define TIKU_DEVICE_MSP430FR2433 1
#endif

/*---------------------------------------------------------------------------*/
/* SYSTEM CONFIGURATION (before includes to avoid circular dependencies)    */
/*---------------------------------------------------------------------------*/

/** Clock time type definition */
#define TIKU_CLOCK_CONF_TIME_T unsigned short

/*---------------------------------------------------------------------------*/
/* SYSTEM INCLUDES                                                          */
/*---------------------------------------------------------------------------*/

#include <msp430.h>   /* MSP430 specific header file */
#include <stddef.h>   /* NULL */

#include <arch/msp430/tiku_device_select.h> /* Device + board headers */

/*---------------------------------------------------------------------------*/
/* COMPILER-AWARE PRINTF                                                     */
/*---------------------------------------------------------------------------*/

/**
 * Under CCS: printf() routes through CIO semihosting (JTAG debugger).
 * Under GCC: tiku_uart_printf() routes through eUSCI_A0 backchannel UART.
 */
#if defined(__TI_COMPILER_VERSION__)
#include <stdio.h>
#define TIKU_PRINTF(...) printf(__VA_ARGS__)
#else
#include <arch/msp430/tiku_uart_arch.h>
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)
#endif

/*---------------------------------------------------------------------------*/
/* TIKU OS INCLUDES                                                         */
/*---------------------------------------------------------------------------*/
#include <hal/tiku_cpu.h>
#include <kernel/cpu/tiku_common.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <kernel/process/tiku_process.h>
#include <kernel/process/tiku_proto.h>
#include <arch/msp430/tiku_timer_arch.h> /* TIKU_CLOCK_ARCH_SECOND et al. */
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_htimer.h>
#include <kernel/timers/tiku_timer.h>
#include <interfaces/bus/tiku_i2c_bus.h>
#include <interfaces/bus/tiku_spi_bus.h>
#include <interfaces/adc/tiku_adc.h>

/*---------------------------------------------------------------------------*/
/* TEST CONFIGURATION                                                       */
/*---------------------------------------------------------------------------*/

#include <tests/tiku_test_config.h>

/*---------------------------------------------------------------------------*/
/* EXAMPLE CONFIGURATION                                                    */
/*---------------------------------------------------------------------------*/

#include <examples/tiku_example_config.h>

/*---------------------------------------------------------------------------*/
/* DEBUG CONFIGURATION                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @defgroup TIKU_DEBUG_CONFIG Debug Configuration Flags
 * @brief Configuration flags for enabling/disabling debug output
 *
 * Each subsystem has its own debug macro following the pattern:
 * - MAIN_PRINTF()       - Main application debug
 * - PROCESS_PRINTF()    - Process management debug
 * - HTIMER_PRINTF()     - Hardware timer debug
 * - TIMER_PRINTF()      - Event timer debug
 * - CPU_FREQ_PRINTF()   - CPU frequency debug
 * - CLOCK_PRINTF()      - Clock architecture debug
 * - TEST_PRINTF()       - Test module debug
 * - HTIMER_ARCH_PRINTF()- Hardware timer arch debug
 * - SCHED_PRINTF()      - Scheduler debug
 * - WDT_PRINTF()        - Watchdog timer debug
 *
 * All debug messages are disabled by default (flags set to 0).
 * Set any flag to 1 to enable debug output for that subsystem.
 * @{
 */

/** Enable debug printing for process management */
#define DEBUG_PROCESS 1

/** Enable debug printing for hardware timer */
#define DEBUG_HTIMER 0

/** Enable debug printing for CPU frequency */
#define DEBUG_CPU_FREQ 0

/** Enable debug printing for main application */
#define DEBUG_MAIN 1

/** Enable debug printing for timer subsystem */
#define DEBUG_TIMER 0

/** Enable debug printing for clock architecture */
#define DEBUG_CLOCK_ARCH 0

/** Enable debug printing for test modules */
#define DEBUG_TESTS 1

/** Enable debug printing for scheduler */
#define DEBUG_SCHED 0

/** Enable debug printing for watchdog timer */
#define DEBUG_WDT 0

/** Enable debug printing for I2C bus */
#define DEBUG_I2C 0

/** Enable debug printing for SPI bus */
#define DEBUG_SPI 0

/** @} */ /* End of TIKU_DEBUG_CONFIG group */

/*---------------------------------------------------------------------------*/
/* DEBUG MACROS                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @defgroup TIKU_DEBUG_MACROS Debug Output Macros
 * @brief Unified debug output macros for all subsystems
 * @{
 */

#if DEBUG_MAIN
#define MAIN_PRINTF(...) TIKU_PRINTF("[MAIN] " __VA_ARGS__)
#else
#define MAIN_PRINTF(...)
#endif

#if DEBUG_PROCESS
#define PROCESS_PRINTF(...) TIKU_PRINTF("[PROCESS] " __VA_ARGS__)
#else
#define PROCESS_PRINTF(...)
#endif

#if DEBUG_HTIMER
#define HTIMER_PRINTF(...) TIKU_PRINTF("[HTIMER] " __VA_ARGS__)
#else
#define HTIMER_PRINTF(...)
#endif

#if DEBUG_TIMER
#define TIMER_PRINTF(...) TIKU_PRINTF("[TIMER] " __VA_ARGS__)
#else
#define TIMER_PRINTF(...)
#endif

#if DEBUG_CPU_FREQ
#define CPU_FREQ_PRINTF(...) TIKU_PRINTF("[CPU_FREQ] " __VA_ARGS__)
#else
#define CPU_FREQ_PRINTF(...)
#endif

#if DEBUG_CLOCK_ARCH
#define CLOCK_ARCH_PRINTF(...) TIKU_PRINTF("[CLOCK_ARCH] " __VA_ARGS__)
#else
#define CLOCK_ARCH_PRINTF(...)
#endif

#if DEBUG_TESTS
#define TEST_PRINTF(...) TIKU_PRINTF("[TEST] " __VA_ARGS__)
#else
#define TEST_PRINTF(...)
#endif

#if DEBUG_HTIMER
#define HTIMER_ARCH_PRINTF(...) TIKU_PRINTF("[HTIMER_ARCH] " __VA_ARGS__)
#else
#define HTIMER_ARCH_PRINTF(...)
#endif

#if DEBUG_AES
#define AES_PRINTF(...) TIKU_PRINTF("[AES] " __VA_ARGS__)
#else
#define AES_PRINTF(...)
#endif

#if DEBUG_SCHED
#define SCHED_PRINTF(...) TIKU_PRINTF("[SCHED] " __VA_ARGS__)
#else
#define SCHED_PRINTF(...)
#endif

#if DEBUG_WDT
#define WDT_PRINTF(...) TIKU_PRINTF("[WDT] " __VA_ARGS__)
#else
#define WDT_PRINTF(...)
#endif

#if DEBUG_I2C
#define I2C_PRINTF(...) TIKU_PRINTF("[I2C] " __VA_ARGS__)
#else
#define I2C_PRINTF(...)
#endif

#if DEBUG_SPI
#define SPI_PRINTF(...) TIKU_PRINTF("[SPI] " __VA_ARGS__)
#else
#define SPI_PRINTF(...)
#endif

/** @} */ /* End of TIKU_DEBUG_MACROS group */

/*---------------------------------------------------------------------------*/
/* SYSTEM CONFIGURATION                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @defgroup TIKU_SYS_CONFIG System Configuration
 * @brief Core system configuration parameters
 * @{
 */

/** Enable autostart process functionality */
#define TIKU_AUTOSTART_ENABLE 1

/** Target CPU frequency setting (enum value, not MHz):
 * 1=1MHz, 2=2.67MHz, 3=3.5MHz, 4=4MHz, 5=5.33MHz, 6=7MHz, 7=8MHz (max)
 * NOTE: 16MHz (value 8) is currently DISABLED due to stability issues.
 * Maximum supported frequency is 8MHz for MSP430FR5969.
 */
#define MAIN_CPU_FREQ 7 /* 8 MHz (maximum supported) */

/** @} */ /* End of TIKU_SYS_CONFIG group */

#endif /* TIKU_H_ */
