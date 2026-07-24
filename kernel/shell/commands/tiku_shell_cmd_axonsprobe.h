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

#include <kernel/shell/tiku_shell_config.h>

#if TIKU_SHELL_CMD_AXONSPROBE
/**
 * @brief "axonsprobe" command handler — Axon NPU bring-up probe.
 *
 * With no sub-command it prints the AXONS ENABLE/STATUS registers and the
 * FICR identity.  Sub-commands: "en" (enable, spin for READY), "off"
 * (disable), "dump <off> <n>" (hex-dump n words from hex offset off),
 * "diff" (list the words that change across an enable), "irq" (arm IRQ 86
 * and count what fires).  When the vendor Axon driver is built in, also
 * "hw", "acc" and "fir".  Read-only: it never writes the engine window.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] selects the sub-command, argv[2..]
 *              carry its offset and word-count parameters
 */
void tiku_shell_cmd_axonsprobe(int argc, char **argv);
#endif

#endif /* TIKU_SHELL_CMD_AXONSPROBE_H_ */
