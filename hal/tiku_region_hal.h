/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_hal.h - HAL interface for memory region registry
 *
 * Declares the arch-level region table accessor that each platform port
 * must implement. The platform port returns a const table of memory
 * region descriptors describing its physical memory map (SRAM, NVM,
 * peripherals, flash).
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

#ifndef TIKU_REGION_HAL_H_
#define TIKU_REGION_HAL_H_

#include <stdint.h>
#include "hal/tiku_mem_hal.h"

/*---------------------------------------------------------------------------*/
/* FORWARD DECLARATION                                                       */
/*---------------------------------------------------------------------------*/

/* Full definition in tiku_mem.h */
struct tiku_mem_region;

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTION                                                */
/*---------------------------------------------------------------------------*/

/*
 * Each platform port implements this function in its arch directory
 * (e.g. arch/msp430/tiku_region_arch.c) to return a const array of
 * tiku_mem_region_t descriptors that describe the physical memory map.
 */

/**
 * @brief Return the platform's memory region table
 *
 * Returns a pointer to a const array of region descriptors that
 * describe the platform's physical memory map (SRAM, NVM, peripheral,
 * flash regions). The returned table is expected to reside in flash
 * and remain valid for the lifetime of the system.
 *
 * @param count  Output: number of entries in the returned table
 * @return Pointer to the platform's region descriptor array (const)
 */
const struct tiku_mem_region *tiku_region_arch_get_table(
    tiku_mem_arch_size_t *count);

#endif /* TIKU_REGION_HAL_H_ */
