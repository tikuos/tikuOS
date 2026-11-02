/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_media_arch.h - the RP2350 board's QSPI flash as the medium.
 *
 * A 256-byte page is programmed, bits 1 to 0 only, and a 4 KB sector is
 * erased before a rewrite; the part is rated for 10^5 erases a sector.  A
 * cut during either leaves bits that may read right and fade.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_NVM_MEDIA_ARCH_H_
#define TIKU_RP2350_NVM_MEDIA_ARCH_H_

#define TIKU_NVM_MEDIA_UNIT       256u
#define TIKU_NVM_MEDIA_TEAR       TIKU_NVM_TEAR_LINE
#define TIKU_NVM_MEDIA_ENDURANCE  100000ul
#define TIKU_NVM_MEDIA_ERASE      4096u

#endif /* TIKU_RP2350_NVM_MEDIA_ARCH_H_ */
