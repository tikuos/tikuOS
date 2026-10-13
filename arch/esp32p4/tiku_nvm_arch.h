/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_nvm_arch.h - P4 explicit durable-mirror initialization and status.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_NVM_ARCH_H_
#define TIKU_P4_NVM_ARCH_H_
#include <kernel/memory/tiku_nvm_mirror.h>
/** @brief Restore the SRAM durable image after flash mapping is ready.
 * @note Requires CPU0 foreground execution and no durable-image users. Does
 * not format or write flash. Invalid nonblank mirrors leave the image zeroed
 * and commits disabled. A reset is required before another initialization.
 * @return Zero on success, negative for unavailable or corrupt storage.
 */
int tiku_p4_nvm_init(void);
/** @brief Return boot restore status; unavailable storage reports CRC_FAIL. */
tiku_nvm_restore_t tiku_mem_arch_nvm_restore_status(void);
/** @brief Return the number of verified commits since initialization. */
uint32_t tiku_p4_nvm_commits(void);
#endif
