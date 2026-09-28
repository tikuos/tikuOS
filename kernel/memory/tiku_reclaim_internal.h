/* Allocator/coordinator interface, not application API. SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_RECLAIM_INTERNAL_H_
#define TIKU_RECLAIM_INTERNAL_H_
#include "tiku_reclaim.h"

#if TIKU_MEM_RECLAIM_ENABLE
typedef struct {
    const void *descriptor;
    tiku_mem_backing_t handle;
    tiku_mem_owner_t owner;
    uint16_t owner_slot;
    tiku_mem_tier_t tier;
    uint8_t span_index, kind, allocation_class, state;
    tiku_mem_arch_size_t offset, length, alignment, block_size, block_count;
} tiku_reclaim_record_t;

/* Only the coordinator can hold capacity or end nonempty owned lifetimes. */
int tiku_reclaim_record(unsigned slot, tiku_reclaim_record_t *out);
tiku_mem_err_t tiku_reclaim_normalize(const tiku_mem_reclaim_request_t *,
                                      tiku_reclaim_record_t *out);
tiku_mem_err_t tiku_reclaim_hold_direct(tiku_reclaim_record_t *,
                                        const tiku_mem_reclaim_request_t *);
tiku_mem_err_t tiku_reclaim_bind(tiku_mem_backing_t, void *descriptor, uint8_t id, int restoring);
void tiku_reclaim_drop(tiku_mem_backing_t);
int tiku_reclaim_credit(tiku_mem_backing_t *);
int tiku_reclaim_fence(tiku_mem_tier_t, uint8_t, int enabled);
int tiku_reclaim_fenced(tiku_mem_tier_t, uint8_t);
int tiku_reclaim_any_fence(void);
int tiku_reclaim_reset_busy(void);
int tiku_mem_reclaim_pending(void);
struct tiku_process;
int tiku_mem_reclaim_process_exit(struct tiku_process *);
int tiku_mem_reclaim_process_exit_valid(const struct tiku_process *);
int tiku_mem_reclaim_process_start_allowed(const struct tiku_process *);
void tiku_mem_reclaim_process_started(struct tiku_process *);
int tiku_mem_reclaim_process_dispatch(const struct tiku_process *, unsigned event);
void tiku_mem_reclaim_process_yielded(struct tiku_process *);
/* Charge a real failed restore to the ordinary crash-storm allowance. */
int tiku_process_restart_charge(struct tiku_process *);
int tiku_process_reclaim_quiet(const struct tiku_process *);
int tiku_mem_reclaim_read(const char *entry, char *buf, size_t size);
tiku_mem_err_t tiku_mem_reclaim_write(const char *entry, const char *buf, size_t size);
int tiku_reclaim_publish(const tiku_reclaim_record_t *old,
                         tiku_reclaim_record_t *replacement, unsigned count,
                         tiku_reclaim_record_t *request);
int tiku_reclaim_restore_original(const tiku_reclaim_record_t *,
    tiku_reclaim_record_t *, unsigned count, tiku_mem_owner_t);
int tiku_reclaim_rearm(tiku_reclaim_record_t *, unsigned count, tiku_mem_owner_t);
int tiku_reclaim_process_owner_valid(tiku_mem_owner_t, const void *, size_t);
tiku_mem_err_t tiku_reclaim_context_tag(tiku_proc_mem_t *, tiku_mem_owner_t, uint16_t);

/* Callouts from tier operations. */
int tiku_reclaim_owner_options(const tiku_mem_request_t *, uint8_t *cls);
tiku_mem_err_t tiku_reclaim_owner_create(const tiku_mem_request_t *,
    tiku_mem_tier_t, int span, uint8_t kind, tiku_mem_arch_size_t length,
    tiku_mem_arch_size_t alignment, tiku_mem_arch_size_t stride,
    tiku_mem_arch_size_t count, void *descriptor, uint8_t id, int *claimed);
int tiku_reclaim_owner_access(tiku_mem_owner_t);
void tiku_reclaim_detached(tiku_mem_tier_t, uint8_t);

#endif
#endif
