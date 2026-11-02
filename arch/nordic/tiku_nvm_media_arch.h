/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_media_arch.h - the nRF54L RRAM as the kernel's medium.
 *
 * The RRAMC programs a whole 128-bit wordline for a write of any size and
 * counts it against the line's 10^4 rating (nRF54L15 datasheet, RRAMC
 * "Writing to RRAM"; electrical specification, nENDURANCE).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_NVM_MEDIA_ARCH_H_
#define TIKU_NORDIC_NVM_MEDIA_ARCH_H_

#define TIKU_NVM_MEDIA_UNIT       16u
#define TIKU_NVM_MEDIA_TEAR       TIKU_NVM_TEAR_LINE
#define TIKU_NVM_MEDIA_ENDURANCE  10000ul
#define TIKU_NVM_MEDIA_ERASE      0u

#endif /* TIKU_NORDIC_NVM_MEDIA_ARCH_H_ */
