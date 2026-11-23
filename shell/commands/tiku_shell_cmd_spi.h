/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_spi.h - "spi" command: transfers with a GPIO chip select
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_SPI_H_
#define TIKU_SHELL_CMD_SPI_H_

#include <stdint.h>

/**
 * @brief "spi" command -- master-mode transfers on the board's SPI bus.
 *
 * `xfer <port> <pin> <byte>...` drives the chip select on GPIO port/pin low,
 * sends the bytes and prints those received; `init <mode> <prescaler>` sets
 * the bus.  A closed bus is first initialised in mode 0 at prescaler 16.
 */
void tiku_shell_cmd_spi(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_SPI_H_ */
