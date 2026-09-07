/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_usbprobe.h - "usbprobe" command (nRF54LM20 USB high speed).
 *
 * Bring-up recon for the USB block: what the DWC2 core says it is, whether
 * the regulator sees a cable, and what a host's enumeration attempt reaches.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_USBPROBE_H_
#define TIKU_SHELL_CMD_USBPROBE_H_

#include <stdint.h>

/** @brief "usbprobe" command handler. */
void tiku_shell_cmd_usbprobe(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_USBPROBE_H_ */
