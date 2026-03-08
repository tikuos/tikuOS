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
/* TEST 22: MPU UNLOCK / LOCK NVM                                             */
/*---------------------------------------------------------------------------*/

void test_mpu_unlock_lock(void)
{
    uint16_t saved;

    TEST_PRINT("\n--- Test: MPU Unlock / Lock ---\n");

    tiku_mpu_init();

    saved = tiku_mpu_unlock_nvm();
    TEST_ASSERT(saved == 0x0555,
                "unlock returns previous SAM (0x0555)");

    /* After unlock, write bits should be set: 0x0555 | 0x0222 = 0x0777 */
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0777,
                "SAM has write bits after unlock (0x0777)");

    tiku_mpu_lock_nvm(saved);
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
    tiku_mpu_lock_nvm(0x0555);
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "lock when already locked keeps 0x0555");

    /* Double unlock — second call should return the already-unlocked state */
    saved1 = tiku_mpu_unlock_nvm();
    TEST_ASSERT(saved1 == 0x0555, "first unlock returns 0x0555");

    saved2 = tiku_mpu_unlock_nvm();
    TEST_ASSERT(saved2 == 0x0777,
                "second unlock returns 0x0777 (already unlocked)");
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0777,
                "SAM still 0x0777 after double unlock");

    /* Restoring with saved1 should relock properly */
    tiku_mpu_lock_nvm(saved1);
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "lock with original saved state restores 0x0555");
}

/*---------------------------------------------------------------------------*/
/* TEST 26: MPU SET PERMISSIONS ON ALL SEGMENTS INDEPENDENTLY                 */
/*---------------------------------------------------------------------------*/

void test_mpu_all_segments(void)
{
    uint16_t sam;

    TEST_PRINT("\n--- Test: MPU All Segments Independent ---\n");

    tiku_mpu_init();

    /* Set each segment to a different permission */
    tiku_mpu_set_permissions(TIKU_MPU_SEG1, TIKU_MPU_READ);      /* 0x1 */
    tiku_mpu_set_permissions(TIKU_MPU_SEG2, TIKU_MPU_RD_WR);     /* 0x3 */
    tiku_mpu_set_permissions(TIKU_MPU_SEG3, TIKU_MPU_ALL);        /* 0x7 */

    sam = tiku_mpu_arch_get_sam();
    TEST_ASSERT((sam & 0x000F) == 0x0001,
                "segment 1 is READ-only (0x1)");
    TEST_ASSERT((sam & 0x00F0) == 0x0030,
                "segment 2 is RD_WR (0x3)");
    TEST_ASSERT((sam & 0x0F00) == 0x0700,
                "segment 3 is ALL (0x7)");

    /* Now change segment 2 alone — 1 and 3 must stay */
    tiku_mpu_set_permissions(TIKU_MPU_SEG2, TIKU_MPU_EXEC);      /* 0x4 */

    sam = tiku_mpu_arch_get_sam();
    TEST_ASSERT((sam & 0x000F) == 0x0001,
                "segment 1 unchanged after seg2 update");
    TEST_ASSERT((sam & 0x00F0) == 0x0040,
                "segment 2 now EXEC-only (0x4)");
    TEST_ASSERT((sam & 0x0F00) == 0x0700,
                "segment 3 unchanged after seg2 update");
}

/*---------------------------------------------------------------------------*/
/* TEST 27: MPU INDIVIDUAL PERMISSION FLAGS                                   */
/*---------------------------------------------------------------------------*/

