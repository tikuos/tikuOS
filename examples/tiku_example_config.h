/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_example_config.h - Example selection configuration
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file tiku_example_config.h
 * @brief Example selection flags for enabling/disabling example applications
 *
 * Enable ONE example at a time (set to 1). All others must be 0.
 * When all are 0, no example runs and main.c controls startup.
 * See docs/Examples.md for details on each example.
 */

#ifndef TIKU_EXAMPLE_CONFIG_H_
#define TIKU_EXAMPLE_CONFIG_H_

/**
 * @defgroup TIKU_EXAMPLES Example Selection
 * @brief Enable ONE example at a time (set to 1). All others must be 0.
 *
 * When all are 0, no example runs and main.c controls startup.
 * See docs/Examples.md for details on each example.
 * @{
 */

/** Master example enable - set to 1 to allow example selection */
#define TIKU_EXAMPLES_ENABLE         1

#define TIKU_EXAMPLE_BLINK           0  /**< 01: Single LED blink */
#define TIKU_EXAMPLE_DUAL_BLINK      0  /**< 02: Two LEDs, two processes */
#define TIKU_EXAMPLE_BUTTON_LED      0  /**< 03: Button-controlled LED */
#define TIKU_EXAMPLE_MULTI_PROCESS   0  /**< 04: Inter-process events */
#define TIKU_EXAMPLE_STATE_MACHINE   0  /**< 05: Event-driven state machine */
#define TIKU_EXAMPLE_CALLBACK_TIMER  0  /**< 06: Callback-mode timers */
#define TIKU_EXAMPLE_BROADCAST       0  /**< 07: Broadcast events */
#define TIKU_EXAMPLE_TIMEOUT         0  /**< 08: Timeout pattern */
#define TIKU_EXAMPLE_CHANNEL         1  /**< 09: Channel message passing */

/** @} */ /* End of TIKU_EXAMPLES group */

#endif /* TIKU_EXAMPLE_CONFIG_H_ */
