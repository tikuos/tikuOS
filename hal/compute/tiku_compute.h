/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compute.h - shared arithmetic status and provider identification.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_COMPUTE_H_
#define TIKU_COMPUTE_H_

#include <stdint.h>

/** @brief Compute call results; errors leave output unchanged. */
typedef enum {
    TIKU_COMPUTE_OK = 0,
    TIKU_COMPUTE_ERR_PARAM = -1,
    TIKU_COMPUTE_ERR_RANGE = -2,
    TIKU_COMPUTE_ERR_OVERLAP = -3
} tiku_compute_status_t;

/** @brief Operations identified by the provider query. */
typedef enum {
    TIKU_COMPUTE_DOT_S8 = 0,
    TIKU_COMPUTE_DOT_S16,
    TIKU_COMPUTE_ADD_S32,
    TIKU_COMPUTE_MATVEC_S8
} tiku_compute_op_t;

/** @brief Compiled implementation, or an unknown operation. */
typedef enum {
    TIKU_COMPUTE_PROVIDER_UNKNOWN = -1,
    TIKU_COMPUTE_PROVIDER_REFERENCE = 0,
    TIKU_COMPUTE_PROVIDER_P4 = 1,
    TIKU_COMPUTE_PROVIDER_MVE = 2
} tiku_compute_provider_t;

/** @brief Return the compiled provider for an operation. */
tiku_compute_provider_t tiku_compute_provider(tiku_compute_op_t op);

#if TIKU_COMPUTE_TRACE
/** @brief Test-only per-operation element counts; not atomic. */
typedef struct {
    uint64_t vector_elements;
    uint64_t scalar_elements;
} tiku_compute_trace_t;
/** @brief Clear test counters when no compute caller is running. */
void tiku_compute_trace_reset(void);
/** @brief Enable or pause counters when no compute caller is running. */
void tiku_compute_trace_enable(int enabled);
/** @brief Read test counters; unknown operations return zero counts. */
tiku_compute_trace_t tiku_compute_trace(tiku_compute_op_t op);
#endif
#endif
