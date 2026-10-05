/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cam.h - "cam" command: capture a frame and show it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CAM_H_
#define TIKU_SHELL_CMD_CAM_H_

#include <stdint.h>

/**
 * @brief Power the OV5640, capture one QVGA frame and show it on the panel.
 *
 * A build without a display captures the frame and reports it only.
 *
 * @param argc  Argument count (unused)
 * @param argv  Arguments (unused)
 * @note Defined only when TIKU_SHELL_CMD_CAM is set: an RA8P1 build with
 *       TIKU_DRV_CAM_ENABLE=1.
 */
void tiku_shell_cmd_cam(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CAM_H_ */
