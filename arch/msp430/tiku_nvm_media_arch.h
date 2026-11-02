/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_media_arch.h - the MSP430 FRAM as the kernel's medium.
 *
 * FRAM is written in place by the 16-bit word; a cut splits a longer write
 * at a word.  TI rates it for about 10^15 write cycles, which the kernel
 * treats as no limit.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MSP430_NVM_MEDIA_ARCH_H_
#define TIKU_MSP430_NVM_MEDIA_ARCH_H_

#define TIKU_NVM_MEDIA_UNIT       2u
#define TIKU_NVM_MEDIA_TEAR       TIKU_NVM_TEAR_BYTE
#define TIKU_NVM_MEDIA_ENDURANCE  0ul
#define TIKU_NVM_MEDIA_ERASE      0u

#endif /* TIKU_MSP430_NVM_MEDIA_ARCH_H_ */
