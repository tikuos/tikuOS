/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_sync.c - the registered sync hooks and the interval poll.
 *
 * The hooks are a list the owners keep.  The poll compares the uptime with
 * each hook's due time; the port's supply warning calls power_fail() from
 * its interrupt.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/memory/tiku_nvm_sync.h"

#if TIKU_NVM_SYNC_ENABLE

#include "kernel/timers/tiku_clock.h"
#include <hal/tiku_mem_hal.h>

static tiku_nvm_sync_hook_t *hooks;
static uint32_t syncs;
static uint8_t  last = 0xFFu;
static volatile uint8_t running;        /* run() is calling hooks */
static volatile uint8_t failed;         /* the warning has run its hooks */
static uint16_t warning_mv;

/**
 * @brief Run each hook for @p reason, every one or with @p due_only those
 *        whose due time has come, and set its next due time.
 */
static void run(uint8_t reason, int due_only)
{
    uint32_t now = (uint32_t)tiku_clock_seconds();
    tiku_nvm_sync_hook_t *h;

    running = 1u;
    for (h = hooks; h != NULL; h = h->next) {
        if (due_only && (int32_t)(now - h->due_s) < 0) {
            continue;
        }
        if (reason == TIKU_NVM_SYNC_POWER_FAIL && !h->on_power_fail) {
            continue;
        }
        h->due_s = now + h->every_s;
        syncs++;
        last = reason;
        h->save(reason);
    }
    running = 0u;
}

#if TIKU_MEM_ARCH_POWER_FAIL
/**
 * @brief The supply warning: run the power-fail hooks, once a boot, and not
 *        over a sync it interrupted, which is storing the same values.
 */
static void power_fail(void)
{
    if (failed || running) {
        return;
    }
    failed = 1u;
    run(TIKU_NVM_SYNC_POWER_FAIL, 0);
}
#endif

void tiku_nvm_sync_register(tiku_nvm_sync_hook_t *h)
{
    tiku_nvm_sync_hook_t **at;

    if (h == NULL || h->save == NULL) {
        return;
    }
    for (at = &hooks; *at != NULL; at = &(*at)->next) {
        if (*at == h) {
            return;
        }
    }
    if (h->every_s < TIKU_NVM_SYNC_FLOOR_S) {
        h->every_s = TIKU_NVM_SYNC_FLOOR_S;
    }
    h->due_s = (uint32_t)tiku_clock_seconds() + h->every_s;
    h->next = NULL;
    *at = h;
#if TIKU_MEM_ARCH_POWER_FAIL
    if (h->on_power_fail && warning_mv == 0u) {
        warning_mv = tiku_mem_arch_power_fail_arm(power_fail);
    }
#endif
}

void tiku_nvm_sync(uint8_t reason)
{
    run(reason, 0);
}

void tiku_nvm_sync_poll(void)
{
    run(TIKU_NVM_SYNC_INTERVAL, 1);
}

const tiku_nvm_sync_hook_t *tiku_nvm_sync_hooks(void)
{
    return hooks;
}

uint32_t tiku_nvm_sync_count(void)
{
    return syncs;
}

uint8_t tiku_nvm_sync_last(void)
{
    return last;
}

uint16_t tiku_nvm_sync_warning_mv(void)
{
    return warning_mv;
}

const char *tiku_nvm_sync_reason_name(uint8_t reason)
{
    static const char *const names[] = {
        "interval", "sleep", "shutdown", "power-fail"
    };

    return (reason < sizeof names / sizeof names[0]) ? names[reason] : "none";
}

#endif /* TIKU_NVM_SYNC_ENABLE */
