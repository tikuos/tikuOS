/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_npu_arch.h - bring the RA8P1's Ethos-U55 out of reset.
 *
 * Release the NPU power domain and module stop in the order the manual gives,
 * then report what the block says about itself.  Command streams are not this
 * layer's business.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_NPU_ARCH_H_
#define TIKU_RA8P1_NPU_ARCH_H_

#include <stdint.h>

/** @brief Bring-up outcomes; anything but OK leaves the NPU gated. */
#define TIKU_RA8P1_NPU_OK           0
#define TIKU_RA8P1_NPU_ERR_MOCO    -1   /**< MOCO stopped; gating needs it   */
#define TIKU_RA8P1_NPU_ERR_POWER   -2   /**< domain never left power gating  */
#define TIKU_RA8P1_NPU_ERR_ID      -3   /**< released, but not the expected  */

/**
 * @brief Power and ungate the NPU, then confirm it by its ID.
 *
 * @note Idempotent: a second call on a running NPU re-checks the ID and
 *       returns without touching the power sequence.
 * @return TIKU_RA8P1_NPU_OK, or one of the errors above
 */
int tiku_ra8p1_npu_init(void);

/**
 * @brief Return the NPU to module stop and power gating.
 *
 * @note The manual's order is the reverse of bring-up: stop the module first,
 *       gate the domain second.
 */
void tiku_ra8p1_npu_stop(void);

/**
 * @brief Is the NPU powered, ungated and answering with the expected ID?
 *
 * @return Non-zero when the block is usable
 */
int tiku_ra8p1_npu_ready(void);

/**
 * @brief The NPU's identity register.
 *
 * @return The raw ID, or 0 while the block is gated
 */
uint32_t tiku_ra8p1_npu_id(void);

/**
 * @brief MACs per cycle, which fixes the command-stream compiler's target.
 *
 * @return 256 on this die, or 0 while the block is gated
 */
uint16_t tiku_ra8p1_npu_macs(void);

/**
 * @brief The NPU's shared-memory size in KB, as the block reports it.
 *
 * @return Size in KB, or 0 while the block is gated
 */
uint16_t tiku_ra8p1_npu_shram_kb(void);

/** @brief Interrupts taken from the NPU; 0 with work submitted means the
 *         event never reached the NVIC. */
extern volatile uint32_t tiku_ra8p1_npu_irq_count;

/** @brief Self-test outcomes beyond the bring-up codes above. */
#define TIKU_RA8P1_NPU_ERR_TIMEOUT -4   /**< never reached the stream's end */
#define TIKU_RA8P1_NPU_ERR_FAULT   -5   /**< parse error or bus fault       */
#define TIKU_RA8P1_NPU_ERR_MISMATCH -6  /**< ran, but disagreed with the M85 */
#define TIKU_RA8P1_NPU_ERR_IMAGE   -7   /**< no usable model in the store   */
#define TIKU_RA8P1_NPU_ERR_ARENA   -8   /**< model arena exceeds the build */

/**
 * @brief Run the built-in max-pool stream and check it against the M85.
 *
 * Submits the Vela command stream over seeded input and compares every output
 * byte with a windowed maximum computed here.  The model carries one scale and
 * a zero zero-point, so the comparison is exact.
 *
 * @param seed        Varies the input pattern between runs
 * @param status_out  Out: NPU status word after completion, or NULL
 * @return TIKU_RA8P1_NPU_OK, or one of the errors above
 */
int tiku_ra8p1_npu_selftest(uint32_t seed, uint32_t *status_out);

/**
 * @brief Corrupt one command-stream byte and run, to prove the check can fail.
 *
 * @return The same codes as the self-test; anything but OK means the
 *         tampering was detected
 */
int tiku_ra8p1_npu_selftest_tampered(uint32_t seed);

/**
 * @brief Run the same stream with the cache maintenance omitted.
 *
 * @return OK only if the result survived anyway, which would mean the buffers
 *         were never cached and the maintained run proved nothing
 */
