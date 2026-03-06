/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu.c - MPU write-protection wrappers (platform-independent)
 *
 * Provides a controlled interface for FRAM protection using the MPU.
 * All hardware register access is routed through the HAL functions
 * (tiku_mpu_arch_get_sam, tiku_mpu_arch_set_sam, etc.), so this file
 * contains only platform-independent logic.
 *
 * Default policy: all three MPU segments are read+execute, no write.
 * This prevents stray pointers and runaway code from corrupting FRAM.
 * To write to FRAM, code explicitly unlocks, writes, and relocks.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_mem.h"

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/*
 * What 0x0555 means in MPUSAM:
 *   Each segment occupies 4 bits in the SAM register. Within those
 *   4 bits, the lower 3 are R/W/X permissions. 0x5 = 0b0101 =
 *   READ | EXEC (no WRITE). Three segments at 0x5 each: 0x0555.
 *
 *     Bits [3:0]   = Segment 1: 0x5 (R+X)
 *     Bits [7:4]   = Segment 2: 0x5 (R+X)
 *     Bits [11:8]  = Segment 3: 0x5 (R+X)
 *
 * What 0x0222 means:
 *   Bit 1 (WRITE) set in each segment's nybble. ORing this into
 *   the current SAM value adds write permission to all segments.
 */
#define MPU_SAM_DEFAULT   0x0555  /* R+X on all 3 segments */
#define MPU_SAM_WRITE_ALL 0x0222  /* Write bit for all 3 segments */

/*---------------------------------------------------------------------------*/
/* MPU FUNCTIONS                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize MPU — all segments read+execute, no write
 *
 * Sets the segment-access-mode register to 0x0555 via the arch HAL,
 * which gives all three segments read+execute with no write permission.
 */
void tiku_mpu_init(void)
{
    tiku_mpu_arch_set_sam(MPU_SAM_DEFAULT);
}

/**
 * @brief Set permissions on one segment
 *
 * Computes the bit position from the segment number (seg * 4), clears
 * the old 3-bit permission field, and sets the new bits. The 4th bit
 * in each nybble is reserved and left untouched.
 */
void tiku_mpu_set_permissions(tiku_mpu_seg_t seg, tiku_mpu_perm_t perm)
{
    uint16_t shift = (uint16_t)seg * 4U;
    uint16_t mask  = (uint16_t)0x07 << shift;
    uint16_t sam   = tiku_mpu_arch_get_sam();

    sam = (sam & ~mask) | (((uint16_t)perm & 0x07) << shift);
    tiku_mpu_arch_set_sam(sam);
}

/*
 * Why unlock is coarse-grained (all segments at once):
 *   Most FRAM write operations need to touch multiple segments (code
 *   constants in one, data in another). Unlocking all at once keeps the
 *   critical section short. For finer control, use tiku_mpu_set_permissions()
 *   on individual segments.
 */

/**
 * @brief Unlock FRAM for writing — ORs the write bit into all segments
 *
 * @return Previous SAM value for later restoration
 */
uint16_t tiku_mpu_unlock_fram(void)
{
    uint16_t saved = tiku_mpu_arch_get_sam();

    tiku_mpu_arch_set_sam(saved | MPU_SAM_WRITE_ALL);

    return saved;
}

/**
 * @brief Restore MPU to a previously saved state
 */
void tiku_mpu_lock_fram(uint16_t saved_state)
{
    tiku_mpu_arch_set_sam(saved_state);
}

/*
 * Why scoped_write disables interrupts:
 *   While FRAM is unlocked, any ISR that fires has write access to
 *   FRAM. A bug in an ISR could corrupt persistent data. By disabling
 *   interrupts for the duration of the unlock window, we guarantee that
 *   only the caller's function can write to FRAM.
 *
 * Caveat: the write function (fn) must be short — while it runs,
 * interrupts are masked and hardware events queue up. Long writes
 * should be broken into multiple scoped_write calls.
 */

/**
 * @brief Execute a function with FRAM unlocked and interrupts disabled
 */
void tiku_mpu_scoped_write(tiku_mpu_write_fn fn, void *ctx)
{
    uint16_t saved;

    tiku_mpu_arch_disable_irq();
    saved = tiku_mpu_unlock_fram();

    fn(ctx);

    tiku_mpu_lock_fram(saved);
    tiku_mpu_arch_enable_irq();
}
