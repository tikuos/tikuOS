/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1_sha256.h - the A/B compute kernel both cores compile.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CPU1_SHA256_H_
#define TIKU_CPU1_SHA256_H_

#include <stdint.h>

/**
 * @brief Iterated SHA-256: digest feeds the next round's input.
 *
 * Compute-bound and sequential by construction, so the time it takes is
 * the silicon's, not the mailbox's.
 *
 * @param seed  40 input bytes
 * @param iters Rounds; 0 is treated as 1
 * @param out   The final digest
 */
void tiku_cpu1_sha256_chain(const uint8_t seed[40], uint32_t iters,
                            uint8_t out[32]);

#endif /* TIKU_CPU1_SHA256_H_ */
