/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_platform.h - MSP430FR5969 platform header
 *
 * This file provides the core CPU frequency configuration functions
 * for the Tiku Operating System on the MSP430FR5969 microcontroller.
 * It includes clock system configuration, frequency setting, and
 * frequency getting functions.
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

 #ifndef TIKU_PLATFORM_H
 #define TIKU_PLATFORM_H

 #include <tiku.h>

 /*
  * Device-specific definitions are now provided by the device/board
  * headers selected via tiku_device_select.h (included from tiku.h).
  * The old TIKU_PLATFORM_MSP430_FR5969 define has been replaced by
  * TIKU_DEVICE_MSP430FR5969 in tiku.h.
  */

#endif