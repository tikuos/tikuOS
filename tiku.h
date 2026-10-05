/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku.h - Main system header with platform and device configuration
 *
 * Top-level configuration header for the Tiku Operating System. Defines
 * platform selection, device configuration, debug flags, and test enables.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_H_
#define TIKU_H_

/*---------------------------------------------------------------------------*/
/* VERSION                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Kernel version string. */
#define TIKU_VERSION        "0.06"
/** @brief Project tagline. */
#define TIKU_TAGLINE        "Simple. Ubiquitous. Intelligence, Everywhere."

/*---------------------------------------------------------------------------*/
/* PLATFORM CONFIGURATION                                                   */
/*---------------------------------------------------------------------------*/

/*
 * The Makefile sets exactly one PLATFORM_* define on the command line.
 * With nothing set the build falls back to MSP430.
 */
#if !defined(PLATFORM_MSP430) && !defined(PLATFORM_RP2350) && \
    !defined(PLATFORM_AMBIQ) && !defined(PLATFORM_NORDIC) && \
    !defined(PLATFORM_STM32N6) && !defined(PLATFORM_RA8P1) && \
    !defined(PLATFORM_ESP32C61)
#define PLATFORM_MSP430 1
#endif

/*---------------------------------------------------------------------------*/
/* DEVICE SELECTION                                                          */
/*---------------------------------------------------------------------------*/

/*
 * Device selection, passed as -DTIKU_DEVICE_<PART>=1 from the Makefile.  CCS
 * passes -D__MSP430FRxxxx__ instead, so those are mapped automatically, and an
 * MSP430 build with no device named defaults to the FR2433.
 */
#if defined(PLATFORM_MSP430)

#if defined(__MSP430FR5969__) && !defined(TIKU_DEVICE_MSP430FR5969)
#define TIKU_DEVICE_MSP430FR5969 1
#elif defined(__MSP430FR5994__) && !defined(TIKU_DEVICE_MSP430FR5994)
#define TIKU_DEVICE_MSP430FR5994 1
#elif defined(__MSP430FR6989__) && !defined(TIKU_DEVICE_MSP430FR6989)
#define TIKU_DEVICE_MSP430FR6989 1
#elif defined(__MSP430FR2433__) && !defined(TIKU_DEVICE_MSP430FR2433)
#define TIKU_DEVICE_MSP430FR2433 1
#endif

#if !defined(TIKU_DEVICE_MSP430FR5969) && \
    !defined(TIKU_DEVICE_MSP430FR5994) && \
    !defined(TIKU_DEVICE_MSP430FR6989) && \
    !defined(TIKU_DEVICE_MSP430FR2433)
#define TIKU_DEVICE_MSP430FR2433 1
#endif

#elif defined(PLATFORM_RP2350)

/*
 * Raspberry Pi RP2350 (Pico 2, Pico 2 W).  One silicon variant; the
 * Makefile passes the board define (TIKU_BOARD_RPI_PICO2 or _PICO2_W)
 * that selects the board header.
 */
#ifndef TIKU_DEVICE_RP2350
#define TIKU_DEVICE_RP2350 1
#endif

#elif defined(PLATFORM_AMBIQ)

/*
 * Ambiq silicon.  The Makefile passes the device define derived from MCU --
 * TIKU_DEVICE_APOLLO510 or _APOLLO510B (Cortex-M55), _APOLLO4L or _APOLLO4P
 * (Cortex-M4F) -- with the matching board define alongside it.  Apollo510 is
 * the default only when no device was selected: the device-select router
 * checks APOLLO510 before APOLLO4L, so an unconditional default here would
 * build the wrong device.
 *
 * Every M4F device must be in the exclusion list, or the fallback would also
 * define APOLLO510 and pull Cortex-M55-only code (e.g. the ARMv8-M MPU diag)
 * into an M4F build, which fails to link.  Apollo510B is not listed: it is the
 * same M55 die and wants the APOLLO510 code paths.
 */
#if !defined(TIKU_DEVICE_APOLLO510) && !defined(TIKU_DEVICE_APOLLO4L) && \
    !defined(TIKU_DEVICE_APOLLO4P)
