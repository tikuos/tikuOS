/* Callback-owner declarations; state stays outside BASIC's movable arena.
 * SPDX-License-Identifier: Apache-2.0 */
#define BASIC_RECLAIM_ENABLE (TIKU_MEM_RECLAIM_ENABLE && \
                             TIKU_BASIC_PERSIST_RUN_ENABLE && BASIC_NVM_ON_REGION)
#if BASIC_RECLAIM_ENABLE
static tiku_mem_owner_t basic_reclaim_owner;
static struct tiku_process *basic_reclaim_process;
static unsigned basic_reclaim_entered;
static uint8_t basic_reclaim_external;
static int basic_reclaim_register(void);
static int basic_reclaim_available(void)
{
    return !basic_reclaim_owner.slot_plus_one ||
           tiku_mem_owner_available(basic_reclaim_owner);
}
#define BASIC_RECLAIM_EXTERNAL() (basic_reclaim_external = 1)
#else
static int basic_reclaim_available(void) { return 1; }
#define BASIC_RECLAIM_EXTERNAL() ((void)0)
#endif
