/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cam.h - "cam" command: camera bring-up and capture.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CAM_H_
#define TIKU_SHELL_CMD_CAM_H_

#include <stdint.h>

/**
 * @brief Drive the camera: power, identify, and later capture.
 *
 * @param argc  Argument count
 * @param argv  Arguments
 */
void tiku_shell_cmd_cam(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CAM_H_ */
