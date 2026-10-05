/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_internal.h - private lifecycle hooks between allocators and tiers.
 *
 * The arena and pool code asks the tier allocator, through these hooks,
 * whether a control block holds a tracked reservation and may change.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEM_INTERNAL_H_
#define TIKU_MEM_INTERNAL_H_

#include "tiku_mem.h"

/** @brief Reservation kind of an arena control block. */
#define TIKU_BACKING_ARENA 1u
/** @brief Reservation kind of a pool control block. */
#define TIKU_BACKING_POOL  2u

/**
 * @brief Link attribute of the hooks: strong in tiku_tier.c, weak elsewhere.
 *
 * Raw allocators are also linked into standalone libraries without tiers,
 * which use caller-owned backing, never a tracked descriptor.  There the hooks
 * are NULL, and tiku_backing_check() fails closed for any non-zero handle.
 */
#ifdef TIKU_MEM_RESERVATION_IMPL
#define TIKU_BACKING_LINK
#else
#define TIKU_BACKING_LINK __attribute__((weak))
#endif

/** @brief Non-zero while any reservation record names @p descriptor. */
int tiku_backing_busy(const void *descriptor) TIKU_BACKING_LINK;

/**
 * @brief Non-zero when @p handle is the live reservation behind
 *        @p descriptor and the descriptor's fields still match it.
 *
 * @param kind  TIKU_BACKING_ARENA or TIKU_BACKING_POOL
 */
int tiku_backing_valid(const void *descriptor, tiku_mem_backing_t handle,
                       uint8_t kind) TIKU_BACKING_LINK;

/**
 * @brief Release the reservation behind @p descriptor.
 *
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID when the handle does not match a
 *         live record, or TIKU_MEM_ERR_BUSY while a reclaim fence holds the
 *         span or a fixed reservation above it is still live
 */
tiku_mem_err_t tiku_backing_release(const void *descriptor,
        tiku_mem_backing_t handle, uint8_t kind) TIKU_BACKING_LINK;

/**
 * @brief Non-zero when the allocator may change the backing of @p handle.
 *
 * With reclaim enabled the record must be live and current, and while a
 * reclaim job holds its owner only the owner itself may change it.  Without
 * reclaim it is always non-zero.
 */
int tiku_backing_mutable(tiku_mem_backing_t handle) TIKU_BACKING_LINK;

#ifndef TIKU_MEM_RESERVATION_IMPL
/** @brief Non-zero while @p descriptor holds a tracked reservation. */
static inline int tiku_backing_output_busy(const void *descriptor)
{
    return tiku_backing_busy != NULL && tiku_backing_busy(descriptor);
}

/** @brief Non-zero when an allocator may change memory behind @p handle. */
static inline int tiku_backing_can_mutate(tiku_mem_backing_t handle)
{
#if defined(TIKU_MEM_RECLAIM_ENABLE) && TIKU_MEM_RECLAIM_ENABLE
    return !handle.slot_plus_one || tiku_backing_mutable == NULL ||
           tiku_backing_mutable(handle);
#else
    (void)handle;
    return 1;
#endif
}

/**
 * @brief Whether a descriptor's backing is consistent with its handle.
 *
 * A zero handle (caller-owned backing) must not be named by any record; a
 * non-zero one must pass tiku_backing_valid().
 */
static inline int tiku_backing_check(const void *descriptor,
        tiku_mem_backing_t handle, uint8_t kind)
{
    if (handle.slot_plus_one == 0u) {
        return handle.generation == 0u && !tiku_backing_output_busy(descriptor);
    }
    return tiku_backing_valid != NULL &&
           tiku_backing_valid(descriptor, handle, kind);
}
#endif

#endif
