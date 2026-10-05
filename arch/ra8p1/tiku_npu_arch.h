/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_npu_arch.h - RA8P1 Ethos-U55 bring-up, model loading and runs.
 *
 * Powers the NPU domain, releases its module stop, loads a Vela command stream
 * from the image or the store, runs it, and checks it against the M85.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_NPU_ARCH_H_
#define TIKU_RA8P1_NPU_ARCH_H_

#include <stdint.h>

/** @brief Bring-up outcomes; anything but OK leaves the NPU gated. */
#define TIKU_RA8P1_NPU_OK           0
#define TIKU_RA8P1_NPU_ERR_MOCO    -1   /**< MOCO stopped; gating needs it    */
#define TIKU_RA8P1_NPU_ERR_POWER   -2   /**< power-up or soft reset failed    */
#define TIKU_RA8P1_NPU_ERR_ID      -3   /**< ID or CONFIG is not as expected  */

/**
 * @brief Power and ungate the NPU, then confirm it by its ID.
 *
 * Also soft-resets the block, sets its AXI limits and arms the completion
 * interrupt.
 *
 * @note Idempotent: a second call on a running NPU re-checks the ID and
 *       returns without touching the power sequence.
 * @return TIKU_RA8P1_NPU_OK, ERR_MOCO, ERR_POWER or ERR_ID
 */
int tiku_ra8p1_npu_init(void);

/**
 * @brief Return the NPU to module stop and power gating.
 *
 * Masks the completion interrupt, sets the module stop, then gates the
 * domain, the reverse of bring-up.  Does nothing while
 * tiku_ra8p1_npu_ready() is 0.
 */
void tiku_ra8p1_npu_stop(void);

/**
 * @brief Report whether a bring-up succeeded with no stop since.
 *
 * @return Non-zero when the block is usable
 */
int tiku_ra8p1_npu_ready(void);

/**
 * @brief The NPU's identity register.
 *
 * @return The raw ID, or 0 while tiku_ra8p1_npu_ready() is 0
 */
uint32_t tiku_ra8p1_npu_id(void);

/**
 * @brief MACs per cycle, decoded from CONFIG.
 *
 * @return 256 on this die, or 0 while tiku_ra8p1_npu_ready() is 0
 */
uint16_t tiku_ra8p1_npu_macs(void);

/**
 * @brief The NPU's shared-memory size in KB, as the block reports it.
 *
 * @return Size in KB, or 0 while tiku_ra8p1_npu_ready() is 0
 */
uint16_t tiku_ra8p1_npu_shram_kb(void);

/** @brief Completion interrupts taken from the NPU since boot. */
extern volatile uint32_t tiku_ra8p1_npu_irq_count;

/** @brief Load, run and self-test outcomes beyond the bring-up codes. */
#define TIKU_RA8P1_NPU_ERR_TIMEOUT -4   /**< no completion within 50 ms      */
#define TIKU_RA8P1_NPU_ERR_FAULT   -5   /**< faulted, or stopped before END  */
#define TIKU_RA8P1_NPU_ERR_MISMATCH -6  /**< ran, but disagreed with the M85 */
#define TIKU_RA8P1_NPU_ERR_IMAGE   -7   /**< no usable model or bad argument */
#define TIKU_RA8P1_NPU_ERR_ARENA   -8   /**< arena exceeds TIKU_NPU_ARENA_MAX */

/**
 * @brief Run the model in force over seeded input and check it on the M85.
 *
 * Without a store model this is the built-in max-pool stream.  Every output
 * byte must equal the M85's 2x2 maximum, or the input for an identity model;
 * input and output share one scale, so the comparison is exact.
 *
 * @param seed        Varies the input pattern between runs
 * @param status_out  Out: as for tiku_ra8p1_npu_run(), or NULL
 * @return TIKU_RA8P1_NPU_OK, or a negative TIKU_RA8P1_NPU_ERR_* code
 */
int tiku_ra8p1_npu_selftest(uint32_t seed, uint32_t *status_out);

/**
 * @brief Run the self-test with one command-stream byte inverted for the run.
 *
 * @param seed  Varies the input pattern
 * @return The self-test's codes; anything but OK means the corruption was
 *         detected
 */
int tiku_ra8p1_npu_selftest_tampered(uint32_t seed);

/**
 * @brief Run the self-test without the arena's cache maintenance.
 *
 * The command stream and the weights are still cleaned.
 *
 * @param seed  Varies the input pattern
 * @return The self-test's codes; OK means the arena held no dirty or stale
 *         lines
 */
