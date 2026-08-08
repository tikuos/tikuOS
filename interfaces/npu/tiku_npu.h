/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_npu.h - the portable neural accelerator contract.
 *
 * A named model out of the file store, a buffer in, a buffer out, and one
 * blocking run.  Cache coherency belongs to the backend, not the caller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NPU_H_
#define TIKU_NPU_H_

#include <stdint.h>

/** @brief Zero where no backend is compiled in, so callers can compile out. */
#ifndef TIKU_HAS_NPU
#define TIKU_HAS_NPU            0
#endif

#define TIKU_NPU_OK              0
#define TIKU_NPU_ERR_STATE      -1  /**< gated, or nothing loaded to run   */
#define TIKU_NPU_ERR_MODEL      -2  /**< no such model, or not for this part */
#define TIKU_NPU_ERR_TIMEOUT    -3  /**< submitted, never reached the end   */
#define TIKU_NPU_ERR_FAULT      -4  /**< the accelerator rejected the work  */

/** @brief Models are named files in the store rather than linked-in arrays. */
#define TIKU_NPU_F_STORE_MODEL  (1u << 0)
/** @brief Integer quantised networks only; no float path exists. */
#define TIKU_NPU_F_INT_ONLY     (1u << 1)

typedef enum {
    TIKU_NPU_ABSENT = 0,    /**< no accelerator on this part      */
    TIKU_NPU_GATED,         /**< present, powered down            */
    TIKU_NPU_IDLE,          /**< released, nothing loaded         */
    TIKU_NPU_READY,         /**< a model is loaded and runnable   */
    TIKU_NPU_FAULTED        /**< a run failed; reload to recover  */
} tiku_npu_state_t;

/** @brief What the accelerator is, and what the loaded model asks of it. */
typedef struct {
    uint16_t macs;          /**< multiply-accumulates per cycle   */
    uint16_t shram_kb;      /**< the accelerator's own memory     */
    uint32_t arena;         /**< working buffer the model needs   */
    uint32_t in_bytes;      /**< 0 until a model is loaded        */
    uint32_t out_bytes;
} tiku_npu_info_t;

/**
 * @brief Which parts of this contract the backend actually implements.
 *
 * @return A mask of TIKU_NPU_F_*
 */
uint32_t tiku_npu_flags(void);

/**
 * @brief Where the accelerator is in its lifecycle.
 *
 * @return One of tiku_npu_state_t
 */
tiku_npu_state_t tiku_npu_state(void);

/**
 * @brief Power and release the accelerator.
 *
 * @return TIKU_NPU_OK, or TIKU_NPU_ERR_STATE
 */
int tiku_npu_start(void);

/**
 * @brief Return the accelerator to its powered-down state.
 */
void tiku_npu_stop(void);

/**
 * @brief Take a model from the store and make it the one that runs.
 *
 * @param name  File in the store, packed for this backend
 * @return TIKU_NPU_OK, or TIKU_NPU_ERR_MODEL
 */
int tiku_npu_load(const char *name);

/**
 * @brief Describe the accelerator and the loaded model.
 *
 * @param out  Filled on success
 * @return TIKU_NPU_OK, or TIKU_NPU_ERR_STATE
 */
int tiku_npu_info(tiku_npu_info_t *out);

/**
 * @brief The input buffer to fill before a run.
 *
 * @param len  Out: its size in bytes, or NULL
 * @return Pointer to write into, or NULL with no model loaded
 */
void *tiku_npu_input(uint32_t *len);

/**
 * @brief The output buffer, valid once a run has returned OK.
 *
 * @param len  Out: its size in bytes, or NULL
 * @return Pointer to read from, or NULL with no model loaded
 */
const void *tiku_npu_output(uint32_t *len);

/**
 * @brief Run the loaded model over the input buffer and block until it ends.
 *
 * @return TIKU_NPU_OK, ERR_STATE, ERR_TIMEOUT or ERR_FAULT
 */
int tiku_npu_run(void);

/**
 * @brief Runs completed since boot, for observability.
 *
 * @return The count
 */
uint32_t tiku_npu_runs(void);

#endif /* TIKU_NPU_H_ */
