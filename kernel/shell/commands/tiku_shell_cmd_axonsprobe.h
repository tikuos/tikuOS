/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_axonsprobe.h - "axonsprobe" Axon NPU bring-up probe (opt-in)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_AXONSPROBE_H_
#define TIKU_SHELL_CMD_AXONSPROBE_H_

#include <stdint.h>                          /* uint8_t in the handler type */
#include <kernel/shell/tiku_shell_config.h>

#if TIKU_SHELL_CMD_AXONSPROBE
/**
 * @brief "axonsprobe" command handler — Axon NPU bring-up probe.
 *
 * Bare form prints the AXONS ENABLE/STATUS registers and the FICR identity.
 * Sub-commands: en, off, dump, diff, irq, plus hw/acc/fir when the vendor Axon
 * driver is built in.  Read-only: it never writes the engine window.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] selects the sub-command, argv[2..]
 *              carry its offset and word-count parameters
 */
/* Signature MUST be tiku_shell_handler_t (tiku_shell.h): the command table
 * stores it directly.  It read (int, char **) until the Axon checkout existed
 * to build against, so nothing ever instantiated the table entry and the
 * mismatch stayed invisible. */
void tiku_shell_cmd_axonsprobe(uint8_t argc, const char *argv[]);
#endif

#endif /* TIKU_SHELL_CMD_AXONSPROBE_H_ */
