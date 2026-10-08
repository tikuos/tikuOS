/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mem.h - "peek" and "poke" commands
 *
 * Both handlers share one address parser in tiku_shell_cmd_mem.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_MEM_H_
#define TIKU_SHELL_CMD_MEM_H_

#include <stdint.h>

/**
 * @brief "peek" command -- read N bytes from a memory address.
 *
 * Usage: peek <addr> [count].  Both take decimal or 0x-prefixed hex; count is
 * 1-32, default 1.  On MSP430 only the low 64 KB is reachable (not HIFRAM);
 * 32-bit ports take any address.  Reads are subject to the active MPU rules.
 */
void tiku_shell_cmd_peek(uint8_t argc, const char *argv[]);

/**
 * @brief "poke" command -- write a single byte to an address.
 *
 * Both arguments take decimal or 0x-prefixed hex, and the write is a plain
 * volatile store under the active MPU rules: MSP430's read-only FRAM drops
 * it, and a write-protected durable region faults.  Use `write` for NVM nodes.
 */
void tiku_shell_cmd_poke(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_MEM_H_ */
