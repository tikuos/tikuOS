/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cryptoprobe.h - crypto engine probe (opt-in).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CRYPTOPROBE_H_
#define TIKU_SHELL_CMD_CRYPTOPROBE_H_

#include <stdint.h>                          /* uint8_t in the handler type */
#include <shell/tiku_shell_config.h>

#if TIKU_SHELL_CMD_CRYPTOPROBE
/**
 * @brief "cryptoprobe" command handler — hardware crypto engine probe.
 *
 * On every port: sha, ecb, gcm, bench, mode and counters.  On Nordic also
 * hwcfg, sha <cfg>, sweep, ccm, pk, the bypass/direct DMA probes and dbg.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] selects the sub-command, argv[2]
 *              carries its hex config word or mode name
 * @note The signature must match tiku_shell_handler_t (tiku_shell.h): the
 *       command table stores it directly.
 */
void tiku_shell_cmd_cryptoprobe(uint8_t argc, const char *argv[]);
#endif

#endif /* TIKU_SHELL_CMD_CRYPTOPROBE_H_ */
