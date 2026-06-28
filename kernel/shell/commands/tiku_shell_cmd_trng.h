/*
 * Tiku Operating System  -  http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_trng.h - "trng" command: dump hardware-TRNG bytes (hex).
 *
 * A diagnostic for the on-die true random number generator that backs the
 * cert-TLS handshake (RP2350 datasheet TRNG, or the Apollo CryptoCell-312).
 * `trng [n]` prints n (default 16, max 64) random bytes as hex.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_TRNG_H_
#define TIKU_SHELL_CMD_TRNG_H_

#include <stdint.h>

void tiku_shell_cmd_trng(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_TRNG_H_ */