#define TIKU_DEVICE_APOLLO510 1
#endif

#elif defined(PLATFORM_NORDIC)

/*
 * Nordic nRF54L silicon. The Makefile derives one TIKU_DEVICE_NRF54* macro
 * from MCU=... and passes it on the command line, along with the matching
 * board define. With no nordic device selected the build falls back to the
 * nRF54L15, so a bare PLATFORM_NORDIC build still resolves a device.
 */
#if !defined(TIKU_DEVICE_NRF54L15) && !defined(TIKU_DEVICE_NRF54LM20A) && \
    !defined(TIKU_DEVICE_NRF54LM20B)
#define TIKU_DEVICE_NRF54L15 1
#endif

#endif /* PLATFORM_* */

/*---------------------------------------------------------------------------*/
/* SYSTEM CONFIGURATION                                                      */
/*---------------------------------------------------------------------------*/

/* Defined before the includes below, which depend on them. */

/**
 * @brief Clock time type: 16 bits on MSP430, 32 bits elsewhere.
 *
 * 16 bits wraps every 512 s at 128 Hz, past which a single-difference interval
 * is wrong; MSP430 keeps it because every timer compare pays for the width.
 * 32 bits moves the wrap to 388 days.  TIKU_CLOCK_LT / _DIFF follow the type.
 */
#if defined(PLATFORM_MSP430)
#define TIKU_CLOCK_CONF_TIME_T unsigned short
#else
#define TIKU_CLOCK_CONF_TIME_T unsigned long
#endif

/**
 * @brief Boot CPU frequency, passed to tiku_cpu_full_init().
 *
 * On MSP430 an index into the DCO presets (1 = 1 MHz .. 7 = 8 MHz; 8, for
 * 16 MHz, is clamped to 8 MHz); on the other ports the boot rate in MHz.
 */
#if defined(PLATFORM_RP2350)
/* RP2350 PLL_SYS target in MHz. Supported values (see
 * arch/arm-rp2350/tiku_cpu_freq_boot_arch.c rp2350_freq_table[]):
 *   12, 48, 100, 125, 133, 150
 * Anything else keeps the 150 MHz boot clock and sets the clock-fault flag. */
#ifndef MAIN_CPU_FREQ
#define MAIN_CPU_FREQ 150
#endif
#elif defined(PLATFORM_AMBIQ)
/* Apollo: 96 boots the core in Low-Power mode at 96 MHz; a larger value asks
 * tiku_cpu_freq_init() for turbo (250 MHz on Apollo510, 192 MHz on Apollo4).
 * The tick runs from the STIMER.  TIKU_MAIN_CPU_HZ sets the reload of the
 * free-running SysTick that the busy-delays count, and they scale by the live
 * core clock, which tiku_cpu_ambiq_clock_get_hz() reads from the perf-mode
 * register. */
#ifndef MAIN_CPU_FREQ
#define MAIN_CPU_FREQ 96
#endif
#elif defined(PLATFORM_NORDIC)
/* nRF54L: tiku_cpu_freq_init() ignores this value.  The core runs at
 * TIKU_NORDIC_CPU_MHZ (128 by default) or the saved choice, set once by
 * tiku_cpu_boot_nordic_init(); delays read the live PLL rate, and the tick
 * runs from the GRTC.  128 keeps TIKU_MAIN_CPU_HZ at the default rate. */
#ifndef MAIN_CPU_FREQ
#define MAIN_CPU_FREQ 128
#endif
#elif defined(PLATFORM_RA8P1)
/* Boot rate, applied by tiku_cpu_freq_init(): PLL1 comes up at 240 MHz, and
 * `freq` moves between 240, 480 and 1000 at run time. */
#ifndef MAIN_CPU_FREQ
#define MAIN_CPU_FREQ 240
#endif
#elif defined(PLATFORM_STM32N6)
/* Boot rate, applied by tiku_cpu_freq_init(): 150 MHz is what the boot ROM
 * hands over, so booting here changes nothing but puts the clock tree under
 * the kernel's own setup.  `freq <mhz>` moves it up to 800 or down to 10 at
 * runtime; the tick and console run from HSI and do not follow. */
