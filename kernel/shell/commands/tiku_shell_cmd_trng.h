/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_trng.h - "trng" command: dump entropy-source bytes (hex).
 *
 * A diagnostic for the random-number source that backs the cert-TLS
 * handshake: a hardware TRNG where the die provides one (the RP2350
 * ring-oscillator TRNG, the Apollo CryptoCell-312), or a SHA-256-conditioned
 * software entropy source on parts without one (MSP430).  `trng [n]` prints
 * n (default 16, max 64) random bytes as hex.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_TRNG_H_
#define TIKU_SHELL_CMD_TRNG_H_

#include <stdint.h>

/**
 * @brief "trng" command handler — dump entropy-source bytes as hex.
 *
 * Reads from the platform hardware TRNG, or from the SHA-256-conditioned
 * software entropy source on parts without one, and prints the bytes as
 * lowercase hex on a single line.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] is the optional byte count
 *              (decimal, default 16, clamped to 64)
 */
void tiku_shell_cmd_trng(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_TRNG_H_ */
