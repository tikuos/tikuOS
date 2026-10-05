/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_reclaim_state.inl - state of BASIC's memory-reclaim owner.
 *
 * Declared early so every interpreter piece can test the gate and flag an
 * external resource.  The state stays outside BASIC's movable arena; without
 * reclaim the gate is always open.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** 1 when BASIC registers as a reclaim owner: needs the reclaim coordinator,
 *  PERSIST / RUN RESUME and the /data store. */
#define BASIC_RECLAIM_ENABLE (TIKU_MEM_RECLAIM_ENABLE && \
                             TIKU_BASIC_PERSIST_RUN_ENABLE && BASIC_NVM_ON_REGION)
#if BASIC_RECLAIM_ENABLE
static tiku_mem_owner_t basic_reclaim_owner;
static struct tiku_process *basic_reclaim_process;
/** Depth of interpreter entries from the shell hooks; nonzero is busy. */
static unsigned basic_reclaim_entered;
/** Set once BASIC touches state a snapshot cannot hold; stays set this boot. */
static uint8_t basic_reclaim_external;
static int basic_reclaim_register(void);
/** @brief 0 while a reclaim job holds BASIC gated, else 1. */
static int basic_reclaim_available(void)
{
    return !basic_reclaim_owner.slot_plus_one ||
           tiku_mem_owner_available(basic_reclaim_owner);
}
/** Mark a native word, BLE or PEEK/POKE use; BASIC then stays unreclaimable. */
#define BASIC_RECLAIM_EXTERNAL() (basic_reclaim_external = 1)
#else
/** @brief Reclaim compiled out: BASIC is always available (1). */
static int basic_reclaim_available(void) { return 1; }
#define BASIC_RECLAIM_EXTERNAL() ((void)0)
#endif
