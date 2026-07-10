/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_printf_hal.h - Platform-routing header for debug printf
 *
 * Defines TIKU_PRINTF() based on the active platform.  Each platform
 * block selects the correct low-level output channel (semihosting,
 * UART, RTT, etc.) and handles transport-conflict suppression
 * (e.g. SLIP owns the UART).
 *
 * Adding a new platform:
 *   1. Add an #elif block for PLATFORM_<NAME>
 *   2. Include the platform's console/UART header
 *   3. Map TIKU_PRINTF to the platform's printf function
 *   4. Handle any transport conflicts (SLIP, BLE, etc.)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PRINTF_HAL_H_
#define TIKU_PRINTF_HAL_H_

/*---------------------------------------------------------------------------*/
/* MSP430                                                                    */
/*---------------------------------------------------------------------------*/

#if defined(PLATFORM_MSP430)

#if defined(__TI_COMPILER_VERSION__)
/* TI CCS: printf() routes through CIO semihosting (JTAG debugger). */
#include <stdio.h>
#define TIKU_PRINTF(...) printf(__VA_ARGS__)

#else
/* GCC: tiku_uart_printf() routes through eUSCI_A backchannel UART. */
#include <arch/msp430/tiku_uart_arch.h>

#if defined(TIKU_APP_NET)
/* UART is dedicated to SLIP — suppress all debug printf. */
#define TIKU_PRINTF(...)
#else
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)
#endif

#endif /* __TI_COMPILER_VERSION__ */

/*---------------------------------------------------------------------------*/
/* Raspberry Pi RP2350 (Pico 2 / Pico 2 W)                                   */
/*---------------------------------------------------------------------------*/

#elif defined(PLATFORM_RP2350)

/* Console channel selectable at build time via the TIKU_CONSOLE make var:
 *   uart (default) -> hardware UART0 (external FT232)
 *   usb            -> native USB CDC-ACM on the programming connector
 *   both           -> mirror to UART and USB CDC                          */
#include <arch/arm-rp2350/tiku_uart_arch.h>
#if defined(TIKU_CONSOLE_BOTH)
#include <arch/arm-rp2350/tiku_usb_cdc_arch.h>
#define TIKU_PRINTF(...) \
    do { tiku_uart_printf(__VA_ARGS__); tiku_usb_cdc_printf(__VA_ARGS__); } \
    while (0)
#elif defined(TIKU_CONSOLE_USB)
#include <arch/arm-rp2350/tiku_usb_cdc_arch.h>
#define TIKU_PRINTF(...) tiku_usb_cdc_printf(__VA_ARGS__)
#else
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)
#endif

/*---------------------------------------------------------------------------*/
/* Ambiq Apollo 510 (Cortex-M55) — console over SWO/ITM                       */
/*---------------------------------------------------------------------------*/

#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_uart_arch.h>
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)

/*---------------------------------------------------------------------------*/
/* Fallback: no platform defined — suppress output                           */
/*---------------------------------------------------------------------------*/

#elif !defined(TIKU_PRINTF)
#define TIKU_PRINTF(...)
#endif

#endif /* TIKU_PRINTF_HAL_H_ */
