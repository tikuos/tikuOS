/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_media_arch.h - the RA8P1 code MRAM as the kernel's medium.
 *
 * Stores fill a 32-byte program buffer that commits as one block; a block
 * is rated for 10^5 reprogramming cycles, a rewrite of the same data
 * counted (RA8P1 datasheet, Code MRAM Characteristics, NPC).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_NVM_MEDIA_ARCH_H_
#define TIKU_RA8P1_NVM_MEDIA_ARCH_H_

#define TIKU_NVM_MEDIA_UNIT       32u
#define TIKU_NVM_MEDIA_TEAR       TIKU_NVM_TEAR_LINE
#define TIKU_NVM_MEDIA_ENDURANCE  100000ul
#define TIKU_NVM_MEDIA_ERASE      0u

#endif /* TIKU_RA8P1_NVM_MEDIA_ARCH_H_ */
