/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_test_config.h - Test framework configuration
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDI TIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file tiku_test_config.h
 * @brief Test configuration flags for enabling/disabling test modules
 *
 * Set TEST_ENABLE to 1 and enable individual test flags to run
 * specific tests from the test suite.
 */

#ifndef TIKU_TEST_CONFIG_H_
#define TIKU_TEST_CONFIG_H_

/**
 * @defgroup TIKU_TEST_CONFIG Test Configuration Flags
 * @brief Configuration flags for enabling/disabling test modules
 * @{
 */

/** Master test enable - set to 1 to run test suite from main */
#define TEST_ENABLE 0

/*---------------------------------------------------------------------------*/
/* WATCHDOG TESTS                                                            */
/*---------------------------------------------------------------------------*/

/** Enable watchdog timer tests */
#define TEST_WATCHDOG 0

/**
 * Enable basic watchdog operation test.
 * Configures the watchdog with ~1s timeout using ACLK source, then
 * repeatedly kicks it every 500ms to prevent reset. Verifies the system
 * stays alive by toggling LED1 and counting successful kicks over 30
 * iterations. Proves the kick mechanism prevents timeout reset.
 */
#define TEST_WDT_BASIC 0

/**
 * Enable watchdog pause/resume test.
 * Pauses the watchdog timer, waits longer than the configured timeout
 * without kicking, then resumes. Verifies the system does not reset
 * during the pause window, confirming pause truly halts the countdown.
 */
#define TEST_WDT_PAUSE_RESUME 0

/**
 * Enable interval timer mode test.
 * Configures the watchdog in interval timer mode (periodic interrupt
 * instead of system reset). Verifies the interval ISR fires at the
 * expected rate by counting interrupts over a measurement window.
 */
#define TEST_WDT_INTERVAL 0

/**
 * Enable watchdog timeout test.
 * WARNING: This test intentionally does NOT kick the watchdog, causing
 * a device reset. After reset, checks the reset-cause register to
 * confirm the watchdog was responsible. This test is destructive and
 * will reset the device — enable only when specifically testing
 * watchdog reset behavior.
 */
#define TEST_WDT_TIMEOUT 0

/*---------------------------------------------------------------------------*/
/* CPU CLOCK TESTS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * Enable CPU clock configuration test.
 * Configures MSP430 port P3.4 as a clock output pin by setting the
 * appropriate GPIO and special-function registers, exposing the CPU
 * clock signal on a physical pin for external measurement with an
 * oscilloscope or frequency counter.
 */
#define TEST_CPUCLOCK 0

/*---------------------------------------------------------------------------*/
/* PROCESS TESTS                                                             */
/*---------------------------------------------------------------------------*/

/**
 * Enable basic process lifecycle test.
 * Starts a process and verifies it reaches initialization after INIT
 * event delivery, continues in running state after a yield, advances
 * through a second phase via a CONTINUE event, and terminates
 * automatically at the PROCESS_END macro without explicit cleanup.
 */
#define TEST_PROCESS_LIFECYCLE 0

/**
 * Enable event posting test.
 * Starts a process with an event queue, posts TEST_NUM_EVENTS custom
 * events, drains the scheduler, and asserts all posted events were
 * delivered and processed. Verifies tiku_process_post() return values
 * indicate success for each post.
 */
#define TEST_PROCESS_EVENTS 0

/**
 * Enable cooperative yield test.
 * Validates protothread yielding across three execution phases: first
 * INIT event reaches phase 1, posting TIKU_EVENT_CONTINUE advances to
 * phase 2, a second continuation reaches phase 3, and the process
 * terminates automatically without an explicit exit call.
 */
#define TEST_PROCESS_YIELD 0

/**
 * Enable broadcast event test.
 * Starts two independent processes and posts a TIKU_PROCESS_BROADCAST
 * event. Asserts both processes independently receive the broadcast.
 * Repeats the broadcast to verify correct delivery across multiple
 * broadcast cycles.
 */
#define TEST_PROCESS_BROADCAST 0

/**
 * Enable process poll test.
 * Requests multiple poll events (TEST_NUM_POLLS) on a target process
 * via tiku_process_poll(), drains the scheduler, and asserts all
 * TIKU_EVENT_POLL events were received by the target process.
 */
