/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_sync.h - the points where values kept in SRAM reach NVM.
 *
 * An owner of such values registers a hook that stores them.  The kernel runs
 * each hook at the owner's own pace from the scheduler loop, and every hook
 * before a sleep that loses SRAM and before a reset it starts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NVM_SYNC_H_
#define TIKU_NVM_SYNC_H_

#include <stddef.h>
#include <stdint.h>
#include "kernel/memory/tiku_nvm_media.h"

/** @brief 1 where the kernel runs sync points; 0 on MSP430, whose retained
 *         memory is FRAM, so a value kept there needs none. */
#ifndef TIKU_NVM_SYNC_ENABLE
#if defined(PLATFORM_MSP430)
#define TIKU_NVM_SYNC_ENABLE  0
#else
#define TIKU_NVM_SYNC_ENABLE  1
#endif
#endif

/*---------------------------------------------------------------------------*/
/* REASONS                                                                   */
/*---------------------------------------------------------------------------*/

#define TIKU_NVM_SYNC_INTERVAL    0u  /**< the hook's own interval has passed  */
#define TIKU_NVM_SYNC_SLEEP       1u  /**< a sleep that loses SRAM follows     */
#define TIKU_NVM_SYNC_SHUTDOWN    2u  /**< a reset the kernel starts follows   */
#define TIKU_NVM_SYNC_POWER_FAIL  3u  /**< the supply is failing               */

/*---------------------------------------------------------------------------*/
/* PACE                                                                      */
/*---------------------------------------------------------------------------*/

/** @brief The life an interval sync is paced for: twice the declared life,
 *         so a hook's syncs spend at most half the rating, the level the
 *         wear check warns at, and the owner's other writes the rest. */
#ifndef TIKU_NVM_SYNC_LIFE_DAYS
#define TIKU_NVM_SYNC_LIFE_DAYS  (2u * TIKU_NVM_LIFE_DAYS)
#endif

/** @brief Fewest seconds between two interval syncs of one hook, the
 *         interval where the medium states no limit. */
#ifndef TIKU_NVM_SYNC_FLOOR_S
#define TIKU_NVM_SYNC_FLOOR_S  60u
#endif

/*---------------------------------------------------------------------------*/
/* HOOKS                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief One owner's values and how often they may be stored.
 *
 * The owner fills `name`, `save` and `every_s` and keeps the hook for as
 * long as the kernel runs; tiku_nvm_sync_register() sets the rest.
 */
typedef struct tiku_nvm_sync_hook {
    const char *name;                   /**< its line in /sys/mem/sync       */
    void (*save)(uint8_t reason);       /**< stores the values; a TIKU_NVM_SYNC_* */
    uint32_t every_s;                   /**< seconds between interval syncs  */
    uint32_t due_s;                     /**< uptime of the next interval sync */
    struct tiku_nvm_sync_hook *next;    /**< the next registered hook        */
} tiku_nvm_sync_hook_t;

#if TIKU_NVM_SYNC_ENABLE

/**
 * @brief Add @p h to the hooks; its first interval sync is every_s from now.
 *
 * Raises every_s to TIKU_NVM_SYNC_FLOOR_S.  A hook registered twice is
 * listed once.
 */
void tiku_nvm_sync_register(tiku_nvm_sync_hook_t *h);

/**
 * @brief Run every hook for @p reason, oldest registration first.
 *
 * Each hook's next interval sync is then every_s from now.
 *
 * @param reason  TIKU_NVM_SYNC_SLEEP, _SHUTDOWN or _POWER_FAIL
 */
void tiku_nvm_sync(uint8_t reason);

/**
 * @brief Run, for TIKU_NVM_SYNC_INTERVAL, each hook whose interval has
 *        passed.
 *
 * The scheduler calls it on every pass of its loop, before it idles, so no
 * timer is armed for it: a hook runs at the first pass after it is due.
 */
void tiku_nvm_sync_poll(void);

/** @brief The registered hooks, oldest first; NULL when there are none. */
const tiku_nvm_sync_hook_t *tiku_nvm_sync_hooks(void);

/** @brief Syncs run since boot, each hook a sync. */
uint32_t tiku_nvm_sync_count(void);

/** @brief The reason of the last sync, or 0xFF before the first. */
uint8_t tiku_nvm_sync_last(void);

/** @brief Name of a TIKU_NVM_SYNC_* reason; "none" for anything else. */
const char *tiku_nvm_sync_reason_name(uint8_t reason);

#else /* !TIKU_NVM_SYNC_ENABLE: no hooks, and every call does nothing. */

static inline void tiku_nvm_sync_register(tiku_nvm_sync_hook_t *h) { (void)h; }
static inline void tiku_nvm_sync(uint8_t reason) { (void)reason; }
static inline void tiku_nvm_sync_poll(void) { }

#endif /* TIKU_NVM_SYNC_ENABLE */

#endif /* TIKU_NVM_SYNC_H_ */
