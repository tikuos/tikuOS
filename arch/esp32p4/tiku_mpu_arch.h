/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - P4 memory protection: the durable-span write window,
 * the locked PMP NULL guard and the trap entry's violation latch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_MPU_ARCH_H_
#define TIKU_ESP32P4_MPU_ARCH_H_
#include <stdint.h>

/** @brief Default segment access mask: read and execute, no write. */
#define TIKU_MPU_DEFAULT_SAM 0x0555U

/** @brief The record tiku_crt_early.S latches a durable-span store access
 *         fault in: span start and end, violation flags, count, last
 *         address (TIKU_P4_MPU_LATCH_* index it). */
extern uint32_t tiku_p4_mpu_latch[5];
#define TIKU_P4_MPU_LATCH_START 0
#define TIKU_P4_MPU_LATCH_END   1
#define TIKU_P4_MPU_LATCH_FLAGS 2
#define TIKU_P4_MPU_LATCH_COUNT 3
#define TIKU_P4_MPU_LATCH_LAST  4

/** @brief 1 while PMA entry 1 holds the durable span enabled and read-only,
 *         0 while a window has it writable. */
uint8_t tiku_mpu_arch_nvm_region_ro(void);
/** @brief Store access faults inside the durable span since boot. */
uint32_t tiku_mpu_arch_violation_count(void);
/** @brief The address of the last such fault, 0 before the first. */
uint32_t tiku_mpu_arch_last_fault_addr(void);
#endif