#define TEST_PROCESS_POLL 0

/**
 * Enable queue query function test.
 * Exhaustively tests queue state inspection: verifies initial empty
 * state, tracks queue length and available space after process start,
 * fills the queue to capacity and asserts post failure when full,
 * monitors capacity changes as events drain, and validates
 * is_running status before and after process exit.
 */
#define TEST_PROCESS_QUEUE 0

/**
 * Enable process local storage test.
 * Tests two local storage patterns: TIKU_PROCESS_WITH_LOCAL with the
 * TIKU_LOCAL() macro verifies local state (counter, phase) persists
 * across yields and mutations. TIKU_PROCESS_TYPED with a generated
 * accessor function confirms typed local storage survives yields and
 * maintains correct values.
 */
#define TEST_PROCESS_LOCAL 0

/**
 * Enable broadcast exit safety test.
 * Starts three processes and posts a broadcast event. The middle
 * process (B) exits during event delivery. Asserts both surviving
 * processes (A and C) still receive the broadcast event and process B
 * successfully exits. Validates safe iteration over the process list
 * when a process removes itself mid-broadcast.
 */
#define TEST_PROCESS_BROADCAST_EXIT 0

/**
 * Enable graceful exit vs force exit test.
 * Compares two exit modes: TIKU_EVENT_EXIT allows the process to
 * perform cleanup actions before calling TIKU_PROCESS_EXIT(), while
 * TIKU_EVENT_FORCE_EXIT kills the process unconditionally even if
 * the thread body does not explicitly exit. Verifies cleanup-done
 * flag is set only for graceful exit, and both modes leave
 * is_running as false.
 */
#define TEST_PROCESS_GRACEFUL_EXIT 0

/**
 * Enable current process cleared after dispatch test.
 * Verifies TIKU_THIS() returns the correct process pointer inside
 * the thread body during event dispatch, and asserts TIKU_THIS() is
 * cleared to NULL after dispatch returns. Prevents stale references
 * to the dispatched process from persisting between scheduler runs.
 */
#define TEST_PROCESS_CURRENT_CLEARED 0

/** Auto-derived: true if any process test is enabled */
#define TEST_PROCESS (TEST_PROCESS_LIFECYCLE || TEST_PROCESS_EVENTS ||    \
                      TEST_PROCESS_YIELD || TEST_PROCESS_BROADCAST ||     \
                      TEST_PROCESS_POLL || TEST_PROCESS_QUEUE ||          \
                      TEST_PROCESS_LOCAL || TEST_PROCESS_BROADCAST_EXIT ||\
                      TEST_PROCESS_GRACEFUL_EXIT ||                       \
                      TEST_PROCESS_CURRENT_CLEARED)

/*---------------------------------------------------------------------------*/
/* TIMER TESTS                                                               */
/*---------------------------------------------------------------------------*/

/** Enable timer subsystem tests */
#define TEST_TIMER 0

/**
 * Enable event timer test.
 * Sets an event timer (tiku_timer_set_event) with TEST_TIMER_INTERVAL
 * duration, polls the system while advancing clock ticks, and asserts
 * TIKU_EVENT_TIMER fires when the interval expires. Confirms the
 * timer reports expired status after firing.
 */
#define TEST_TIMER_EVENT 0

/**
 * Enable callback timer test.
 * Sets a callback timer with TEST_TIMER_SHORT duration, polls through
 * clock ticks and process scheduler, and verifies the callback function
 * fires exactly once after the configured interval elapses.
 */
#define TEST_TIMER_CALLBACK 0

/**
 * Enable periodic timer test.
 * Sets a callback that manually reschedules itself via tiku_timer_reset
 * for TEST_TIMER_PERIODIC_CNT iterations. Polls the scheduler and
 * asserts all periodic callbacks fire at the expected intervals. Verifies
 * the timer stops after the final iteration with no further callbacks.
 */
#define TEST_TIMER_PERIODIC 0

