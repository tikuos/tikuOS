/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_usbprobe.h - "usbprobe" command (nRF54LM20 USB high speed).
 *
 * Bring-up diagnostics for the USB block: the DWC2 core's identity and
 * configuration, whether the regulator sees VBUS, and how far a host's
 * enumeration gets.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_USBPROBE_H_
#define TIKU_SHELL_CMD_USBPROBE_H_

#include <stdint.h>

/**
 * @brief "usbprobe" command handler: `usbprobe regs | vbus | up | down |
 *        live [n] | dev [phyif] [trd] [spd] | enum | log |
 *        try <en> <first> [fsel]`.
 */
void tiku_shell_cmd_usbprobe(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_USBPROBE_H_ */
