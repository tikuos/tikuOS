/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_history.c - "history" command implementation.
 *
 * A ring of command strings behind a magic word that detects and clears an
 * uninitialised store; durable on MSP430, kept across a warm reset elsewhere.
 * Every write goes through the MPU unlock window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_history.h"
#include <shell/tiku_shell.h>   /* SHELL_PRINTF, TIKU_SHELL_LINE_SIZE */
#include <kernel/memory/tiku_mem.h>    /* tiku_mpu_unlock/lock_nvm, nvm_write */
#include <string.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** Magic value that detects an uninitialised ring on first boot */
#define TIKU_SHELL_HISTORY_MAGIC  0xC0DEU

/*---------------------------------------------------------------------------*/
/* RING BUFFER                                                               */
/*---------------------------------------------------------------------------*/

/*
 * History ring placement by grade:
 *
 *   MSP430     TIKU_DURABLE -- FRAM in place, survives power cycles.
 *   elsewhere  TIKU_RETAINED -- survives a warm reset; a power cycle loses
 *              it, except where the port mirrors retained data to NVM
 *              (STM32N6, ESP32-C61).  At 256-byte lines and 16 entries the
 *              ring is 4 KB, the whole of RP2350's TIKU_DURABLE budget.
 *
 * At either grade a wrong magic word, or a head or count outside the ring,
 * makes hist_ensure_init() clear the ring.
 */
#ifdef PLATFORM_MSP430
#define HIST_PERSISTENT TIKU_DURABLE
#else
#define HIST_PERSISTENT TIKU_RETAINED
#endif

/** Ring entry: one stored command line */
typedef struct {
    char line[TIKU_SHELL_LINE_SIZE];
} tiku_shell_hist_entry_t;

/**
 * Ring control block, placed by HIST_PERSISTENT.  It has no initializer: off
 * MSP430 the section is NOLOAD, and hist_ensure_init() primes the ring when
 * the magic word is wrong or the head or count is out of range.
 */
static HIST_PERSISTENT struct {
    uint16_t                magic;
    uint8_t                 head;   /* next write slot */
    uint8_t                 count;  /* entries stored  */
    tiku_shell_hist_entry_t ring[TIKU_SHELL_HISTORY_DEPTH];
} hist;

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write a value to a ring variable via the kernel NVM API.
 *
 * @note The caller holds the MPU unlocked (one window for a batch of writes).
 */
#define HIST_NVM_WRITE(fram_var, sram_val) \
    do { \
        __typeof__(fram_var) _tmp = (sram_val); \
        tiku_mem_arch_nvm_write((uint8_t *)&(fram_var), \
                                (const uint8_t *)&_tmp, \
                                sizeof(fram_var)); \
    } while (0)

/**
 * @brief Ensure the ring is initialised (first boot or a corrupted store).
 *
 * Reads are direct (no MPU unlock needed); writes go through the NVM API.
 */
static void
hist_ensure_init(void)
{
    uint16_t saved;

    if (hist.magic == TIKU_SHELL_HISTORY_MAGIC &&
        hist.head < TIKU_SHELL_HISTORY_DEPTH &&
        hist.count <= TIKU_SHELL_HISTORY_DEPTH) {
        return;
    }

    saved = tiku_mpu_unlock_nvm();

    HIST_NVM_WRITE(hist.head, 0);
    HIST_NVM_WRITE(hist.count, 0);

    /* Zero-fill the entire ring */
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

    /* Build the entry in SRAM before the MPU window opens */
    memset(buf, 0, sizeof(buf));
    strncpy(buf, line, TIKU_SHELL_LINE_SIZE - 1);

    new_head  = (hist.head + 1) % TIKU_SHELL_HISTORY_DEPTH;
    new_count = (hist.count < TIKU_SHELL_HISTORY_DEPTH)
                    ? hist.count + 1
                    : hist.count;

    /* Single MPU-unlocked window for all the ring writes */
    saved = tiku_mpu_unlock_nvm();

    tiku_mem_arch_nvm_write(
        (uint8_t *)hist.ring[hist.head].line,
        (const uint8_t *)buf,
        TIKU_SHELL_LINE_SIZE);

    HIST_NVM_WRITE(hist.head, new_head);
    HIST_NVM_WRITE(hist.count, new_count);

    tiku_mpu_lock_nvm(saved);
}

const char *
tiku_shell_history_get(uint8_t age)
{
    uint8_t idx;

    hist_ensure_init();
    if (age >= hist.count) {
        return NULL;
    }
    idx = (hist.head + TIKU_SHELL_HISTORY_DEPTH - 1 - age)
          % TIKU_SHELL_HISTORY_DEPTH;
    return hist.ring[idx].line;
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
        unsigned val = 0;
        size_t j;
        for (j = 0; argv[1][j] != '\0'; j++) {
            if (argv[1][j] < '0' || argv[1][j] > '9') {
                SHELL_PRINTF("Usage: history [N]\n");
                return;
            }
            if (val < hist.count) {
                val = val * 10u + (unsigned)(argv[1][j] - '0');
                if (val > hist.count) val = hist.count;
            }
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