/**
 * Enable timer stop test.
 * Sets a callback timer, verifies it reports active status, then stops
 * it before expiration with tiku_timer_stop(). Asserts expired status
 * afterward and confirms the callback never fires despite polling well
 * past the original timeout duration.
 */
#define TEST_TIMER_STOP 0

/**
 * Enable hardware timer basic test.
 * Schedules a single-shot hardware timer callback 100ms in the future
 * (TIKU_HTIMER_PERIOD), busy-waits for ISR execution, verifies the
 * callback fired at the correct time, and asserts no timer remains
 * scheduled afterward.
 */
#define TEST_HTIMER_BASIC 0

/**
 * Enable hardware timer periodic test.
 * Schedules an initial hardware timer tick, then self-reschedules from
 * within the callback for TEST_HTIMER_REPEAT_CNT subsequent ticks using
 * the scheduled (not actual) time as the base for drift-free operation.
 * Busy-waits for all iterations and confirms nothing remains scheduled
 * after the final tick.
 */
#define TEST_HTIMER_PERIODIC 0

/*---------------------------------------------------------------------------*/
/* MEMORY TESTS (ARENA ALLOCATOR)                                            */
/*---------------------------------------------------------------------------*/

/**
 * Enable arena creation and initial stats test.
 * Creates a 64-byte arena, asserts it is marked active with the correct
 * ID, and verifies initial stats report the expected total_bytes with
 * used_bytes, peak_bytes, and alloc_count all at zero.
 */
#define TEST_MEM_CREATE 0

/**
 * Enable basic allocation and pointer correctness test.
 * Allocates two sequential blocks (8 and 4 bytes), verifies the first
 * starts at the buffer base and the second follows immediately after.
 * Writes distinct patterns (0xAA, 0xBB) to confirm no overlap, and
 * checks used_bytes (12) and alloc_count (2) track correctly.
 */
#define TEST_MEM_ALLOC 0

/**
 * Enable alignment of odd-sized requests test.
 * Requests odd-sized allocations (3, 1, 5 bytes) and verifies each is
 * rounded up to the platform's TIKU_MEM_ARCH_ALIGNMENT. Checks that
 * sequential allocations start at properly aligned offsets and all
 * returned pointers satisfy the alignment requirement.
 */
#define TEST_MEM_ALIGNMENT 0

/**
 * Enable arena full returns NULL test.
 * Creates a 4*alignment-sized arena, allocates 3*alignment bytes
 * (leaving exactly alignment bytes free), verifies a 2*alignment
 * request fails (NULL), an exact alignment request succeeds, and
 * a final 1-byte request fails when the arena is completely full.
 */
#define TEST_MEM_FULL 0

/**
 * Enable reset restores offset but preserves peak test.
 * Allocates 20 bytes across two allocs, verifies used_bytes and
 * alloc_count, resets the arena, asserts both return to zero while
 * peak_bytes is preserved at 20, and confirms reallocation starts
 * at the buffer base.
 */
#define TEST_MEM_RESET 0

/**
 * Enable peak tracking across resets test.
 * Runs three allocate-reset cycles with varying sizes (12, 32, 8
 * bytes). Verifies peak_bytes monotonically tracks the lifetime
 * maximum of 32 even after smaller allocations in later cycles,
 * confirming peak survives resets and never decreases.
 */
#define TEST_MEM_PEAK 0

/**
 * Enable null and zero-size inputs rejected test.
 * Passes NULL arena, NULL buffer, NULL stats pointers, and zero-size
 * allocation requests to all public arena API functions. Asserts each
 * returns the appropriate error code (TIKU_MEM_ERR_INVALID) or NULL
 * pointer without crashing or corrupting state.
 */
#define TEST_MEM_INVALID 0

/**
 * Enable secure reset zeros memory test.
 * Fills a 32-byte arena with 0xAA pattern, calls secure_reset, and
 * verifies every byte is zeroed (volatile wipe prevents compiler
 * optimization). Confirms state is reset (used/alloc to zero, peak
 * preserved) and reallocation starts at the buffer base. Also
 * verifies NULL arena is rejected.
 */
#define TEST_MEM_SECURE_RESET 0

