/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - nRF54L MPU and NVM-gate arch header.
 *
 * The load-bearing pair on this port is unlock_nvm()/lock_nvm(), the RRAMC WEN
 * gate; the segment-access-mask is a software shadow whose write bits track that
 * window.  MPU region protection of the persistent range is a later step.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_MPU_ARCH_H_
#define TIKU_NORDIC_MPU_ARCH_H_

/** @brief Default segment-access-mask: R+X, no write (matches rp2350/ambiq). */
#define TIKU_MPU_DEFAULT_SAM    0x0555U

#endif /* TIKU_NORDIC_MPU_ARCH_H_ */
