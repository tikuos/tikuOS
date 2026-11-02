/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_media_arch.h - the Apollo MRAM as the kernel's medium.
 *
 * The boot ROM programs 16-byte aligned words, taken as the unit; the MRAM
 * is rated for 10^5 programs (Apollo4 Plus datasheet, NVM table, PCYC;
 * Apollo5 family training, "100K program cycles").
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_NVM_MEDIA_ARCH_H_
#define TIKU_AMBIQ_NVM_MEDIA_ARCH_H_

#define TIKU_NVM_MEDIA_UNIT       16u
#define TIKU_NVM_MEDIA_TEAR       TIKU_NVM_TEAR_LINE
#define TIKU_NVM_MEDIA_ENDURANCE  100000ul
#define TIKU_NVM_MEDIA_ERASE      0u

#endif /* TIKU_AMBIQ_NVM_MEDIA_ARCH_H_ */
