/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lpagent_arch.h - P4 LP agent: jobs the LP core runs, HP awake or not.
 *
 * A job reads an LP source on a period; a firing counts, samples or, armed
 * for a deep sleep, wakes the HP core, whose shell then runs the job's
 * command.  The table lives in LP SRAM, with a durable copy in the mirror.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_LPAGENT_ARCH_H_
#define TIKU_ESP32P4_LPAGENT_ARCH_H_

#include <stdint.h>
#include "lpcore/tiku_lpcore_ipc.h"

/** @brief A job as the HP core writes and reads it. */
typedef struct {
    uint8_t  kind;          /* TIKU_LPAGENT_KIND_*, 0 for a free slot     */
    uint8_t  source;        /* LP IO for KIND_IO                          */
    uint8_t  op;            /* TIKU_LPAGENT_OP_*                          */
    uint8_t  action;        /* TIKU_LPAGENT_ACT_* bits                    */
    uint32_t period_ms;     /* between runs, >= 1                         */
    int32_t  threshold;
    char     cmd[TIKU_LPAGENT_CMD_MAX];  /* run after this job's wake     */
} tiku_lpagent_spec_t;

/** @brief What ended this boot's deep sleep, when a job did. */
typedef struct {
    uint32_t job;           /* slot */
    int32_t  value;         /* the reading that fired it */
    uint32_t lp_ticks;      /* LP timer low word when it fired */
    char     cmd[TIKU_LPAGENT_CMD_MAX];
} tiku_lpagent_wake_t;

/**
 * @brief Take this boot's LP agent state over.
 *
 * After tiku_p4_lpcore_boot(): an adopted payload keeps its table, and its
 * wake record is latched when a job ended the sleep.  A table lost with LP
 * SRAM is reloaded from the durable copy, which launches the payload.
 *
 * @note Call once per boot, after the flash mirror is restored.
 */
void tiku_lpagent_boot(void);

/**
 * @brief Add a job in the first free slot; it runs from the payload's next
 *        pass, and the durable copy is updated.
 * @return The slot, -1 when the table is full, -2 for a bad spec, -3 when
 *         the payload does not run and cannot be started
 */
int tiku_lpagent_add(const tiku_lpagent_spec_t *spec);

/** @brief Free slot @p slot.  @return 0, or -1 for a bad or free slot. */
int tiku_lpagent_del(unsigned slot);

/**
 * @brief Read slot @p slot.
 * @return 1 with @p out filled and @p hits its firings, 0 for a free slot
 */
int tiku_lpagent_get(unsigned slot, tiku_lpagent_spec_t *out, uint32_t *hits);

/** @brief Samples taken since the payload was loaded; the ring holds the
 *         latest TIKU_LPAGENT_SAMPLES of them. */
uint32_t tiku_lpagent_sample_count(void);

/**
 * @brief Read sample number @p n (0 is the first ever taken).
 * @return 1, or 0 when it has left the ring or not been taken
 */
int tiku_lpagent_sample(uint32_t n, uint32_t *lp_ticks, uint32_t *job,
                        int32_t *value);

/** @brief Let job wakes fire: called just before a deep sleep. */
void tiku_lpagent_arm(void);

/**
 * @brief This boot's job wake.
 * @return 1 with @p out filled when a job ended this boot's deep sleep
 */
int tiku_lpagent_woken(tiku_lpagent_wake_t *out);

/**
 * @brief Hand over the command line of this boot's job wake, once.
 * @return 1 with @p out filled, 0 when there is none or it was taken
 */
int tiku_lpagent_take_wake_cmd(char *out, unsigned cap);

#endif /* TIKU_ESP32P4_LPAGENT_ARCH_H_ */
