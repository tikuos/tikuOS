/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_trng.h - "trng" command: dump entropy-source bytes as hex.
 *
 * A diagnostic for the random source behind the TLS handshake: a hardware
 * TRNG, or on MSP430 and RA8P1 a software source conditioned with SHA-256.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_TRNG_H_
#define TIKU_SHELL_CMD_TRNG_H_

#include <stdint.h>

/**
 * @brief "trng" command handler — dump entropy-source bytes as hex.
 *
 * Reads the platform's hardware TRNG, or on MSP430 and RA8P1 a software
 * entropy source conditioned with SHA-256, and prints the bytes as lowercase
 * hex on a single line.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] is the optional byte count
 *              (decimal, default 16, clamped to 64)
 */
void tiku_shell_cmd_trng(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_TRNG_H_ */
