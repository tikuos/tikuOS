/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_media_arch.h - Absent NVM medium in the S31 SRAM profile.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32S31_NVM_MEDIA_ARCH_H_
#define TIKU_ESP32S31_NVM_MEDIA_ARCH_H_
#if !defined(TIKU_MEM_NVM_ENABLE) || TIKU_MEM_NVM_ENABLE
#error "S31 SRAM profiles require TIKU_MEM_NVM_ENABLE=0"
#endif
#define TIKU_NVM_MEDIA_DISABLED   1
#define TIKU_NVM_MEDIA_UNIT       0u
#define TIKU_NVM_MEDIA_TEAR       TIKU_NVM_TEAR_LINE
#define TIKU_NVM_MEDIA_ENDURANCE  0ul
#define TIKU_NVM_MEDIA_ERASE      0u
#endif
