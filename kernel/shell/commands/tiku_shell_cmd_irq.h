/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_irq.h - "irq" command: enable/disable GPIO edge IRQs
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_IRQ_H_
#define TIKU_SHELL_CMD_IRQ_H_

#include <stdint.h>

/**
 * @brief "irq" command: configure a per-pin edge interrupt.
 *
 * Takes P<port>.<pin> and one of rising, falling, both or off -- e.g.
 * `irq P1.3 falling` to wake on a button press to GND.  Each fired edge posts
 * TIKU_EVENT_GPIO to every process.
 */
void tiku_shell_cmd_irq(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_IRQ_H_ */
