/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - MSP430 MPU architecture implementation
 *
 * Implements the arch-level MPU register access for MSP430FR series.
 * All accesses to MPUCTL0 and MPUSAM go through this file, keeping
 * the password handling and hardware details in one place.
 *
 * The MSP430 MPU registers are password-protected: every write to
 * MPUCTL0 must include MPUPW (0xA500) in the upper byte. This
 * prevents wild pointer writes from accidentally modifying memory
 * protection configuration.
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

#include "tiku_mpu_arch.h"
#include <msp430.h>

/*---------------------------------------------------------------------------*/
/* MPU REGISTER ACCESS                                                       */
/*---------------------------------------------------------------------------*/

uint16_t tiku_mpu_arch_get_sam(void)
{
    return MPUSAM;
}

/*
 * Why unlock-write-enable in every set_sam call:
 *   The MPU config registers are locked by default. To modify MPUSAM
 *   we must write the password to MPUCTL0 first, then change MPUSAM,
 *   then write password | enable to MPUCTL0 to re-activate. Bundling
 *   this into set_sam means the kernel never needs to know about the
 *   password or the enable bit.
 */
void tiku_mpu_arch_set_sam(uint16_t sam)
{
    MPUCTL0 = MPUPW;           /* Unlock config */
    MPUSAM  = sam;             /* Set permissions */
    MPUCTL0 = MPUPW | MPUENA; /* Re-enable MPU */
}

uint16_t tiku_mpu_arch_get_ctl(void)
{
    return MPUCTL0;
}

void tiku_mpu_arch_disable_irq(void)
{
    __disable_interrupt();
}

void tiku_mpu_arch_enable_irq(void)
{
    __enable_interrupt();
}