/**
 * Enable two independent arenas test.
 * Creates two arenas backed by separate buffer regions with different
 * sizes (32 and 64 bytes). Allocates from each independently and
 * verifies isolated used_bytes, total_bytes, and alloc_count values.
 * Resets one arena and confirms the other remains completely unaffected.
 */
#define TEST_MEM_TWO_ARENAS 0

/** Auto-derived: true if any arena memory test is enabled */
#define TEST_MEM (TEST_MEM_CREATE || TEST_MEM_ALLOC ||                     \
                  TEST_MEM_ALIGNMENT || TEST_MEM_FULL ||                   \
                  TEST_MEM_RESET || TEST_MEM_PEAK ||                       \
                  TEST_MEM_INVALID || TEST_MEM_SECURE_RESET ||             \
                  TEST_MEM_TWO_ARENAS)

/*---------------------------------------------------------------------------*/
/* PERSISTENT STORE TESTS                                                    */
/*---------------------------------------------------------------------------*/

/**
 * Enable persist init on zeroed store test.
 * Initializes a persistent store on zeroed NVM memory, asserts
 * tiku_persist_init returns OK with an entry count of zero, and
 * verifies that a NULL store pointer is properly rejected.
 */
#define TEST_PERSIST_INIT        0

/**
 * Enable persist register and count test.
 * Registers one key-value entry with a FRAM-backed buffer, verifies
 * the entry count increments to 1, and asserts that NULL store, NULL
 * key, NULL buffer, and zero-capacity arguments are all rejected
 * with TIKU_MEM_ERR_INVALID.
 */
#define TEST_PERSIST_REGISTER    0

/**
 * Enable persist write then read back test.
 * Registers a key, writes a 4-byte data payload, reads it back into
 * a separate buffer with output length tracking, and asserts the
 * read data matches the written data byte-for-byte with the correct
 * reported length.
 */
#define TEST_PERSIST_WRITE_READ  0

/**
 * Enable persist read with too-small buffer test.
 * Writes 8 bytes to a key, then attempts to read into a 2-byte
 * buffer. Asserts the read returns TIKU_MEM_ERR_NOMEM and the
 * out_len output reports the full required size (8) so the caller
 * knows how large a buffer to provide.
 */
#define TEST_PERSIST_SMALL_BUF   0

/**
 * Enable persist write exceeding capacity test.
 * Registers a key with 4-byte FRAM capacity, then attempts to write
 * 8 bytes. Asserts the write returns TIKU_MEM_ERR_NOMEM, preventing
 * buffer overrun into adjacent FRAM regions.
 */
#define TEST_PERSIST_OVERFLOW    0

/**
 * Enable persist read non-existent key test.
 * Attempts to read a key that was never registered and asserts the
 * read returns TIKU_MEM_ERR_NOT_FOUND.
 */
#define TEST_PERSIST_NOT_FOUND   0

/**
 * Enable persist delete entry test.
 * Writes an entry, deletes it, verifies the entry count decrements,
 * asserts a subsequent read returns NOT_FOUND, and confirms that
 * deleting the same key again also returns NOT_FOUND (no double-free
 * corruption).
 */
#define TEST_PERSIST_DELETE      0

/**
 * Enable persist store full test.
 * Fills all TIKU_PERSIST_MAX_ENTRIES slots with individually registered
 * keys backed by separate FRAM buffers, verifies the count equals the
 * maximum, and asserts one additional registration returns
 * TIKU_MEM_ERR_FULL.
 */
#define TEST_PERSIST_FULL        0

/**
 * Enable persist reboot survival test (software reset).
 * Two-phase test across a hardware reset. Phase WRITE: initializes
 * the FRAM-backed store, writes test data, then triggers a software
 * reset (PMMSWPOR). Phase VERIFY: after reboot, re-initializes and
 * asserts the data survived the reset with correct entry count and
 * byte-for-byte match. Requires on-target execution.
 */
#define TEST_PERSIST_REBOOT      0

/**
 * Enable persist power-cycle survival test.
 * Two-phase test across a full power removal. Phase WRITE: initializes
 * the FRAM-backed store, writes test data, then prompts the user to
 * physically disconnect power. Phase VERIFY: after power restoration,
 * re-initializes and asserts all data survived with correct count and
 * exact byte match. Requires manual intervention and on-target
 * execution.
 */
