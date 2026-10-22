/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_h264.h - "h264" command: the ESP32-P4 encoder on a moving
 * test pattern, the stream printed for the host.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_CMD_H264_H_
#define TIKU_SHELL_CMD_H264_H_
#include <stdint.h>

/** @brief h264 encode <w> <h> <frames> [qp <n> | rate <kbit/s>] [gop <n>]
 *         | dump | info */
void tiku_shell_cmd_h264(uint8_t argc, const char *argv[]);
#endif /* TIKU_SHELL_CMD_H264_H_ */
