/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - ESP32-C5 memory-protection contract.
 *
 * The segment access mask keeps the MSP430 MPU's layout, which the portable
 * tests check; its write bits open and close the durable span's PMA write
 * permission.  A locked PMP entry guards the first 4 KB (NULL).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_MPU_ARCH_H_
#define TIKU_ESP32C5_MPU_ARCH_H_

#include <stdint.h>

/** @brief Default segment access mask: read and execute, no write. */
#define TIKU_MPU_DEFAULT_SAM 0x0555U

/**
 * @brief Make the durable span writable again by switching its PMA guard
 *        off; tiku_mpu_init() arms it once the working copy is in place.
 *
 * The guard survives a reset of the HP system, so boot and every restore of
 * the working copy call this first.
 */
void tiku_c5_durable_guard_off(void);

#endif
