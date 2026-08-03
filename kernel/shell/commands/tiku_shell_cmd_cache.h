/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cache.h - "cache" command (STM32N6).
 *
 * State, on/off toggling and a fixed timed workload for the CPU caches, so a
 * coherency suspect can be tested against the uncached truth in seconds.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CACHE_H_
#define TIKU_SHELL_CMD_CACHE_H_

#include <stdint.h>

/**
 * @brief Shell command: show cache state, toggle it, or run the benchmark.
 *
 * @param argc  Argument count
 * @param argv  "on", "off", "bench", or nothing for the state
 */
void tiku_shell_cmd_cache(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CACHE_H_ */