int tiku_ra8p1_npu_selftest_nomaint(uint32_t seed);

/**
 * @brief Run the self-test with the completion interrupt masked at the NVIC.
 *
 * @param seed  Varies the input pattern
 * @return The self-test's codes; TIKU_RA8P1_NPU_ERR_TIMEOUT when the run
 *         waits on the interrupt, OK when it does not
 */
int tiku_ra8p1_npu_selftest_noirq(uint32_t seed);

/**
 * @brief Run the self-test with one weight byte inverted for the run.
 *
 * @param seed  Varies the input pattern
 * @return ERR_IMAGE when the model has no weights; otherwise the self-test's
 *         codes, where a failure means the NPU reads the weights in region 0
 */
int tiku_ra8p1_npu_selftest_badwts(uint32_t seed);

/** @brief Geometry a packed model carries; the built-in one fills it too. */
typedef struct {
    uint32_t arena;         /**< arena bytes the stream expects     */
    uint32_t ifm_off;       /**< input offset within the arena      */
    uint32_t ofm_off;       /**< output offset within the arena     */
    uint16_t ifm_dim;       /**< side of the square input           */
    uint16_t ofm_dim;       /**< side of the square output          */
    uint32_t cms_len;       /**< command stream bytes; 0 = no model */
    uint32_t wts_len;       /**< weight bytes; 0 for a weightless model */
    uint8_t  kind;          /**< TIKU_RA8P1_NPU_KIND_* reference         */
    uint8_t  channels;      /**< channels per input pixel                */
} tiku_ra8p1_npu_model_t;

/** @brief Values of tiku_ra8p1_npu_model_t.kind: what the M85 computes. */
#define TIKU_RA8P1_NPU_KIND_MAXPOOL   0u   /**< 2x2 maximum, stride 2 */
#define TIKU_RA8P1_NPU_KIND_IDENTITY  1u   /**< output equals input   */

/**
 * @brief Load a packed model from the /data store as the model in force.
 *
 * Brings the NPU up first, since the file's CONFIG word must match the
 * silicon's.  The command stream and weights are copied to aligned buffers.
 *
 * @param name  File in /data, packed by tools/npu/velapack.py
 * @return TIKU_RA8P1_NPU_OK, ERR_ARENA when the arena exceeds
 *         TIKU_NPU_ARENA_MAX, or ERR_IMAGE when the file is absent or unusable
 */
int tiku_ra8p1_npu_load(const char *name);

/** @brief Geometry currently in force, from the store or built in. */
const tiku_ra8p1_npu_model_t *tiku_ra8p1_npu_model(void);

/** @brief Non-zero when the model in force was loaded from the store. */
int tiku_ra8p1_npu_from_store(void);

/**
 * @brief The input buffer of the model in force, for the caller to fill.
 *
 * @return Pointer into the arena, or NULL when no model is in force
 */
void *tiku_ra8p1_npu_ifm(void);

/**
 * @brief The output buffer of the model in force, valid after a run.
 *
 * @return Pointer into the arena, or NULL when no model is in force
 */
const void *tiku_ra8p1_npu_ofm(void);

/**
 * @brief Run the model in force over the input buffer.
 *
 * Cleans the stream, weights and arena from the D-cache before the run and
 * invalidates the arena after it; the caller does no cache maintenance.
 *
 * @param status_out  Out: STATUS in bits 15:0 and QREAD (stream bytes
 *                    consumed) in bits 31:16, or NULL
 * @return TIKU_RA8P1_NPU_OK, a bring-up error, ERR_IMAGE, ERR_TIMEOUT or
 *         ERR_FAULT
 */
int tiku_ra8p1_npu_run(uint32_t *status_out);

/** @brief Successful tiku_ra8p1_npu_run() calls since boot. */
uint32_t tiku_ra8p1_npu_runs(void);

/**
 * @brief Time the model in force on the accelerator and on this core.
 *
 * Fills the input with a fixed pattern.  The accelerator's time includes its
 * cache maintenance; the core's is the reference computation alone.
 *
 * @param rounds   Iterations to average over; 0 returns ERR_IMAGE
 * @param npu_us   Out: microseconds per accelerator inference, or NULL
 * @param cpu_us   Out: microseconds per M85 inference, or NULL
 * @return TIKU_RA8P1_NPU_OK, or the failure that stopped it
 */
int tiku_ra8p1_npu_bench(uint32_t rounds, uint32_t *npu_us, uint32_t *cpu_us);

#endif /* TIKU_RA8P1_NPU_ARCH_H_ */
