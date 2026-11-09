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
 * each hook's due time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/memory/tiku_nvm_sync.h"

#if TIKU_NVM_SYNC_ENABLE

#include "kernel/timers/tiku_clock.h"

static tiku_nvm_sync_hook_t *hooks;
static uint32_t syncs;
static uint8_t  last = 0xFFu;

/**
 * @brief Run each hook for @p reason, every one or with @p due_only those
 *        whose due time has come, and set its next due time.
 */
static void run(uint8_t reason, int due_only)
{
    uint32_t now = (uint32_t)tiku_clock_seconds();
    tiku_nvm_sync_hook_t *h;

    for (h = hooks; h != NULL; h = h->next) {
        if (due_only && (int32_t)(now - h->due_s) < 0) {
            continue;
        }
        h->due_s = now + h->every_s;
        syncs++;
        last = reason;
        h->save(reason);
    }
}

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

const char *tiku_nvm_sync_reason_name(uint8_t reason)
{
    static const char *const names[] = {
        "interval", "sleep", "shutdown", "power-fail"
    };

    return (reason < sizeof names / sizeof names[0]) ? names[reason] : "none";
}

#endif /* TIKU_NVM_SYNC_ENABLE */