int tiku_ra8p1_npu_selftest_nomaint(uint32_t seed);

/**
 * @brief Run once with the completion interrupt masked at the NVIC.
 *
 * @return Anything but OK means the run really does end on the interrupt
 *         rather than on a status poll that happens to notice
 */
int tiku_ra8p1_npu_selftest_noirq(uint32_t seed);

/**
 * @brief Run once with one weight byte corrupted.
 *
 * @return ERR_IMAGE when the model is weightless; otherwise anything but OK
 *         means the read-only region really is being read
 */
int tiku_ra8p1_npu_selftest_badwts(uint32_t seed);

/** @brief Geometry a packed model carries; the built-in one fills it too. */
typedef struct {
    uint32_t arena;         /**< working buffer the stream expects  */
    uint32_t ifm_off;       /**< input offset within the arena      */
    uint32_t ofm_off;       /**< output offset within the arena     */
    uint16_t ifm_dim;
    uint16_t ofm_dim;
    uint32_t cms_len;
    uint32_t wts_len;       /**< read-only blob; 0 for a weightless model */
    uint8_t  kind;          /**< what the expected output is              */
    uint8_t  channels;
} tiku_ra8p1_npu_model_t;

/** @brief Reference the firmware holds the accelerator to. */
#define TIKU_RA8P1_NPU_KIND_MAXPOOL   0u
#define TIKU_RA8P1_NPU_KIND_IDENTITY  1u

/**
 * @brief Take the model out of the file store rather than the image.
 *
 * @note The command stream is copied into an aligned buffer because the queue
 *       base needs alignment the store does not promise; region bases carry no
 *       such rule, so weights could be pointed at where they lie.
 * @param name  File in /data, packed by tools/npu/velapack.py
 * @return TIKU_RA8P1_NPU_OK, or ERR_IMAGE when the file is absent or unusable
 */
int tiku_ra8p1_npu_load(const char *name);

/** @brief Geometry currently in force, from the store or built in. */
const tiku_ra8p1_npu_model_t *tiku_ra8p1_npu_model(void);

/** @brief Is the loaded model the one the store supplied? */
int tiku_ra8p1_npu_from_store(void);

/**
 * @brief The loaded model's input buffer, for the caller to fill.
 *
 * @return Pointer into the arena, or NULL with no model loaded
 */
void *tiku_ra8p1_npu_ifm(void);

/**
 * @brief The loaded model's output buffer, valid after a run.
 *
 * @return Pointer into the arena, or NULL with no model loaded
 */
const void *tiku_ra8p1_npu_ofm(void);

/**
 * @brief Run the loaded model over whatever the input buffer holds.
 *
 * @note Owns the cache maintenance both ways: the NPU is a second AXI master,
 *       and neither side sees the other's writes without it.
 * @param status_out  Out: status word and bytes consumed, or NULL
 * @return TIKU_RA8P1_NPU_OK, ERR_IMAGE, ERR_TIMEOUT or ERR_FAULT
 */
int tiku_ra8p1_npu_run(uint32_t *status_out);

/** @brief Completed runs since boot. */
uint32_t tiku_ra8p1_npu_runs(void);

/**
 * @brief Time the same work on the accelerator and on this core.
 *
 * @note The accelerator's figure INCLUDES its cache maintenance, because that
 *       is part of what offloading costs; the core's is the kernel alone.
 * @param rounds   Iterations to average over
 * @param npu_us   Out: microseconds per accelerator inference
 * @param cpu_us   Out: microseconds per M85 inference
 * @return TIKU_RA8P1_NPU_OK, or the failure that stopped it
 */
int tiku_ra8p1_npu_bench(uint32_t rounds, uint32_t *npu_us, uint32_t *cpu_us);

#endif /* TIKU_RA8P1_NPU_ARCH_H_ */
