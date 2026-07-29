/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mem.h - "peek" and "poke" commands
 *
 * The two handlers share an address parser and a single .c so the
 * code-size cost is paid once.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_MEM_H_
#define TIKU_SHELL_CMD_MEM_H_

#include <stdint.h>

/**
 * @brief "peek" command -- read N bytes from a memory address.
 *
 * Address takes decimal or 0x-prefixed hex, count defaults to 1 and caps at
 * 32.  Low 64 KB only, since uintptr_t is 16-bit in the small memory model, so
 * HIFRAM is out of reach.  Reads are subject to the active MPU rules.
 */
void tiku_shell_cmd_peek(uint8_t argc, const char *argv[]);

/**
 * @brief "poke" command -- write a single byte to an address.
 *
 * Both arguments take decimal or 0x-prefixed hex, and the write is a plain
 * volatile store subject to the active MPU rules -- a read-only FRAM segment
 * silently drops it, so bracket the call with the NVM unlock or use `write`.
 */
void tiku_shell_cmd_poke(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_MEM_H_ */
