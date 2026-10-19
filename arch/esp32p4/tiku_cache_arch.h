/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cache_arch.h - Boot-only P4 L2 cache/SRAM partition selection.
 *
 * The L2 cache takes the top 128 KiB of the 768 KiB HP SRAM, which the
 * linker scripts leave unused; tiku_crt_early.S selects it before main in
 * the kernel-test and firmware profiles.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_CACHE_ARCH_H_
#define TIKU_ESP32P4_CACHE_ARCH_H_
/**
 * @brief Select 128 KiB L2 with 64-byte lines; return zero on readback match.
 * @note CPU0, interrupts masked, CPU1 parked, low SRAM stack, no external
 *       memory accesses or live allocations. ROM ECO2 only.
 */
int tiku_p4_cache_boot_init(void);
/** @brief Non-zero when cache geometry matches the SRAM linker contract. */
int tiku_p4_cache_layout_ok(void);
/** @brief L2 size register sampled before boot-time partition selection. */
unsigned tiku_p4_cache_initial_size(void);
#endif
