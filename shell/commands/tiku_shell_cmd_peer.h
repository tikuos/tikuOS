/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_peer.h - "peer" command: the board's other processors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_PEER_H_
#define TIKU_SHELL_CMD_PEER_H_

#include <stdint.h>

/**
 * @brief "peer" command -- the peers of interfaces/peer/tiku_peer.h.
 *
 * With no arguments it lists each peer: role, carrier, state and services.
 * `speed <name> <bits>` asks for its link's speed.
 * @note `send <endpoint> <word>...`, `listen <endpoint> <seconds>` and
 *       `ping <endpoint> <count>` move messages on one of its endpoints.
 */
void tiku_shell_cmd_peer(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_PEER_H_ */
