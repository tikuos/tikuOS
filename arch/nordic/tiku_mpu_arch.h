/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - nRF54L MPU / NVM-gate arch header
 *
 * The MPU HAL (hal/tiku_mpu_hal.h) declares the tiku_mpu_arch_* API.  On this
 * port the load-bearing pair is unlock_nvm()/lock_nvm() (the RRAMC WEN write
 * gate); the MSP430-style segment-access-mask is a software shadow (same
 * bookkeeping model as the rp2350/ambiq ports) whose write bits track the WEN
 * window, so the portable MPU semantics tests exercise the same state machine
 * on every port.  ARMv8-M MPU region protection of the persistent range is a
 * later hardening step.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_MPU_ARCH_H_
#define TIKU_NORDIC_MPU_ARCH_H_

/** @brief Default segment-access-mask: R+X, no write (matches rp2350/ambiq). */
#define TIKU_MPU_DEFAULT_SAM    0x0555U

#endif /* TIKU_NORDIC_MPU_ARCH_H_ */
