/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_media_arch.h - the STM32N6 board's octal NOR as the medium.
 *
 * The MX25UM51245G programs a 256-byte page, bits 1 to 0 only, and erases
 * a 4 KB sector before a rewrite; it is rated for 10^5 erases a sector.  A
 * cut during either leaves bits that may read right and fade.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_NVM_MEDIA_ARCH_H_
#define TIKU_STM32N6_NVM_MEDIA_ARCH_H_

#define TIKU_NVM_MEDIA_UNIT       256u
#define TIKU_NVM_MEDIA_TEAR       TIKU_NVM_TEAR_LINE
#define TIKU_NVM_MEDIA_ENDURANCE  100000ul
#define TIKU_NVM_MEDIA_ERASE      4096u

#endif /* TIKU_STM32N6_NVM_MEDIA_ARCH_H_ */
