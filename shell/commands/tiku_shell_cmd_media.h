/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_media.h - Native SD volume and object operations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_CMD_MEDIA_H_
#define TIKU_SHELL_CMD_MEDIA_H_
#include <stdint.h>
/** @brief Mount, explicitly format, inspect and transfer native card objects. */
void tiku_shell_cmd_media(uint8_t argc,const char *argv[]);
#endif
