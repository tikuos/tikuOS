/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_adc.h - "adc" command: read ADC channels
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_ADC_H_
#define TIKU_SHELL_CMD_ADC_H_

#include <stdint.h>

/**
 * @brief "adc" command handler — read analog channels through the HAL.
 *
 * Usage:
 *   adc <channel>               — read channel (12-bit, AVCC ref)
 *   adc <channel> <ref>         — read with reference (avcc|1v2|2v0|2v5)
 *   adc temp                    — read internal temperature sensor
 *   adc bat                     — read battery (AVCC/2)
 *
 * Channel is 0-15 for external pins, or "temp"/"bat" for internal.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_adc(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ADC_H_ */