#define TEST_PERSIST_POWERCYCLE  0

/**
 * Enable persist wear check test.
 * Verifies initial write_count is zero, manually elevates the counter
 * above TIKU_PERSIST_WEAR_THRESHOLD, and asserts tiku_persist_wear_check
 * returns 1 (worn) with the correct write count reported. Also verifies
 * wear_check returns NOT_FOUND for non-existent keys.
 */
#define TEST_PERSIST_WEAR        0

/**
 * Enable persist register same key twice test.
 * Registers a key with buffer1, writes data, then re-registers the
 * same key with buffer2. Asserts the entry count stays at 1 (no
 * duplicate created) and the previously written data is still readable
 * with the correct length preserved.
 */
#define TEST_PERSIST_DUP_KEY     0

/*---------------------------------------------------------------------------*/
/* MPU TESTS                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * Enable MPU init defaults test.
 * Initializes the MPU and asserts the Segment Access Mode register
 * (SAM) is 0x0555 (all three segments set to read+execute, no write)
 * and the MPU enable bit is set in the control register.
 */
#define TEST_MPU_INIT            0

/**
 * Enable MPU unlock/lock FRAM test.
 * Calls tiku_mpu_unlock_nvm() and asserts the returned saved state
 * is 0x0555 (default). Verifies the unlocked SAM becomes 0x0777
 * (write bits added to all segments). Calls tiku_mpu_lock_nvm() with
 * the saved value and confirms SAM is restored to 0x0555.
 */
#define TEST_MPU_UNLOCK_LOCK     0

/**
 * Enable MPU set permissions test.
 * Sets segment 3 to RD_WR (0x3) via tiku_mpu_set_permissions() and
 * verifies only the segment 3 nybble (bits [11:8]) changes while
 * segments 1 and 2 remain at their default R+X (0x5) values.
 */
#define TEST_MPU_SET_PERM        0

/**
 * Enable MPU scoped write test.
 * Calls tiku_mpu_scoped_write() with a callback that captures the
 * SAM register during execution. Asserts the callback was invoked,
 * the write bits (0x0222) were set during the callback, and SAM is
 * restored to 0x0555 after the callback returns.
 */
#define TEST_MPU_SCOPED          0

/**
 * Enable MPU lock/unlock idempotency test.
 * Locks when already locked and verifies the state is unchanged.
 * Double-unlocks and asserts the second unlock returns the
 * already-unlocked 0x0777 state. Restores with the original saved
 * state and confirms correct final SAM value.
 */
#define TEST_MPU_IDEMPOTENT      0

/**
 * Enable MPU all segments independent test.
 * Sets each of the three segments to different permissions (READ,
 * RD_WR, ALL), then modifies one segment and verifies the other two
 * remain untouched. Proves per-segment bit manipulation does not
 * interfere with neighboring nybbles in the SAM register.
 */
#define TEST_MPU_ALL_SEGMENTS    0

/**
 * Enable MPU permission flags test.
 * Applies each permission enum value to segment 1 and reads back the
 * low nybble: READ->0x01, WRITE->0x02, EXEC->0x04, RD_WR->0x03,
 * RD_EXEC->0x05, ALL->0x07. Verifies the enum values map correctly
 * to hardware bit positions.
 */
#define TEST_MPU_PERM_FLAGS      0

/**
 * Enable MPU re-init restores defaults test.
 * Applies custom permissions to all three segments, then calls
 * tiku_mpu_init() again. Asserts SAM returns to the default 0x0555
 * and the MPU enable bit is still set, confirming re-initialization
 * wipes any custom permission configuration.
 */
#define TEST_MPU_REINIT          0

/**
 * Enable MPU unlock with custom base permissions test.
 * Sets segments to non-default values (READ, EXEC, default), then
 * calls unlock_nvm(). Asserts the OR of write bits (0x0222) preserves
 * the existing non-write permission bits. Locks with the saved state
 * and confirms the custom base permissions are fully restored.
 */
#define TEST_MPU_UNLOCK_CUSTOM   0

