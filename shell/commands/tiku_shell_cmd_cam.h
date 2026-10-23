/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
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
 * @brief On the RA8P1, power the OV5640, capture a QVGA frame and show it on
 *        the panel (a build without a display reports it only); on the
 *        ESP32-P4, the IMX219 subcommands of tiku_shell_cmd_cam_p4.c.
 *
 * @param argc  Argument count (unused on the RA8P1)
 * @param argv  Arguments (unused on the RA8P1)
 * @note Defined only when TIKU_SHELL_CMD_CAM is set: an RA8P1 or FireBeetle 2
 *       ESP32-P4 build with TIKU_DRV_CAM_ENABLE=1.
 */
void tiku_shell_cmd_cam(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CAM_H_ */