void test_mpu_permission_flags(void)
{
    uint16_t sam;

    TEST_PRINT("\n--- Test: MPU Permission Flags ---\n");

    /* Test each permission enum value on segment 1 (bits [3:0]) */

    tiku_mpu_init();
    tiku_mpu_set_permissions(TIKU_MPU_SEG1, TIKU_MPU_READ);
    sam = tiku_mpu_arch_get_sam();
    TEST_ASSERT((sam & 0x000F) == 0x0001,
                "TIKU_MPU_READ maps to 0x01");

    tiku_mpu_set_permissions(TIKU_MPU_SEG1, TIKU_MPU_WRITE);
    sam = tiku_mpu_arch_get_sam();
    TEST_ASSERT((sam & 0x000F) == 0x0002,
                "TIKU_MPU_WRITE maps to 0x02");

    tiku_mpu_set_permissions(TIKU_MPU_SEG1, TIKU_MPU_EXEC);
    sam = tiku_mpu_arch_get_sam();
    TEST_ASSERT((sam & 0x000F) == 0x0004,
                "TIKU_MPU_EXEC maps to 0x04");

    tiku_mpu_set_permissions(TIKU_MPU_SEG1, TIKU_MPU_RD_WR);
    sam = tiku_mpu_arch_get_sam();
    TEST_ASSERT((sam & 0x000F) == 0x0003,
                "TIKU_MPU_RD_WR maps to 0x03");

    tiku_mpu_set_permissions(TIKU_MPU_SEG1, TIKU_MPU_RD_EXEC);
    sam = tiku_mpu_arch_get_sam();
    TEST_ASSERT((sam & 0x000F) == 0x0005,
                "TIKU_MPU_RD_EXEC maps to 0x05");

    tiku_mpu_set_permissions(TIKU_MPU_SEG1, TIKU_MPU_ALL);
    sam = tiku_mpu_arch_get_sam();
    TEST_ASSERT((sam & 0x000F) == 0x0007,
                "TIKU_MPU_ALL maps to 0x07");
}

/*---------------------------------------------------------------------------*/
/* TEST 28: MPU RE-INIT RESTORES DEFAULTS AFTER CUSTOM PERMISSIONS           */
/*---------------------------------------------------------------------------*/

void test_mpu_reinit_restores(void)
{
    TEST_PRINT("\n--- Test: MPU Re-init Restores Defaults ---\n");

    tiku_mpu_init();

    /* Apply custom permissions to all segments */
    tiku_mpu_set_permissions(TIKU_MPU_SEG1, TIKU_MPU_ALL);
    tiku_mpu_set_permissions(TIKU_MPU_SEG2, TIKU_MPU_WRITE);
    tiku_mpu_set_permissions(TIKU_MPU_SEG3, TIKU_MPU_READ);

    TEST_ASSERT(tiku_mpu_arch_get_sam() != 0x0555,
                "SAM is non-default after custom permissions");

    /* Re-init should wipe custom permissions */
    tiku_mpu_init();

    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0555,
                "SAM restored to 0x0555 after re-init");
    TEST_ASSERT((tiku_mpu_arch_get_ctl() & 0x0001) != 0,
                "MPU still enabled after re-init");
}

/*---------------------------------------------------------------------------*/
/* TEST 29: MPU UNLOCK PRESERVES NON-WRITE BITS ON CUSTOM BASE               */
/*---------------------------------------------------------------------------*/

void test_mpu_unlock_custom_base(void)
{
    uint16_t saved, sam;

    TEST_PRINT("\n--- Test: MPU Unlock Custom Base ---\n");

    tiku_mpu_init();

    /* Set segment 1 to READ-only (0x1), seg 2 to EXEC (0x4), seg 3 to R+X (0x5) */
    tiku_mpu_set_permissions(TIKU_MPU_SEG1, TIKU_MPU_READ);
    tiku_mpu_set_permissions(TIKU_MPU_SEG2, TIKU_MPU_EXEC);
    /* seg 3 keeps default 0x5 */

    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0541,
                "custom base is 0x0541");

    saved = tiku_mpu_unlock_nvm();
    TEST_ASSERT(saved == 0x0541,
                "unlock returns custom base (0x0541)");

    /* Unlock should OR 0x0222 into each segment: 0x0541 | 0x0222 = 0x0763 */
    sam = tiku_mpu_arch_get_sam();
    TEST_ASSERT(sam == 0x0763,
                "SAM is 0x0763 after unlock (custom base | write)");

    /* Verify each segment got write bit without losing existing bits */
    TEST_ASSERT((sam & 0x000F) == 0x0003,
                "seg1: READ | WRITE = 0x3");
    TEST_ASSERT((sam & 0x00F0) == 0x0060,
                "seg2: EXEC | WRITE = 0x6");
    TEST_ASSERT((sam & 0x0F00) == 0x0700,
                "seg3: R+X | WRITE = 0x7");

    /* Lock restores original custom base */
    tiku_mpu_lock_nvm(saved);
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0541,
                "lock restores custom base 0x0541");
}