/**
 * Enable MPU scoped write with custom base test.
 * Sets segment 1 to EXEC only (base SAM = 0x0554), calls
 * tiku_mpu_scoped_write(), and verifies write bits are added during
 * the callback (SAM = 0x0776). Asserts the custom base (0x0554) is
 * fully restored after the callback returns.
 */
#define TEST_MPU_SCOPED_CUSTOM   0

/**
 * Enable MPU violation detection test.
 * Initializes MPU, enables violation NMI, clears flags, then triggers
 * a write to a locked segment (hardware write or host stub). Asserts
 * the violation flag is set. Clears flags, unlocks NVM, triggers the
 * same write, and asserts no violation flag — proving unlocked writes
 * do not trigger violations.
 */
#define TEST_MPU_VIOLATION       0

/** Auto-derived: true if any MPU test is enabled */
#define TEST_MPU (TEST_MPU_INIT || TEST_MPU_UNLOCK_LOCK ||                  \
                  TEST_MPU_SET_PERM || TEST_MPU_SCOPED ||                    \
                  TEST_MPU_IDEMPOTENT || TEST_MPU_ALL_SEGMENTS ||            \
                  TEST_MPU_PERM_FLAGS || TEST_MPU_REINIT ||                  \
                  TEST_MPU_UNLOCK_CUSTOM || TEST_MPU_SCOPED_CUSTOM ||        \
                  TEST_MPU_VIOLATION)

/** Auto-derived: true if any persistent store test is enabled */
#define TEST_PERSIST (TEST_PERSIST_INIT || TEST_PERSIST_REGISTER ||        \
                      TEST_PERSIST_WRITE_READ || TEST_PERSIST_SMALL_BUF || \
                      TEST_PERSIST_OVERFLOW || TEST_PERSIST_NOT_FOUND ||   \
                      TEST_PERSIST_DELETE || TEST_PERSIST_FULL ||          \
                      TEST_PERSIST_REBOOT || TEST_PERSIST_POWERCYCLE ||   \
                      TEST_PERSIST_WEAR || TEST_PERSIST_DUP_KEY)

/*---------------------------------------------------------------------------*/
/* REGION REGISTRY TESTS                                                     */
/*---------------------------------------------------------------------------*/

/**
 * Enable region init valid test.
 * Retrieves the platform's memory region table via the arch HAL,
 * passes it to tiku_region_init(), asserts OK return, and verifies
 * the table contains at least 2 entries (SRAM and NVM regions).
 */
#define TEST_REGION_INIT         0

/**
 * Enable region init invalid (overlapping / NULL) test.
 * Passes NULL table pointer (rejected), zero count (rejected), and a
 * hand-crafted table with two SRAM regions whose address ranges
 * overlap (rejected). Restores valid state afterward for subsequent
 * tests.
 */
#define TEST_REGION_INIT_INVALID 0

/**
 * Enable region contains basic check test.
 * Verifies SRAM pool buffer is recognized as SRAM, NVM pool buffer is
 * recognized as NVM, NULL pointer is rejected, and zero size is
 * rejected. Covers the basic happy-path and trivial-rejection cases.
 */
#define TEST_REGION_CONTAINS     0

/**
 * Enable region contains wrong type test.
 * Asserts SRAM buffer is NOT recognized as NVM, NVM buffer is NOT
 * recognized as SRAM, and SRAM buffer is NOT recognized as
 * PERIPHERAL. Proves type-specific matching rejects correct-address
 * but wrong-type queries.
 */
#define TEST_REGION_WRONG_TYPE   0

/**
 * Enable region contains boundary conditions test.
 * Tests exact region size (passes), one byte past the end (fails),
 * range starting inside but extending past the end (fails), single
 * byte at the first address (passes), and single byte at the last
 * valid address (passes). Exercises half-open interval arithmetic.
 */
#define TEST_REGION_BOUNDARY     0

/**
 * Enable region contains pointer overflow test.
 * Crafts a pointer near the top of the address space (~0xFFFA on
 * 16-bit) with a size that causes ptr+size to wrap around to zero.
 * Asserts the overflow guard rejects the wrapping range, preventing
 * false containment matches on 16-bit targets.
 */
