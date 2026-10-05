/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_reboot.h - "reboot" command: trigger a system reset
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_REBOOT_H_
#define TIKU_SHELL_CMD_REBOOT_H_

#include <stdint.h>

/**
 * @brief "reboot" command handler — trigger a system reset.
 *
 * Usage: reboot [boot]   (boot: RP2350 only, reboot into BOOTSEL)
 * Configures the watchdog timer for a short timeout in watchdog mode,
 * then spins until the hardware resets the system.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] may be "boot" on RP2350
 */
void tiku_shell_cmd_reboot(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_REBOOT_H_ */
