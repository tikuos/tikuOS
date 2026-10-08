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
 * @brief Bring the panel up and paint a colour (red by default), draw the
 *        name "TikuOS" (`text`) or a circle (`circle`).
 *
 * An unknown argument prints the accepted words and draws nothing.
 *
 * @param argc  Argument count
 * @param argv  Arguments; argv[1] is a colour name, `text` or `circle`
 */
void tiku_shell_cmd_panel(uint8_t argc, const char *argv[]);

/**
 * @brief The panel's screen, initialised on first use.
 *
 * Commands that draw, such as the camera, share this screen and its
 * framebuffer.
 *
 * @return The initialised screen, or NULL when it cannot come up
 */
tiku_display_t *tiku_shell_cmd_panel_display(void);

#endif /* TIKU_SHELL_CMD_PANEL_H_ */
