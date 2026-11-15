/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - S31 durable-memory write window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_MPU_ARCH_H
#define TIKU_S31_MPU_ARCH_H
#include <stdint.h>
#define TIKU_MPU_DEFAULT_SAM 0x0555U
/** @brief Disable the durable PMA guard; return -1 for locked or unavailable entries. */
int tiku_s31_durable_guard_off(void);
/** @brief Return one when the readonly durable range and PMA permissions read back. */
int tiku_s31_mpu_ready(void);
#endif