#define TEST_REGION_OVERFLOW     0

/**
 * Enable region claim and unclaim test.
 * Claims a 64-byte SRAM range, unclaims it, asserts unclaiming the
 * same pointer again returns NOT_FOUND, verifies NULL unclaim returns
 * NOT_FOUND, and confirms the range can be re-claimed after release.
 */
#define TEST_REGION_CLAIM        0

/**
 * Enable region claim overlap detection test.
 * Claims [0, 64), then asserts overlapping [32, 96) is rejected,
 * duplicate [0, 64) is rejected, non-overlapping [128, 192) succeeds,
 * and adjacent [64, 128) succeeds (half-open intervals do not overlap
 * at the boundary).
 */
#define TEST_REGION_CLAIM_OVERLAP 0

/**
 * Enable region claim unknown memory test.
 * Attempts to claim an address just past the SRAM pool end (outside
 * all declared regions), a NULL pointer, and zero size. All three
 * are rejected with TIKU_MEM_ERR_INVALID, proving claims must fall
 * within known memory.
 */
#define TEST_REGION_CLAIM_UNKNOWN 0

/**
 * Enable region claim table full test.
 * Fills all TIKU_REGION_MAX_CLAIMS slots with non-overlapping 8-byte
 * ranges, then asserts one additional claim returns TIKU_MEM_ERR_FULL.
 */
#define TEST_REGION_CLAIM_FULL   0

/**
 * Enable region get type lookup test.
 * Queries an address in the SRAM pool (returns SRAM), an address in
 * the NVM pool (returns NVM), the first byte of SRAM (returns SRAM),
 * and the last byte of NVM (returns NVM). Verifies both interior
 * and boundary lookups return the correct region type.
 */
#define TEST_REGION_GET_TYPE     0

/**
 * Enable region get type not found test.
 * Queries an address past the NVM pool end (returns NOT_FOUND), a
 * NULL pointer (returns NOT_FOUND), and a NULL output pointer
 * (returns NOT_FOUND). Covers all rejection paths in get_type().
 */
#define TEST_REGION_NOT_FOUND    0

/** Auto-derived: true if any region test is enabled */
#define TEST_REGION (TEST_REGION_INIT || TEST_REGION_INIT_INVALID ||        \
                     TEST_REGION_CONTAINS || TEST_REGION_WRONG_TYPE ||       \
                     TEST_REGION_BOUNDARY || TEST_REGION_OVERFLOW ||         \
                     TEST_REGION_CLAIM || TEST_REGION_CLAIM_OVERLAP ||       \
                     TEST_REGION_CLAIM_UNKNOWN || TEST_REGION_CLAIM_FULL ||  \
                     TEST_REGION_GET_TYPE || TEST_REGION_NOT_FOUND)

/*---------------------------------------------------------------------------*/
/* POOL ALLOCATOR TESTS                                                      */
/*---------------------------------------------------------------------------*/

/**
 * Enable pool creation and initial stats test.
 * Creates a pool with 4 blocks of 8 bytes each, asserts the pool is
 * marked active with the correct ID and block_count, and verifies
 * initial stats show correct total_bytes with used, peak, and alloc
 * all at zero.
 */
#define TEST_POOL_CREATE         0

/**
 * Enable pool basic alloc and free test.
 * Allocates two 8-byte blocks, verifies distinct non-overlapping
 * pointers by writing separate patterns, checks used_bytes and
 * alloc_count track correctly, frees one block, and confirms
 * alloc_count decrements.
 */
#define TEST_POOL_ALLOC_FREE     0

/**
 * Enable pool exhaustion test.
 * Allocates all 4 blocks successfully, asserts a 5th allocation
 * returns NULL (pool empty), frees one block, and verifies the next
 * allocation succeeds — confirming freed blocks are immediately
 * reusable.
 */
#define TEST_POOL_EXHAUSTION     0

/**
 * Enable pool free out-of-range pointer test.
 * Attempts to free pointers from a completely different buffer, past
 * the pool buffer end, and before the buffer start. All three are
 * rejected with TIKU_MEM_ERR_INVALID, preventing freelist corruption
 * from stray pointers.
 */