#ifndef MAIN_CPU_FREQ
#define MAIN_CPU_FREQ 150
#endif
#elif defined(PLATFORM_ESP32C61)
/* Boot rate, applied by tiku_cpu_freq_init(): 160 MHz is the PLL rate the
 * ROM already runs at.  The tick and the console run from the crystal and
 * do not follow a later `freq`. */
#ifndef MAIN_CPU_FREQ
#define MAIN_CPU_FREQ 160
#endif
#else
#define MAIN_CPU_FREQ 7    /* MSP430: 8 MHz (maximum supported) */
#endif

/**
 * @brief MAIN_CPU_FREQ in Hz, for the htimer and other subsystems that need
 *        the clock frequency as a compile-time constant.
 */
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
    defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6) || \
    defined(PLATFORM_RA8P1) || defined(PLATFORM_ESP32C61)
#define TIKU_MAIN_CPU_HZ  ((unsigned long)MAIN_CPU_FREQ * 1000000UL)
#elif MAIN_CPU_FREQ == 1
#define TIKU_MAIN_CPU_HZ  1000000UL
#elif MAIN_CPU_FREQ == 2
#define TIKU_MAIN_CPU_HZ  2670000UL
#elif MAIN_CPU_FREQ == 3
#define TIKU_MAIN_CPU_HZ  3500000UL
#elif MAIN_CPU_FREQ == 4
#define TIKU_MAIN_CPU_HZ  4000000UL
#elif MAIN_CPU_FREQ == 5
#define TIKU_MAIN_CPU_HZ  5330000UL
#elif MAIN_CPU_FREQ == 6
#define TIKU_MAIN_CPU_HZ  7000000UL
#elif MAIN_CPU_FREQ == 7
#define TIKU_MAIN_CPU_HZ  8000000UL
#elif MAIN_CPU_FREQ == 8
/* The clock code clamps the 16 MHz preset to 8 MHz, so the rate is 8 MHz. */
#define TIKU_MAIN_CPU_HZ  8000000UL
#else
#error "Unknown MAIN_CPU_FREQ value"
#endif

/*---------------------------------------------------------------------------*/
/* SYSTEM INCLUDES                                                          */
/*---------------------------------------------------------------------------*/

#include <stddef.h>   /* NULL */

#if defined(PLATFORM_MSP430)
#include <msp430.h>                            /* MSP430-specific header */
#include <arch/msp430/tiku_device_select.h>    /* Device + board headers */
#elif defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_device_select.h>
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_device_select.h>
#elif defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_device_select.h>
#elif defined(PLATFORM_STM32N6)
#include <arch/stm32n6/tiku_device_select.h>
#elif defined(PLATFORM_RA8P1)
#include <arch/ra8p1/tiku_device_select.h>
#elif defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_device_select.h>
#endif

/*---------------------------------------------------------------------------*/
/* PLATFORM-ROUTED PRINTF                                                    */
/*---------------------------------------------------------------------------*/

/*
 * TIKU_PRINTF() routes through hal/tiku_printf_hal.h, which picks the output
 * channel for the active platform and compiler and resolves transport conflicts
 * such as SLIP over the console UART.
 */
#include <hal/tiku_printf_hal.h>

/*---------------------------------------------------------------------------*/
/* TEST CONFIGURATION                                                       */
/*---------------------------------------------------------------------------*/
/* Included early so that feature-gate macros (e.g. TIKU_LC_PERSISTENT) are
 * visible to the kernel headers below. */

#if defined(HAS_TESTS)
#include <tests/tiku_test_config.h>
#else
/** @brief 1 when a test build enables the test runner; 0 otherwise. */
#define TEST_ENABLE 0
#endif

