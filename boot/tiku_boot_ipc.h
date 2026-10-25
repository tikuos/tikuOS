/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot_ipc.h - boot assembly of IPC domains and endpoints.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_BOOT_IPC_H_
#define TIKU_BOOT_IPC_H_
/** @brief Claim configured domains and initialize IPC; zero on success. */
int tiku_boot_ipc_init(void);
#endif
