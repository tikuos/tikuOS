/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_history.c - "history" command implementation
 *
 * Maintains a circular ring buffer of command strings in FRAM so that
 * history survives across reboots.  The ring uses a magic number to
 * detect first-boot (uninitialised FRAM) and auto-clears in that case.
 *
 * All FRAM writes go through tiku_mpu_unlock_nvm / tiku_mem_arch_nvm_write /
 * tiku_mpu_lock_nvm to respect the kernel's MPU write-protection scheme.
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

#include "tiku_shell_cmd_history.h"
#include <kernel/shell/tiku_shell.h>             /* SHELL_PRINTF, TIKU_SHELL_LINE_SIZE */
#include <kernel/memory/tiku_mem.h>              /* tiku_mpu_unlock/lock_nvm, nvm_write */
#include <string.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** Magic value to detect uninitialised FRAM on first boot */
#define TIKU_SHELL_HISTORY_MAGIC  0xC0DEU

/*---------------------------------------------------------------------------*/
/* FRAM-BACKED RING BUFFER                                                   */
/*---------------------------------------------------------------------------*/

/**
 * History ring stored in the .persistent section so FRAM retains it
 * across reboots.  On non-MSP430 builds the buffer is plain static
 * (useful for host-side testing — history is lost on power cycle).
 */
#ifdef PLATFORM_MSP430
#define HIST_PERSISTENT \
    __attribute__((section(".persistent")))
#else
#define HIST_PERSISTENT
#endif

/** Ring entry: one stored command line */
typedef struct {
    char line[TIKU_SHELL_LINE_SIZE];
} tiku_shell_hist_entry_t;

/** Ring control block (all fields in FRAM) */
static HIST_PERSISTENT struct {
    uint16_t                magic;
    uint8_t                 head;   /* next write slot */
    uint8_t                 count;  /* entries stored  */
    tiku_shell_hist_entry_t ring[TIKU_SHELL_HISTORY_DEPTH];
} hist = {0};

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write a value to an FRAM-backed variable via the kernel NVM API.
 *
 * Caller must hold the MPU unlocked (single unlock window for batching).
 */
#define HIST_NVM_WRITE(fram_var, sram_val) \
    do { \
        __typeof__(fram_var) _tmp = (sram_val); \
        tiku_mem_arch_nvm_write((uint8_t *)&(fram_var), \
                                (const uint8_t *)&_tmp, \
                                sizeof(fram_var)); \
    } while (0)

/**
 * @brief Ensure the ring is initialised (first boot or corrupted FRAM).
 *
 * Reads are direct (no MPU unlock needed); writes go through the NVM API.
 */
static void
hist_ensure_init(void)
{
    uint16_t saved;

    if (hist.magic == TIKU_SHELL_HISTORY_MAGIC) {
        return;
    }

    saved = tiku_mpu_unlock_nvm();

    HIST_NVM_WRITE(hist.head, 0);
    HIST_NVM_WRITE(hist.count, 0);

    /* Zero-fill the entire ring buffer in FRAM */
    {
        uint8_t zero[TIKU_SHELL_LINE_SIZE];
        uint8_t i;
        memset(zero, 0, sizeof(zero));
        for (i = 0; i < TIKU_SHELL_HISTORY_DEPTH; i++) {
            tiku_mem_arch_nvm_write(
                (uint8_t *)hist.ring[i].line, zero,
                TIKU_SHELL_LINE_SIZE);
        }
    }

    /* Write magic last — acts as commit marker */
    HIST_NVM_WRITE(hist.magic, TIKU_SHELL_HISTORY_MAGIC);

    tiku_mpu_lock_nvm(saved);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

void
tiku_shell_history_record(const char *line)
{
    uint16_t saved;
    uint8_t new_head;
    uint8_t new_count;
    char buf[TIKU_SHELL_LINE_SIZE];

    hist_ensure_init();

    if (line == NULL || line[0] == '\0') {
        return;
    }

    /* Suppress duplicate consecutive entries (read only — no MPU needed) */
    if (hist.count > 0) {
        uint8_t prev = (hist.head == 0)
                           ? TIKU_SHELL_HISTORY_DEPTH - 1
                           : hist.head - 1;
        if (strncmp(hist.ring[prev].line, line,
                    TIKU_SHELL_LINE_SIZE) == 0) {
            return;
        }
    }

    /* Prepare the entry in SRAM before taking the MPU lock */
    memset(buf, 0, sizeof(buf));
    strncpy(buf, line, TIKU_SHELL_LINE_SIZE - 1);

    new_head  = (hist.head + 1) % TIKU_SHELL_HISTORY_DEPTH;
    new_count = (hist.count < TIKU_SHELL_HISTORY_DEPTH)
                    ? hist.count + 1
                    : hist.count;

    /* Single MPU-unlocked window for all FRAM writes */
    saved = tiku_mpu_unlock_nvm();

    tiku_mem_arch_nvm_write(
        (uint8_t *)hist.ring[hist.head].line,
        (const uint8_t *)buf,
        TIKU_SHELL_LINE_SIZE);

    HIST_NVM_WRITE(hist.head, new_head);
    HIST_NVM_WRITE(hist.count, new_count);

    tiku_mpu_lock_nvm(saved);
}

void
tiku_shell_cmd_history(uint8_t argc, const char *argv[])
{
    uint8_t n;
    uint8_t start;
    uint8_t i;
    uint8_t idx;

    hist_ensure_init();

    /* Default: show all stored entries (reads — no MPU unlock needed) */
    n = hist.count;

    /* Optional argument: limit to last N */
    if (argc >= 2) {
        uint8_t val = 0;
        uint8_t j;
        for (j = 0; argv[1][j] != '\0'; j++) {
            if (argv[1][j] < '0' || argv[1][j] > '9') {
                SHELL_PRINTF("Usage: history [N]\n");
                return;
            }
            val = val * 10 + (argv[1][j] - '0');
        }
        if (val < n) {
            n = val;
        }
    }

    if (n == 0) {
        SHELL_PRINTF("(no history)\n");
        return;
    }

    /* Walk the ring from oldest to newest of the requested window */
    start = (hist.head + TIKU_SHELL_HISTORY_DEPTH - hist.count)
            % TIKU_SHELL_HISTORY_DEPTH;
    /* Skip to show only the last 'n' entries */
    start = (start + (hist.count - n)) % TIKU_SHELL_HISTORY_DEPTH;

    for (i = 0; i < n; i++) {
        idx = (start + i) % TIKU_SHELL_HISTORY_DEPTH;
        SHELL_PRINTF("  %u  %s\n", (unsigned)(i + 1),
                     hist.ring[idx].line);
    }
}
