/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_adc.h - "adc" command: read ADC channels
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_ADC_H_
#define TIKU_SHELL_CMD_ADC_H_

#include <stdint.h>

/**
 * @brief "adc" command: read one ADC channel through the HAL and print it.
 *
 * Usage: adc <channel|temp|bat> [ref].  The channel is 0-15 (external pins),
 * temp (internal sensor) or bat (battery divider); ref is avcc, 1v2, 2v0 or
 * 2v5, default avcc.  Every conversion is 12-bit.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_adc(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ADC_H_ */
