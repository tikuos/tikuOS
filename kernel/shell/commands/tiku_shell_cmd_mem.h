/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mem.h - "peek" and "poke" commands
 *
 * The two handlers share an address parser and a single .c so the
 * code-size cost is paid once.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_MEM_H_
#define TIKU_SHELL_CMD_MEM_H_

#include <stdint.h>

/**
 * @brief "peek" command -- read N bytes from a memory address.
 *
 * Usage:
 *   peek <addr> [count]      (count default 1, max 32)
 *
 * Address accepts decimal or 0x-prefixed hex.  Bytes are printed
 * as space-separated hex on a single line, prefixed by the
 * starting address.
 *
 * Address space: low 64 KB only (uintptr_t is 16-bit in the small
 * memory model used on the MSP430FR5969 build).  HIFRAM (>= 0x10000)
 * cannot be reached from this command; callers needing far reads
 * should add a `peek_far` variant with `__data20` pointers.
 *
 * Reads are subject to the active MPU rules: a read from a
 * region currently configured as no-access will fault.  Writing
 * is *not* gated by this command -- use `poke` for that, and only
 * after unlocking the destination region as appropriate.
 */
void tiku_shell_cmd_peek(uint8_t argc, const char *argv[]);

/**
 * @brief "poke" command -- write a single byte to an address.
 *
 * Usage:
 *   poke <addr> <byte>
 *
 * Both arguments accept decimal or 0x-prefixed hex.  The write is
 * a plain `*(volatile uint8_t *)addr = byte` -- subject to the
 * active MPU rules.  FRAM segments configured as read-only will
 * silently drop the write; callers writing to FRAM should bracket
 * the call with `tiku_mpu_unlock_nvm()` / `tiku_mpu_lock_nvm()` or
 * use the higher-level VFS `write` for nodes that already do.
 */
void tiku_shell_cmd_poke(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_MEM_H_ */
