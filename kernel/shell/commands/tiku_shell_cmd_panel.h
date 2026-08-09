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

/**
 * @brief Bring the panel up and paint a colour, so a person can see it.
 *
 * @param argc  Argument count
 * @param argv  Arguments; optional colour name
 */
void tiku_shell_cmd_panel(int argc, char **argv);

#endif /* TIKU_SHELL_CMD_PANEL_H_ */
