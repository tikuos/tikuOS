/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_irq.h - "irq" command: enable/disable GPIO edge IRQs
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_IRQ_H_
#define TIKU_SHELL_CMD_IRQ_H_

#include <stdint.h>

/**
 * @brief "irq" command: configure a per-pin edge interrupt.
 *
 * Usage:
 *   irq P<port>.<pin> <rising|falling|both|off>
 *
 * Examples:
 *   irq P1.3 falling     — wake on a button press to GND
 *   irq P2.0 rising      — wake on a sensor pulse
 *   irq P1.4 both        — wake on every transition
 *   irq P1.3 off         — disable
 *
 * Each fired edge posts TIKU_EVENT_GPIO to every process.
 */
void tiku_shell_cmd_irq(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_IRQ_H_ */
