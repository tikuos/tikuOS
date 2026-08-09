/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_panel.h - "panel" command: drive the parallel RGB display.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_PANEL_H_
#define TIKU_SHELL_CMD_PANEL_H_

#include <stdint.h>

#include <interfaces/display/tiku_display.h>

/**
 * @brief Bring the panel up and paint a colour, so a person can see it.
 *
 * @param argc  Argument count
 * @param argv  Arguments; optional colour name
 */
void tiku_shell_cmd_panel(uint8_t argc, const char *argv[]);

/**
 * @brief The one screen this command owns, brought up on first use.
 *
 * Other commands that draw -- the camera, for one -- share the panel and its
 * framebuffer through this instead of claiming a second buffer of their own.
 *
 * @return The initialised screen, or NULL when it cannot come up
 */
tiku_display_t *tiku_shell_cmd_panel_display(void);

#endif /* TIKU_SHELL_CMD_PANEL_H_ */
