/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_mem_mpu.c - MPU write-protection tests
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

#include "test_tiku_mem.h"

/*---------------------------------------------------------------------------*/
/* TEST 21: MPU INIT SETS DEFAULT PROTECTION                                  */
/*---------------------------------------------------------------------------*/

void test_mpu_init_defaults(void)
{
    TEST_PRINT("\n--- Test: MPU Init Defaults ---\n");

    tiku_mpu_init();

    /* All 3 segments should be R+X (0x5 per nybble) = 0x0555 */
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "SAM is 0x0555 after init (all segments R+X, no W)");

    /* MPU should be enabled (MPUENA = 0x0001 in lower byte) */
    TEST_ASSERT((tiku_mpu_arch_get_ctl() & 0x0001) != 0,
                "CTL has enable bit set after init");
}

/*---------------------------------------------------------------------------*/
/* TEST 22: MPU UNLOCK / LOCK FRAM                                            */
/*---------------------------------------------------------------------------*/

void test_mpu_unlock_lock(void)
{
    uint16_t saved;

    TEST_PRINT("\n--- Test: MPU Unlock / Lock ---\n");

    tiku_mpu_init();

    saved = tiku_mpu_unlock_fram();
    TEST_ASSERT(saved == 0x0555,
                "unlock returns previous SAM (0x0555)");

    /* After unlock, write bits should be set: 0x0555 | 0x0222 = 0x0777 */
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0777,
                "SAM has write bits after unlock (0x0777)");

    tiku_mpu_lock_fram(saved);
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "SAM restored to 0x0555 after lock");
}

/*---------------------------------------------------------------------------*/
/* TEST 23: MPU SET PERMISSIONS ON ONE SEGMENT                                */
/*---------------------------------------------------------------------------*/

void test_mpu_set_permissions(void)
{
    uint16_t sam;

    TEST_PRINT("\n--- Test: MPU Set Permissions ---\n");

    tiku_mpu_init();
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555, "baseline is 0x0555");

    /* Set segment 3 (bits [11:8]) to RD_WR (0x03) */
    tiku_mpu_set_permissions(TIKU_MPU_SEG3, TIKU_MPU_RD_WR);

    sam = tiku_mpu_arch_get_sam();
    /* Segment 1 and 2 unchanged (0x55), segment 3 now 0x3 */
    TEST_ASSERT((sam & 0x000F) == 0x0005,
                "segment 1 unchanged (R+X)");
    TEST_ASSERT((sam & 0x00F0) == 0x0050,
                "segment 2 unchanged (R+X)");
    TEST_ASSERT((sam & 0x0F00) == 0x0300,
                "segment 3 set to RD_WR (0x3)");
}

/*---------------------------------------------------------------------------*/
/* TEST 24: MPU SCOPED WRITE                                                  */
/*---------------------------------------------------------------------------*/

/* Context for the scoped-write test callback */
typedef struct {
    int called;
    uint16_t sam_during;
} scoped_write_ctx_t;

static void scoped_write_cb(void *arg)
{
    scoped_write_ctx_t *ctx = (scoped_write_ctx_t *)arg;
    ctx->called = 1;
    ctx->sam_during = tiku_mpu_arch_get_sam();
}

void test_mpu_scoped_write(void)
{
    scoped_write_ctx_t ctx;

    TEST_PRINT("\n--- Test: MPU Scoped Write ---\n");

    tiku_mpu_init();

    ctx.called = 0;
    ctx.sam_during = 0;

    tiku_mpu_scoped_write(scoped_write_cb, &ctx);

    TEST_ASSERT(ctx.called == 1,
                "callback was invoked");
    TEST_ASSERT((ctx.sam_during & 0x0222) == 0x0222,
                "SAM had write bits during callback");
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "SAM locked again after scoped_write");
}

/*---------------------------------------------------------------------------*/
/* TEST 25: MPU LOCK / UNLOCK IDEMPOTENCY                                     */
/*---------------------------------------------------------------------------*/

void test_mpu_idempotent(void)
{
    uint16_t saved1, saved2;

    TEST_PRINT("\n--- Test: MPU Lock/Unlock Idempotency ---\n");

    tiku_mpu_init();

    /* Lock when already locked — state should not change */
    tiku_mpu_lock_fram(0x0555);
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "lock when already locked keeps 0x0555");

    /* Double unlock — second call should return the already-unlocked state */
    saved1 = tiku_mpu_unlock_fram();
    TEST_ASSERT(saved1 == 0x0555, "first unlock returns 0x0555");

    saved2 = tiku_mpu_unlock_fram();
    TEST_ASSERT(saved2 == 0x0777,
                "second unlock returns 0x0777 (already unlocked)");
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0777,
                "SAM still 0x0777 after double unlock");

    /* Restoring with saved1 should relock properly */
    tiku_mpu_lock_fram(saved1);
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "lock with original saved state restores 0x0555");
}
