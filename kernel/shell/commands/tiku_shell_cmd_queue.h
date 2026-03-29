/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * tiku_shell_cmd_queue.h - "queue" command interface
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_QUEUE_H_
#define TIKU_SHELL_CMD_QUEUE_H_

#include <stdint.h>

/**
 * @brief List pending events in the scheduler queue.
 *
 * Shows each event's index, type name, and target process.
 */
void tiku_shell_cmd_queue(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_QUEUE_H_ */
