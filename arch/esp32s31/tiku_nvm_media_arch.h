/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_media_arch.h - S31 firmware flash geometry; SRAM profiles have no NVM medium.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32S31_NVM_MEDIA_ARCH_H_
#define TIKU_ESP32S31_NVM_MEDIA_ARCH_H_
#if TIKU_MEM_NVM_ENABLE
#define TIKU_NVM_MEDIA_UNIT       256u
#define TIKU_NVM_MEDIA_TEAR       TIKU_NVM_TEAR_LINE
#define TIKU_NVM_MEDIA_ENDURANCE  0ul /* Board flash endurance is unspecified. */
#define TIKU_NVM_MEDIA_ERASE      4096u
#else
#define TIKU_NVM_MEDIA_DISABLED   1
#define TIKU_NVM_MEDIA_UNIT       0u
#define TIKU_NVM_MEDIA_TEAR       TIKU_NVM_TEAR_LINE
#define TIKU_NVM_MEDIA_ENDURANCE  0ul
#define TIKU_NVM_MEDIA_ERASE      0u
#endif
#endif