#define TEST_POOL_FREE_RANGE     0

/**
 * Enable pool free misaligned pointer test.
 * Attempts to free pointers offset by 1 byte and block_size+1 bytes
 * within the pool buffer. Both are rejected with TIKU_MEM_ERR_INVALID,
 * catching mid-block pointers that would corrupt the freelist.
 */
#define TEST_POOL_FREE_ALIGN     0

/**
 * Enable pool alloc-free-realloc (LIFO) test.
 * Allocates p1 and p2, frees p2 then p1 (push order), and asserts the
 * next alloc returns p1 (last freed = first returned). The second alloc
 * returns p2, confirming LIFO freelist ordering.
 */
#define TEST_POOL_REALLOC        0

/**
 * Enable pool peak tracking test.
 * Allocates 3 blocks (peak=3), frees all of them, verifies peak is
 * preserved at 3, reallocates 1 block, and confirms peak remains 3.
 * Proves peak tracks the lifetime maximum and never decreases after
 * frees.
 */
#define TEST_POOL_PEAK           0

/**
 * Enable pool reset test.
 * Allocates all 4 blocks, calls reset, asserts alloc_count and
 * used_bytes return to zero while peak is preserved. Verifies
 * reallocation after reset returns blocks starting at the buffer
 * base, confirming the freelist was fully rebuilt.
 */
#define TEST_POOL_RESET          0

/**
 * Enable pool invalid inputs test.
 * Passes NULL pool, NULL buffer, and zero block_count to create;
 * NULL to alloc, free, reset, and stats. Asserts each returns the
 * appropriate error code or NULL without crashing.
 */
#define TEST_POOL_INVALID        0

/**
 * Enable two independent pools test.
 * Creates two pools backed by separate buffers, allocates from each
 * independently, resets one pool, and confirms the other pool's
 * used_count, alloc_count, and freelist remain completely unaffected.
 */
#define TEST_POOL_TWO_POOLS      0

/**
 * Enable pool block size alignment and minimum test.
 * Creates pools with block_size of 1 and 7, verifies the actual
 * block_size is clamped to at least sizeof(void *) (room for the
 * freelist pointer) and rounded up to TIKU_MEM_ARCH_ALIGNMENT.
 */
#define TEST_POOL_BLOCK_ALIGN    0

/**
 * Enable pool stats mapping test.
 * Allocates 2 blocks and verifies the stats mapping: total_bytes
 * equals block_size * block_count, used_bytes equals block_size * 2,
 * alloc_count equals 2, and peak_bytes equals block_size * 2.
 */
#define TEST_POOL_STATS          0

/**
 * Enable pool debug poisoning test.
 * Requires TIKU_POOL_DEBUG=1 at compile time. Allocates a block,
 * fills it with 0xAA, frees it, and verifies all bytes after the
 * freelist pointer (first sizeof(void *) bytes) are overwritten
 * with the 0xDE poison pattern, making use-after-free bugs visible.
 */
#define TEST_POOL_POISON         0

/**
 * Enable pool alloc within buffer test.
 * Allocates all blocks from a pool and verifies every returned
 * pointer falls within the backing buffer bounds: start >= buf and
 * end <= buf + total_size. Catches off-by-one errors in freelist
 * construction.
 */
#define TEST_POOL_WITHIN_BUF     0

/** Auto-derived: true if any pool test is enabled */
#define TEST_POOL (TEST_POOL_CREATE || TEST_POOL_ALLOC_FREE ||              \
                   TEST_POOL_EXHAUSTION || TEST_POOL_FREE_RANGE ||          \
                   TEST_POOL_FREE_ALIGN || TEST_POOL_REALLOC ||            \
                   TEST_POOL_PEAK || TEST_POOL_RESET ||                    \
                   TEST_POOL_INVALID || TEST_POOL_TWO_POOLS ||             \
                   TEST_POOL_BLOCK_ALIGN || TEST_POOL_STATS ||             \
                   TEST_POOL_POISON || TEST_POOL_WITHIN_BUF)

/** @} */ /* End of TIKU_TEST_CONFIG group */

#endif /* TIKU_TEST_CONFIG_H_ */