/*---------------------------------------------------------------------------*/
/* TEST 30: MPU SCOPED WRITE WITH CUSTOM BASE PERMISSIONS                     */
/*---------------------------------------------------------------------------*/

static void scoped_custom_cb(void *arg)
{
    scoped_write_ctx_t *ctx = (scoped_write_ctx_t *)arg;
    ctx->called = 1;
    ctx->sam_during = tiku_mpu_arch_get_sam();
}

void test_mpu_scoped_write_custom(void)
{
    scoped_write_ctx_t ctx;

    TEST_PRINT("\n--- Test: MPU Scoped Write Custom Base ---\n");

    tiku_mpu_init();

    /* Set segment 1 to EXEC-only before scoped write */
    tiku_mpu_set_permissions(TIKU_MPU_SEG1, TIKU_MPU_EXEC);

    /* baseline: seg1=0x4, seg2=0x5, seg3=0x5 → 0x0554 */
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0554,
                "custom base is 0x0554 before scoped write");

    ctx.called = 0;
    ctx.sam_during = 0;

    tiku_mpu_scoped_write(scoped_custom_cb, &ctx);

    TEST_ASSERT(ctx.called == 1,
                "callback invoked with custom base");
    /* During: 0x0554 | 0x0222 = 0x0776 */
    TEST_ASSERT(ctx.sam_during == 0x0776,
                "SAM had write bits during callback (0x0776)");
    /* After: restored to custom base */
    TEST_ASSERT(tiku_mpu_arch_get_sam() == 0x0554,
                "SAM restored to custom base 0x0554 after scoped write");
}

/*---------------------------------------------------------------------------*/
/* TEST 31: MPU VIOLATION DETECTION                                          */
/*---------------------------------------------------------------------------*/

/*
 * This test intentionally violates the MPU to verify that the violation
 * is caught and reported through the violation flags.
 *
 * On MSP430 hardware: MPUSEGIE is enabled so violations trigger a
 * System NMI instead of resetting the device. A write to protected
 * FRAM sets the violation flag in MPUCTL1.
 *
 * On host: the test uses a stub that checks SAM permissions and sets
 * the violation flag when a write to a locked segment is simulated.
 */

void test_mpu_violation_detect(void)
{
    uint16_t flags;
    uint16_t saved;

    TEST_PRINT("\n--- Test: MPU Violation Detection ---\n");

    tiku_mpu_init();
    tiku_mpu_enable_violation_nmi();
    tiku_mpu_clear_violation_flags();

    /* Baseline: no violation flags after init */
    TEST_ASSERT(tiku_mpu_get_violation_flags() == 0,
                "no violation flags after init");

    /*
     * PHASE 1 — Write while locked (should trigger violation)
     *
     * All segments are R+X (no write). Writing to protected memory
     * must set a violation flag.
     */
#ifdef PLATFORM_MSP430
    {
        /* 0x4400 is the start of main FRAM on FR5969 */
        volatile uint8_t *p = (volatile uint8_t *)0x4400;
        *p = 0xAA;  /* Blocked by MPU — violation flag set */
    }
#else
    test_mpu_trigger_seg_violation(TIKU_MPU_SEG1);
#endif

    flags = tiku_mpu_get_violation_flags();
    TEST_ASSERT(flags != 0,
                "violation flag set after write to locked segment");

    /* Clear and verify flags are gone */
    tiku_mpu_clear_violation_flags();
    TEST_ASSERT(tiku_mpu_get_violation_flags() == 0,
                "violation flags cleared successfully");

    /*
     * PHASE 2 — Write while unlocked (should NOT trigger violation)
     *
     * Unlock FRAM so all segments gain write permission. A write
     * should now succeed without setting any violation flag.
     */
    saved = tiku_mpu_unlock_nvm();
    tiku_mpu_clear_violation_flags();

#ifdef PLATFORM_MSP430
    {
        volatile uint8_t *p = (volatile uint8_t *)0x4400;
        *p = 0xBB;  /* Allowed — FRAM is unlocked */
    }
#else
    test_mpu_trigger_seg_violation(TIKU_MPU_SEG1);
#endif

    TEST_ASSERT(tiku_mpu_get_violation_flags() == 0,
                "no violation when segment is writable");

    tiku_mpu_lock_nvm(saved);
}
