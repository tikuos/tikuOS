/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cryptoprobe.h - CRACEN bring-up probe (opt-in).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CRYPTOPROBE_H_
#define TIKU_SHELL_CMD_CRYPTOPROBE_H_

#include <kernel/shell/tiku_shell_config.h>

#if TIKU_SHELL_CMD_CRYPTOPROBE
/**
 * @brief "cryptoprobe" command handler — CRACEN CryptoMaster probe.
 *
 * Sub-commands: "hwcfg" (dump the fused-engine words), "sha <hexcfg>"
 * (hash "abc" with that BA413 config word), "sweep" (search the config
 * words for one that reproduces SHA-256("abc")), "ecb", "ccm",
 * "gcm [hexextra]" and "pk" (known-answer tests), "bench" (hardware vs
 * software SHA-256 over 4 KB), "bypass" and "direct" (DMA path probes),
 * "mode <sw|auto>", "counters" and "dbg".  Anything else prints usage.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] selects the sub-command, argv[2]
 *              carries its hex config word or mode name
 */
void tiku_shell_cmd_cryptoprobe(int argc, char **argv);
#endif

#endif /* TIKU_SHELL_CMD_CRYPTOPROBE_H_ */
