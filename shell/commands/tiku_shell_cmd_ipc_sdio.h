/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ipc_sdio.h - SDIO bus discovery and native diagnostic commands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_IPC_SDIO_H_
#define TIKU_SHELL_IPC_SDIO_H_
#include <stdint.h>
/** @brief Run an explicitly selected P4/C6 SDIO diagnostic through its controller owner. */
void tiku_shell_ipc_sdio(uint8_t argc,const char *argv[]);
#endif
