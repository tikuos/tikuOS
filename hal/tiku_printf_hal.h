/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_printf_hal.h - platform routing for debug printf.
 *
 * Defines TIKU_PRINTF() per platform, selecting the low-level output channel
 * (semihosting, UART, USB CDC) and suppressing it where a transport owns the
 * link, as SLIP owns the UART.  A new port adds one #elif block.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PRINTF_HAL_H_
#define TIKU_PRINTF_HAL_H_

/**
 * @def TIKU_PRINTF(...)
 * @brief printf-style debug output on the platform's console channel.
 *
 * Compiles to nothing with no platform selected, and in MSP430 GCC builds of
 * the net app (TIKU_APP_NET), whose UART carries SLIP.
 */

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
/* The UART carries SLIP: debug printf compiles to nothing. */
#define TIKU_PRINTF(...)
#else
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)
#endif

#endif /* __TI_COMPILER_VERSION__ */

/*---------------------------------------------------------------------------*/
/* RP2350                                                                    */
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
/* AMBIQ APOLLO                                                              */
/*---------------------------------------------------------------------------*/

#elif defined(PLATFORM_AMBIQ)
/* Console over the COM UART. */
#include <arch/ambiq/tiku_uart_arch.h>
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)

/*---------------------------------------------------------------------------*/
/* NORDIC NRF54L                                                             */
/*---------------------------------------------------------------------------*/

#elif defined(PLATFORM_NORDIC)
/* Console over UARTE (polled EasyDMA); debug printf goes straight to the
 * UARTE backend. */
#include <arch/nordic/tiku_uart_arch.h>
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)

/*---------------------------------------------------------------------------*/
/* STM32N6                                                                   */
/*---------------------------------------------------------------------------*/

#elif defined(PLATFORM_STM32N6)
/* Console over USART1, the ST-LINK virtual COM port. */
#include <arch/stm32n6/tiku_uart_arch.h>
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)

/*---------------------------------------------------------------------------*/
/* RA8P1                                                                     */
/*---------------------------------------------------------------------------*/

#elif defined(PLATFORM_RA8P1)
/* Console over SCI8, the kit's J-Link OB virtual COM port. */
#include <arch/ra8p1/tiku_uart_arch.h>
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)

/*---------------------------------------------------------------------------*/
/* ESP32-C61                                                                 */
/*---------------------------------------------------------------------------*/

#elif defined(PLATFORM_ESP32C61)
/* Console over UART0, the DevKitC's CP2102N bridge. */
#include <arch/esp32c61/tiku_uart_arch.h>
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)

/*---------------------------------------------------------------------------*/
/* FALLBACK: NO PLATFORM, NO OUTPUT                                          */
/*---------------------------------------------------------------------------*/

#elif defined(PLATFORM_ESP32C5)
#include <arch/esp32c5/tiku_debug_arch.h>
#include <arch/esp32c5/tiku_uart_arch.h>
#define TIKU_PRINTF(...) tiku_debug_arch_printf(__VA_ARGS__)

#elif !defined(TIKU_PRINTF)
#define TIKU_PRINTF(...)
#endif

#endif /* TIKU_PRINTF_HAL_H_ */