/*---------------------------------------------------------------------------*/
/* TIKU OS INCLUDES                                                         */
/*---------------------------------------------------------------------------*/
#include <hal/tiku_cpu.h>
#include <kernel/cpu/tiku_common.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <kernel/process/tiku_process.h>
#include <kernel/process/tiku_proto.h>
#if defined(PLATFORM_MSP430)
#include <arch/msp430/tiku_timer_arch.h>     /* TIKU_CLOCK_ARCH_SECOND et al. */
#elif defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_timer_arch.h>
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_timer_arch.h>
#elif defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_timer_arch.h>
#elif defined(PLATFORM_STM32N6)
#include <arch/stm32n6/tiku_timer_arch.h>
#elif defined(PLATFORM_RA8P1)
#include <arch/ra8p1/tiku_timer_arch.h>
#elif defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_timer_arch.h>
#endif
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_htimer.h>
#include <kernel/timers/tiku_timer.h>
#include <interfaces/bus/tiku_i2c_bus.h>
#include <interfaces/bus/tiku_spi_bus.h>
#include <interfaces/adc/tiku_adc.h>
#include <interfaces/onewire/tiku_onewire.h>

/*---------------------------------------------------------------------------*/
/* SHELL CONFIGURATION                                                       */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_SHELL_ENABLE
/**
 * @brief 1 to build the shell (make TIKU_SHELL_ENABLE=1).  It is a kernel
 *        service, so it coexists with tests, examples or an app.
 */
#define TIKU_SHELL_ENABLE 0
#endif

#if TIKU_SHELL_ENABLE
#include <kernel/shell/tiku_shell_config.h>
#endif

/*---------------------------------------------------------------------------*/
/* INIT SYSTEM                                                               */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_INIT_ENABLE
/**
 * @brief 1 to build the init system: shell commands kept in NVM and run at
 *        boot through the shell parser, so it needs the shell.
 */
#define TIKU_INIT_ENABLE 0
#endif

/*---------------------------------------------------------------------------*/
/* EXAMPLE CONFIGURATION                                                    */
/*---------------------------------------------------------------------------*/

#if defined(HAS_EXAMPLES)
#include <examples/tiku_example_config.h>
#else
/** @brief 1 when the examples config enables an example; 0 otherwise. */
#define TIKU_EXAMPLES_ENABLE 0
#endif

/*---------------------------------------------------------------------------*/
/* APP CONFIGURATION                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief App master switch.  No app selection is compiled into the kernel (the
 *        app firmware lives in the TikuBench harness), so it is 0.
 */
#define TIKU_APPS_ENABLE 0

/*---------------------------------------------------------------------------*/
/* MUTUAL EXCLUSION OF TESTS, EXAMPLES AND APPS                              */
/*---------------------------------------------------------------------------*/

#if (!!TEST_ENABLE + !!TIKU_EXAMPLES_ENABLE + !!TIKU_APPS_ENABLE) > 1
#error "Only one of TEST_ENABLE, TIKU_EXAMPLES_ENABLE, TIKU_APPS_ENABLE may be set"
#endif

/*---------------------------------------------------------------------------*/
/* DEBUG CONFIGURATION                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @defgroup TIKU_DEBUG_CONFIG Debug Configuration Flags
 * @brief Per-subsystem debug output switches.
 *
 * Each subsystem has its own <NAME>_PRINTF() macro gated by its own flag.  All
 * are 0 by default; set one to 1 to enable that subsystem's output.
 * @{
 */

/** Enable debug printing for process management */
#define DEBUG_PROCESS 0

/** Enable debug printing for hardware timer */
#define DEBUG_HTIMER 0

/** Enable debug printing for CPU frequency */
#define DEBUG_CPU_FREQ 0

/** Enable debug printing for main application */
#define DEBUG_MAIN 0

/** Enable debug printing for timer subsystem */
#define DEBUG_TIMER 0

/** Enable debug printing for clock architecture */
#define DEBUG_CLOCK_ARCH 0

/** Enable debug printing for test modules (#ifndef so -DDEBUG_TESTS=1 wins) */
#ifndef DEBUG_TESTS
#define DEBUG_TESTS 0
#endif

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
/* ADDITIONAL SYSTEM CONFIGURATION                                          */
/*---------------------------------------------------------------------------*/

/** Enable autostart process functionality */
#define TIKU_AUTOSTART_ENABLE 1

#endif /* TIKU_H_ */
