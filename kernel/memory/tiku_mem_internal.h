/* Private allocator lifecycle hooks. SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_MEM_INTERNAL_H_
#define TIKU_MEM_INTERNAL_H_

#include "tiku_mem.h"

#define TIKU_BACKING_ARENA 1u
#define TIKU_BACKING_POOL  2u

/* Raw allocators are also linked into standalone libraries without tiers.
 * Such a build can use caller-owned backing, never a tracked descriptor.
 * Definitions in tiku_tier.c are strong; missing hooks fail closed for handles. */
#ifdef TIKU_MEM_RESERVATION_IMPL
#define TIKU_BACKING_LINK
#else
#define TIKU_BACKING_LINK __attribute__((weak))
#endif
int tiku_backing_busy(const void *descriptor) TIKU_BACKING_LINK;
int tiku_backing_valid(const void *descriptor, tiku_mem_backing_t handle,
                       uint8_t kind) TIKU_BACKING_LINK;
tiku_mem_err_t tiku_backing_release(const void *descriptor,
        tiku_mem_backing_t handle, uint8_t kind) TIKU_BACKING_LINK;
int tiku_backing_mutable(tiku_mem_backing_t handle) TIKU_BACKING_LINK;

#ifndef TIKU_MEM_RESERVATION_IMPL
static inline int tiku_backing_output_busy(const void *descriptor)
{
    return tiku_backing_busy != NULL && tiku_backing_busy(descriptor);
}

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
