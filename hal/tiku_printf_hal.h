/*
 * Tiku Operating System v0.01
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
/* Future platforms: Ambiq, Nordic, RISC-V, etc.                             */
/*---------------------------------------------------------------------------*/

/*
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_uart_arch.h>
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)

#elif defined(PLATFORM_NRF)
#include <arch/nrf/tiku_uart_arch.h>
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)
*/

/*---------------------------------------------------------------------------*/
/* Fallback: no platform defined — suppress output                           */
/*---------------------------------------------------------------------------*/

#elif !defined(TIKU_PRINTF)
#define TIKU_PRINTF(...)
#endif

#endif /* TIKU_PRINTF_HAL_H_ */
