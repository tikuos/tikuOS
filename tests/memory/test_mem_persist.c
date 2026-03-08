/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_mem_persist.c - Persistent FRAM key-value store tests
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
/* TEST 10: PERSIST INIT ON ZEROED STORE                                     */
/*---------------------------------------------------------------------------*/

void test_persist_init_zeroed(void)
{
    tiku_persist_store_t store;
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Persist Init on Zeroed Store ---\n");

    memset(&store, 0, sizeof(store));
    err = tiku_persist_init(&store);
    TEST_ASSERT(err == TIKU_MEM_OK, "persist_init returns OK");
    TEST_ASSERT(store.count == 0, "count is 0 on zeroed store");

    /* NULL store rejected */
    err = tiku_persist_init(NULL);
    TEST_ASSERT(err == TIKU_MEM_ERR_INVALID, "persist_init NULL rejected");
}

/*---------------------------------------------------------------------------*/
/* TEST 11: REGISTER INCREMENTS COUNT                                        */
/*---------------------------------------------------------------------------*/

void test_persist_register_and_count(void)
{
    tiku_persist_store_t store;
    uint8_t *fram_buf = test_nvm_pool;

    TEST_PRINT("\n--- Test: Persist Register and Count ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);
    TEST_ASSERT(store.count == 0, "count is 0 before register");

    tiku_persist_register(&store, "cfg", fram_buf, 32);
    TEST_ASSERT(store.count == 1, "count is 1 after first register");

    /* NULL args rejected */
    TEST_ASSERT(tiku_persist_register(NULL, "x", fram_buf, 4)
                == TIKU_MEM_ERR_INVALID, "NULL store rejected");
    TEST_ASSERT(tiku_persist_register(&store, NULL, fram_buf, 4)
                == TIKU_MEM_ERR_INVALID, "NULL key rejected");
    TEST_ASSERT(tiku_persist_register(&store, "x", NULL, 4)
                == TIKU_MEM_ERR_INVALID, "NULL buffer rejected");
    TEST_ASSERT(tiku_persist_register(&store, "x", fram_buf, 0)
                == TIKU_MEM_ERR_INVALID, "zero capacity rejected");
}

/*---------------------------------------------------------------------------*/
/* TEST 12: WRITE THEN READ BACK                                             */
/*---------------------------------------------------------------------------*/

void test_persist_write_read(void)
{
    tiku_persist_store_t store;
    uint8_t *fram_buf = test_nvm_pool;
    uint8_t read_buf[32];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};

    TEST_PRINT("\n--- Test: Persist Write Then Read ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);
    tiku_persist_register(&store, "cal", fram_buf, 32);

    err = tiku_persist_write(&store, "cal", data, sizeof(data));
    TEST_ASSERT(err == TIKU_MEM_OK, "write returns OK");

    memset(read_buf, 0, sizeof(read_buf));
    err = tiku_persist_read(&store, "cal", read_buf, sizeof(read_buf),
                            &out_len);
    TEST_ASSERT(err == TIKU_MEM_OK, "read returns OK");
    TEST_ASSERT(out_len == sizeof(data), "read length matches write length");
    TEST_ASSERT(memcmp(read_buf, data, sizeof(data)) == 0,
                "read data matches written data");
}

/*---------------------------------------------------------------------------*/
/* TEST 13: READ WITH TOO-SMALL BUFFER                                       */
/*---------------------------------------------------------------------------*/

void test_persist_read_small_buffer(void)
{
    tiku_persist_store_t store;
    uint8_t *fram_buf = test_nvm_pool;
    uint8_t tiny_buf[2];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    const uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};

    TEST_PRINT("\n--- Test: Persist Read Small Buffer ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);
    tiku_persist_register(&store, "big", fram_buf, 32);
    tiku_persist_write(&store, "big", data, sizeof(data));

    out_len = 0;
    err = tiku_persist_read(&store, "big", tiny_buf, sizeof(tiny_buf),
                            &out_len);
    TEST_ASSERT(err == TIKU_MEM_ERR_NOMEM, "read with small buffer returns ERR_NOMEM");
    TEST_ASSERT(out_len == sizeof(data), "out_len reports required size");
}

/*---------------------------------------------------------------------------*/
/* TEST 14: WRITE EXCEEDING CAPACITY                                         */
/*---------------------------------------------------------------------------*/

void test_persist_write_exceeds_capacity(void)
{
    tiku_persist_store_t store;
    uint8_t *fram_buf = test_nvm_pool;
    const uint8_t big_data[] = {1, 2, 3, 4, 5, 6, 7, 8};
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Persist Write Exceeds Capacity ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);
    tiku_persist_register(&store, "tiny", fram_buf, 4);

    err = tiku_persist_write(&store, "tiny", big_data, sizeof(big_data));
    TEST_ASSERT(err == TIKU_MEM_ERR_NOMEM,
                "write exceeding capacity returns ERR_NOMEM");
}

