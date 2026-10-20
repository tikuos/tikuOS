/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sleep_arch.h - ESP32-P4 deep and light sleep, and the LP timer.
 *
 * Deep sleep powers the HP domain down and keeps the LP domain (LP timer,
 * PMU, LP SRAM, LP core); its wake is a reset.  Light sleep stalls the core
 * with every domain and clock kept, and returns with RAM and time intact.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32P4_SLEEP_ARCH_H_
#define TIKU_ESP32P4_SLEEP_ARCH_H_

#include <stdint.h>

/* PMU wake-cause bits (pmu_bit_defs.h at the IDF pin). */
#define TIKU_P4_WAKE_LP_CORE  (1UL << 1)   /* the LP core's LP_TRIGGER_HP */
#define TIKU_P4_WAKE_TIMER    (1UL << 13)  /* the LP timer's target 0 */
#define TIKU_P4_WAKE_LP_TRAP  (1UL << 16)  /* the LP core took a trap */

/** @brief The LP timer's count: it runs through deep sleep and resets. */
uint64_t tiku_p4_lp_ticks(void);

/** @brief The LP timer's rate, measured once against SYSTIMER. */
uint32_t tiku_p4_lp_hz(void);

/**
 * @brief Checkpoint durable state, then sleep until @p us have passed or
 *        the LP core wakes the part.
 *
 * Does not return: the wake is a reset, and 0 arms no timer.  A running LP
 * core payload runs on and can wake the part (TIKU_P4_WAKE_LP_CORE, or
 * _LP_TRAP when it faults).  The USB console drops and enumerates again.
 *
 * @note A sleep that never takes the core ends in a reset a second later,
 *       which tiku_p4_sleep_missed() reports; a timed one whose wake never
 *       comes ends in a chip reset through the LP watchdog a second late.
 */
void tiku_p4_deep_sleep(uint64_t us) __attribute__((noreturn));

/**
 * @brief Light sleep: the core stalls on the crystal while every domain,
 *        PLL and clock gate stays on.
 *
 * Ends after @p us (0: no timer) or when the LP core wakes the part, and
 * returns with the CPU back at its rate.  SYSTIMER, the tick, PSRAM and the
 * USB console run through it.
 *
 * @return The PMU's wake cause (TIKU_P4_WAKE_*), or 0 when a pending LP
 *         core wake rejected the sleep
 *
 * @note The core stays stalled until a wake: a timed sleep whose wake never
 *       comes ends in an HP reset through the LP watchdog a second late.
 */
uint32_t tiku_p4_light_sleep(uint64_t us);

/** @brief Light sleeps that ended on a wake since boot, in @p count, and
 *         the time they lasted in microseconds, in @p us. */
void tiku_p4_light_stats(uint32_t *count, uint64_t *us);

/**
 * @brief Latch this boot's LP time, the deep-sleep wake cause and the
 *        missed-sleep flag.
 *
 * @note Call once, early in every boot.
 */
void tiku_p4_sleep_boot(void);

/** @brief PMU wake-cause bits (TIKU_P4_WAKE_*) after a deep-sleep wake,
 *         else 0. */
uint32_t tiku_p4_wake_cause(void);

/** @brief The ROM's reset reason for this boot (rom/rtc.h RESET_REASON):
 *         5 a deep-sleep wake, 16 an LP watchdog chip reset. */
uint32_t tiku_p4_rom_reset_code(void);

/** @brief Nonzero when the last deep sleep fell through to a reset. */
int tiku_p4_sleep_missed(void);

/** @brief The LP timer as this boot began: after a wake, when it woke. */
uint64_t tiku_p4_boot_lp_ticks(void);

#endif /* TIKU_ESP32P4_SLEEP_ARCH_H_ */
