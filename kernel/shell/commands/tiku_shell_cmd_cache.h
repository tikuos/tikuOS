/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cache.h - "cache" command (STM32N6).
 *
 * Shows and toggles the CPU caches, times a fixed workload, and checks that a
 * DMA copy stays coherent with the data cache.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CACHE_H_
#define TIKU_SHELL_CMD_CACHE_H_

#include <stdint.h>

/**
 * @brief Shell command: show cache state, toggle it, run the benchmark or
 *        the DMA coherency check.
 *
 * @param argc  Argument count
 * @param argv  "on", "off", "bench", "dma", or nothing for the state
 * @note Defined only when PLATFORM_STM32N6 is set.
 */
void tiku_shell_cmd_cache(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CACHE_H_ */
