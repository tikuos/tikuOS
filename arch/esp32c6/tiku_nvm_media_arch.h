/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_media_arch.h - the ESP32-C6 module's SPI flash as the medium.
 *
 * A 256-byte page is programmed, bits 1 to 0 only, and a 4 KB sector is
 * erased before a rewrite; the parts fitted to the kits are rated for 10^5
 * erases a sector.  A cut during either leaves bits that may fade.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C6_NVM_MEDIA_ARCH_H_
#define TIKU_ESP32C6_NVM_MEDIA_ARCH_H_

#define TIKU_NVM_MEDIA_UNIT       256u
#define TIKU_NVM_MEDIA_TEAR       TIKU_NVM_TEAR_LINE
#define TIKU_NVM_MEDIA_ENDURANCE  100000ul
#define TIKU_NVM_MEDIA_ERASE      4096u

#endif /* TIKU_ESP32C6_NVM_MEDIA_ARCH_H_ */