/*---------------------------------------------------------------------------*/
/* TEST 15: READ NON-EXISTENT KEY                                            */
/*---------------------------------------------------------------------------*/

void test_persist_read_not_found(void)
{
    tiku_persist_store_t store;
    uint8_t buf[16];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;

    TEST_PRINT("\n--- Test: Persist Read Not Found ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);

    err = tiku_persist_read(&store, "nope", buf, sizeof(buf), &out_len);
    TEST_ASSERT(err == TIKU_MEM_ERR_NOT_FOUND,
                "read non-existent key returns ERR_NOT_FOUND");
}

/*---------------------------------------------------------------------------*/
/* TEST 16: DELETE ENTRY                                                      */
/*---------------------------------------------------------------------------*/

void test_persist_delete(void)
{
    tiku_persist_store_t store;
    uint8_t *fram_buf = test_nvm_pool;
    uint8_t read_buf[16];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    const uint8_t data[] = {0xAA, 0xBB};

    TEST_PRINT("\n--- Test: Persist Delete ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);
    tiku_persist_register(&store, "del", fram_buf, 16);
    tiku_persist_write(&store, "del", data, sizeof(data));

    err = tiku_persist_delete(&store, "del");
    TEST_ASSERT(err == TIKU_MEM_OK, "delete returns OK");
    TEST_ASSERT(store.count == 0, "count decremented after delete");

    err = tiku_persist_read(&store, "del", read_buf, sizeof(read_buf),
                            &out_len);
    TEST_ASSERT(err == TIKU_MEM_ERR_NOT_FOUND,
                "read after delete returns ERR_NOT_FOUND");

    /* Delete non-existent key */
    err = tiku_persist_delete(&store, "gone");
    TEST_ASSERT(err == TIKU_MEM_ERR_NOT_FOUND,
                "delete non-existent key returns ERR_NOT_FOUND");
}

/*---------------------------------------------------------------------------*/
/* TEST 17: STORE FULL                                                        */
/*---------------------------------------------------------------------------*/

void test_persist_full(void)
{
    tiku_persist_store_t store;
    uint8_t *extra_buf = test_nvm_pool + TIKU_PERSIST_MAX_ENTRIES * 4;
    char key[TIKU_PERSIST_MAX_KEY_LEN];
    tiku_mem_err_t err;
    int i;

    TEST_PRINT("\n--- Test: Persist Store Full ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);

    /* Fill all slots — each gets a 4-byte slice of the NVM pool */
    for (i = 0; i < TIKU_PERSIST_MAX_ENTRIES; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        err = tiku_persist_register(&store, key,
                                    test_nvm_pool + i * 4, 4);
        TEST_ASSERT(err == TIKU_MEM_OK, "register slot succeeds");
    }

    TEST_ASSERT(store.count == TIKU_PERSIST_MAX_ENTRIES,
                "count equals max entries");

    /* One more should fail */
    err = tiku_persist_register(&store, "extra", extra_buf, 4);
    TEST_ASSERT(err == TIKU_MEM_ERR_FULL,
                "register beyond max returns ERR_FULL");
}

/*---------------------------------------------------------------------------*/
/* TEST 18: REBOOT SURVIVAL (REAL HARDWARE RESET)                            */
/*                                                                           */
/* This test requires real hardware. It uses two phases across an actual      */
/* device reset to verify that FRAM-backed persist data survives reboot:     */
/*                                                                           */
/*   Phase WRITE  (first boot):  write data, set phase flag, trigger BOR     */
/*   Phase VERIFY (after reboot): re-init store, read back, compare          */
/*                                                                           */
/* The phase flag, store, and FRAM buffer are all placed in FRAM via          */
/* __attribute__((section(".persistent"))) so they survive the reset.         */
/*---------------------------------------------------------------------------*/

#ifdef PLATFORM_MSP430

static uint8_t __attribute__((section(".persistent")))
    test_reboot_phase = TEST_PERSIST_REBOOT_PHASE_WRITE;

static tiku_persist_store_t __attribute__((section(".persistent")))
    test_reboot_store = {0};

static uint8_t __attribute__((section(".persistent")))
    test_reboot_fram_buf[16] = {0};

void test_persist_reboot_survival(void)
{
    uint8_t read_buf[16];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    const uint8_t data[] = {0x42, 0x43, 0x44};
    uint16_t mpu_state;

    TEST_PRINT("\n--- Test: Persist Reboot Survival ---\n");

    if (test_reboot_phase == TEST_PERSIST_REBOOT_PHASE_WRITE) {
        /*-- Phase 0: write data and trigger a real reset ----------------*/
        TEST_PRINT("  Phase: WRITE (first boot)\n");

        mpu_state = tiku_mpu_unlock_nvm();

        memset(&test_reboot_store, 0, sizeof(test_reboot_store));
        tiku_persist_init(&test_reboot_store);

        tiku_persist_register(&test_reboot_store, "boot",
                              test_reboot_fram_buf,
                              sizeof(test_reboot_fram_buf));
        tiku_persist_write(&test_reboot_store, "boot", data, sizeof(data));

        test_reboot_phase = TEST_PERSIST_REBOOT_PHASE_VERIFY;

        tiku_mpu_lock_nvm(mpu_state);

        TEST_PRINT("  Triggering software reset...\n");
        PMMCTL0 = PMMPW | PMMSWPOR;
        /* Device resets — execution never reaches here */

    } else {
        /*-- Phase 1: verify data survived the reboot -------------------*/
        TEST_PRINT("  Phase: VERIFY (after reboot)\n");

        mpu_state = tiku_mpu_unlock_nvm();

        tiku_persist_init(&test_reboot_store);

        TEST_ASSERT(test_reboot_store.count == 1,
                    "count is 1 after reboot");

        memset(read_buf, 0, sizeof(read_buf));
        err = tiku_persist_read(&test_reboot_store, "boot", read_buf,
                                sizeof(read_buf), &out_len);
        TEST_ASSERT(err == TIKU_MEM_OK, "read after reboot returns OK");
        TEST_ASSERT(out_len == sizeof(data),
                    "data length preserved after reboot");
        TEST_ASSERT(memcmp(read_buf, data, sizeof(data)) == 0,
                    "data intact after reboot");

        /* Reset phase for next test run */
        test_reboot_phase = TEST_PERSIST_REBOOT_PHASE_WRITE;

        tiku_mpu_lock_nvm(mpu_state);
    }
}

/*---------------------------------------------------------------------------*/
/* TEST 21: POWER-CYCLE SURVIVAL (MANUAL POWER REMOVAL)                      */
/*                                                                           */
/* Unlike Test 18 (software reset), this test requires the user to           */
/* physically disconnect power from the board. A software reset (POR/BOR)    */
/* never actually removes Vcc from the MCU, so FRAM is trivially             */
/* preserved. A true power-cycle drops Vcc to 0 V, which is the real-world  */
/* scenario for battery-swaps, brown-outs, and field power interruptions.    */
/*                                                                           */
/*   Phase WRITE  (first boot):  write data, set phase flag, then prompt     */
/*                                the user to remove power and halt.         */
/*   Phase VERIFY (after power restore): re-init store, read back, compare   */
/*---------------------------------------------------------------------------*/

static uint8_t __attribute__((section(".persistent")))
    test_pwrcycle_phase = TEST_PERSIST_PWRCYCLE_PHASE_WRITE;

static tiku_persist_store_t __attribute__((section(".persistent")))
    test_pwrcycle_store = {0};

static uint8_t __attribute__((section(".persistent")))
    test_pwrcycle_fram_buf[16] = {0};

void test_persist_powercycle_survival(void)
{
    uint8_t read_buf[16];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    const uint8_t data[] = {0xCA, 0xFE, 0xBA, 0xBE, 0x01};
    uint16_t mpu_state;

    TEST_PRINT("\n--- Test: Persist Power-Cycle Survival ---\n");

    if (test_pwrcycle_phase == TEST_PERSIST_PWRCYCLE_PHASE_WRITE) {
        /*-- Phase 0: write data, then wait for user to remove power -----*/
        TEST_PRINT("  Phase: WRITE (first boot)\n");

        mpu_state = tiku_mpu_unlock_nvm();

        memset(&test_pwrcycle_store, 0, sizeof(test_pwrcycle_store));
        tiku_persist_init(&test_pwrcycle_store);

        tiku_persist_register(&test_pwrcycle_store, "pwr",
                              test_pwrcycle_fram_buf,
                              sizeof(test_pwrcycle_fram_buf));
        tiku_persist_write(&test_pwrcycle_store, "pwr",
                           data, sizeof(data));

        test_pwrcycle_phase = TEST_PERSIST_PWRCYCLE_PHASE_VERIFY;

        tiku_mpu_lock_nvm(mpu_state);

        TEST_PRINT("  Data written to FRAM.\n");
        TEST_PRINT("\n");
        TEST_PRINT("  *** ACTION REQUIRED ***\n");
        TEST_PRINT("  Physically REMOVE POWER from the board now.\n");
        TEST_PRINT("  (Unplug USB / disconnect battery — do NOT just\n");
        TEST_PRINT("   press reset. A reset does not cut Vcc.)\n");
        TEST_PRINT("  Wait at least 5 seconds, then restore power.\n");
        TEST_PRINT("  The test will continue automatically on next boot.\n");
        TEST_PRINT("\n");
        TEST_PRINT("  LED1 blinking = waiting for power removal...\n");

        /* Blink LED1 until user physically removes power */
        for (;;) {
            tiku_common_led1_toggle();
            tiku_common_delay_ms(500);
        }
        /* Device loses power — execution never reaches here */

    } else {
        /*-- Phase 1: verify data survived full power-cycle --------------*/
        TEST_PRINT("  Phase: VERIFY (after power restore)\n");
        TEST_PRINT("  Waiting 10 seconds for console connection...\n");
        tiku_common_delay_ms(10000);
        TEST_PRINT("  Starting verification.\n");

        mpu_state = tiku_mpu_unlock_nvm();

        tiku_persist_init(&test_pwrcycle_store);

        TEST_ASSERT(test_pwrcycle_store.count == 1,
                    "count is 1 after power-cycle");

        memset(read_buf, 0, sizeof(read_buf));
        err = tiku_persist_read(&test_pwrcycle_store, "pwr", read_buf,
                                sizeof(read_buf), &out_len);
        TEST_ASSERT(err == TIKU_MEM_OK,
                    "read after power-cycle returns OK");
        TEST_ASSERT(out_len == sizeof(data),
                    "data length preserved after power-cycle");
        TEST_ASSERT(memcmp(read_buf, data, sizeof(data)) == 0,
                    "data intact after power-cycle");

        /* Reset phase for next test run */
        test_pwrcycle_phase = TEST_PERSIST_PWRCYCLE_PHASE_WRITE;

        tiku_mpu_lock_nvm(mpu_state);

        TEST_PRINT("  FRAM data survived full power-cycle!\n");
    }
}

#else /* !PLATFORM_MSP430 */

void test_persist_reboot_survival(void)
{
    TEST_PRINT("\n--- Test: Persist Reboot Survival ---\n");
    TEST_PRINT("  SKIP: requires real hardware (PLATFORM_MSP430)\n");
}

void test_persist_powercycle_survival(void)
{
    TEST_PRINT("\n--- Test: Persist Power-Cycle Survival ---\n");
    TEST_PRINT("  SKIP: requires real hardware (PLATFORM_MSP430)\n");
}

#endif /* PLATFORM_MSP430 */

/*---------------------------------------------------------------------------*/
/* TEST 19: WEAR CHECK                                                        */
/*---------------------------------------------------------------------------*/

void test_persist_wear_check(void)
{
    tiku_persist_store_t store;
    uint8_t *fram_buf = test_nvm_pool;
    uint32_t wc;
    int result;
    tiku_persist_entry_t *entry;

    TEST_PRINT("\n--- Test: Persist Wear Check ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);
    tiku_persist_register(&store, "wear", fram_buf, 8);

    result = tiku_persist_wear_check(&store, "wear", &wc);
    TEST_ASSERT(result == 0, "wear check returns 0 initially");
    TEST_ASSERT(wc == 0, "write_count is 0 initially");

    /* Manually set write_count above threshold */
    entry = &store.entries[0];
    entry->write_count = TIKU_PERSIST_WEAR_THRESHOLD + 1;

    result = tiku_persist_wear_check(&store, "wear", &wc);
    TEST_ASSERT(result == 1, "wear check returns 1 above threshold");
    TEST_ASSERT(wc == TIKU_PERSIST_WEAR_THRESHOLD + 1,
                "write_count matches set value");

    /* Non-existent key */
    result = tiku_persist_wear_check(&store, "nope", &wc);
    TEST_ASSERT(result == TIKU_MEM_ERR_NOT_FOUND,
                "wear check non-existent key returns ERR_NOT_FOUND");
}

/*---------------------------------------------------------------------------*/
/* TEST 20: REGISTER SAME KEY TWICE                                           */
/*---------------------------------------------------------------------------*/

void test_persist_register_twice(void)
{
    tiku_persist_store_t store;
    uint8_t *fram_buf1 = test_nvm_pool;
    uint8_t *fram_buf2 = test_nvm_pool + 128;
    uint8_t read_buf[32];
    tiku_mem_arch_size_t out_len;
    tiku_mem_err_t err;
    const uint8_t data[] = {0x11, 0x22, 0x33};

    TEST_PRINT("\n--- Test: Persist Register Same Key Twice ---\n");

    memset(&store, 0, sizeof(store));
    tiku_persist_init(&store);

    tiku_persist_register(&store, "dup", fram_buf1, 16);
    tiku_persist_write(&store, "dup", data, sizeof(data));

    /* Re-register with different buffer — should update pointer, keep data */
    err = tiku_persist_register(&store, "dup", fram_buf2, 32);
    TEST_ASSERT(err == TIKU_MEM_OK, "re-register same key returns OK");
    TEST_ASSERT(store.count == 1, "count stays 1 (no duplicate slot)");

    /* Data should still be readable (value_len preserved) */
    memset(read_buf, 0, sizeof(read_buf));
    err = tiku_persist_read(&store, "dup", read_buf, sizeof(read_buf),
                            &out_len);
    TEST_ASSERT(err == TIKU_MEM_OK, "read after re-register returns OK");
    TEST_ASSERT(out_len == sizeof(data),
                "value_len preserved after re-register");
}
