/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_common_hal.h - Platform-routing header for common utilities
 *
 * Routes to the correct architecture-specific common header based
 * on the selected platform. This is the single point where the arch
 * common header enters the include chain.
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

#ifndef TIKU_COMMON_HAL_H_
#define TIKU_COMMON_HAL_H_

#ifdef PLATFORM_MSP430
#include "arch/msp430/tiku_cpu_common.h"
#endif

/*---------------------------------------------------------------------------*/
/* HAL-to-arch mapping macros                                                */
/*---------------------------------------------------------------------------*/

#define tiku_common_arch_delay_ms(ms)   tiku_cpu_msp430_delay_ms(ms)
#define tiku_common_arch_delay_us(us)   tiku_cpu_msp430_delay_us(us)
#define tiku_common_arch_unique_id(b,l) tiku_cpu_msp430_unique_id((b),(l))
#define tiku_common_arch_reset_reason() tiku_cpu_msp430_reset_reason()

#endif /* TIKU_COMMON_HAL_H_ */
