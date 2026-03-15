/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - UART backend for printf (MSP430 architecture)
 *
 * Provides a compiler-aware UART printf backend:
 *   - GCC (msp430-elf-gcc): Routes printf through eUSCI_A0 UART
 *     using the LaunchPad backchannel (USB-to-serial on debugger).
 *   - CCS (cl430): No-op; CIO semihosting handles printf via JTAG.
 *
 * Call tiku_uart_init() during boot, after clock and GPIO are ready.
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

#ifndef TIKU_UART_ARCH_H_
#define TIKU_UART_ARCH_H_

/**
 * @brief Initialize the UART peripheral for printf output.
 *
 * Under GCC: configures eUSCI_A0 for 9600 baud @ 8 MHz SMCLK
 * using the board-specific backchannel UART pins.
 * Under CCS: no-op (CIO handles printf).
 */
void tiku_uart_init(void);

/**
 * @brief Transmit a single character over UART.
 *
 * Under GCC: blocking write to eUSCI_A0 TX buffer.
 * Under CCS: no-op.
 *
 * @param c Character to transmit
 */
void tiku_uart_putc(char c);

/**
 * @brief Transmit a null-terminated string over UART.
 *
 * Converts bare '\\n' to '\\r\\n' for terminal compatibility.
 *
 * @param s String to transmit
 */
void tiku_uart_puts(const char *s);

/**
 * @brief Lightweight printf replacement for UART output.
 *
 * Supports: %s, %d, %u, %x, %c, %%, optional field width (e.g. %4d),
 * and long modifier (e.g. %ld, %4ld). Lightweight (~60 bytes of stack).
 */
void tiku_uart_printf(const char *fmt, ...);

#endif /* TIKU_UART_ARCH_H_ */
