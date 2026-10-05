/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - nRF54L MPU and NVM-gate arch header.
 *
 * NVM writes are gated by the RRAMC WEN bit, which unlock_nvm()/lock_nvm()
 * drive; the segment-access-mask is a software shadow whose write bits track
 * that window.  No MPU region covers RRAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_MPU_ARCH_H_
#define TIKU_NORDIC_MPU_ARCH_H_

/** @brief Default segment-access-mask: read and execute, no write. */
#define TIKU_MPU_DEFAULT_SAM    0x0555U

#endif /* TIKU_NORDIC_MPU_ARCH_H_ */
