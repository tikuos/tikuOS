/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_axonsprobe.h - "axonsprobe" Axon NPU probe and tests (opt-in)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_AXONSPROBE_H_
#define TIKU_SHELL_CMD_AXONSPROBE_H_

#include <stdint.h>                          /* uint8_t in the handler type */
#include <shell/tiku_shell_config.h>

#if TIKU_SHELL_CMD_AXONSPROBE
/**
 * @brief "axonsprobe" command handler: Axon NPU probe and tests.
 *
 * The raw sub-commands write nothing in the block but ENABLE; the others run
 * the engine and need the vendor driver (TIKU_AXON_ENABLE).
 *
 * @param argc  Argument count
 * @param argv  argv[1]: none (registers and FICR identity), en, off, dump
 *              (hex offset, word count), diff or irq; with the driver also
 *              hw, acc, fir, hold and busy (milliseconds), modelstore
 *              (model, .kat) in a baked-model or store-only build, and model
 *              and modelbaked in a baked-model build
 * @note The signature must match tiku_shell_handler_t (tiku_shell.h): the
 *       command table stores it directly.
 */
void tiku_shell_cmd_axonsprobe(uint8_t argc, const char *argv[]);
#endif

#endif /* TIKU_SHELL_CMD_AXONSPROBE_H_ */
