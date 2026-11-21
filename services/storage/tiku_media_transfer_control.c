/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_transfer_control.c - retained command results and bounded cleanup steps.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_transfer_control.h"
#include <string.h>

/** @brief Service-private configuration, outstanding commands and backend phase. */
struct tiku_mt_control {
    tiku_mt_control_ops_t ops;
    void *context;
    tiku_mt_control_profile_t profile;
    uint64_t generation, sequence;
    struct { uint64_t token; int result; unsigned done; } results[2];
    tiku_mt_control_state_t state;
    tiku_mt_control_phase_t phase;
    int enabling, stopping, enable_result, error;
    unsigned cleanup_required, dispatching;
};

/** @brief Reject invalid, reentrant and wrong-owner calls before mutation. */
static int allowed(tiku_mt_control_t *c)
{
    if (!c) return TIKU_MFS_ARG;
    if (c->dispatching) return TIKU_MFS_BUSY;
    return c->ops.in_owner(c->context) ? 0 : TIKU_MT_CONTROL_OWNER;
}
/** @brief Count slots whose terminal results have been taken. */
static unsigned free_slots(const tiku_mt_control_t *c)
{ return !c->results[0].token + !c->results[1].token; }
/** @brief Retain a command result and detach its active slot index. */
static void complete(tiku_mt_control_t *c, int *slot, int result)
{
    if (*slot >= 0) { c->results[*slot].result=result; c->results[*slot].done=1; *slot=-1; }
}

size_t tiku_mt_control_size(void) { return sizeof(tiku_mt_control_t); }
size_t tiku_mt_control_align(void) { return _Alignof(tiku_mt_control_t); }
tiku_mt_control_t *tiku_mt_control_init(void *storage, size_t size, tiku_mt_control_ops_t ops, void *context)
{
    if (!storage || size<sizeof(tiku_mt_control_t) || (uintptr_t)storage%_Alignof(tiku_mt_control_t) ||
        !ops.in_owner || !ops.step || !ops.in_owner(context)) return NULL;
    tiku_mt_control_t *c=storage;
    memset(c,0,sizeof *c); c->ops=ops; c->context=context; c->enabling=c->stopping=-1;
    return c;
}
int tiku_mt_control_configure(tiku_mt_control_t *c, const tiku_mt_control_profile_t *p, uint64_t *generation)
{
    int rc=allowed(c); if (rc) return rc;
    if (!p || !generation || !p->port) return TIKU_MFS_ARG;
    unsigned key=0,volume=0;
    for (unsigned i=0;i<32;i++) key|=p->key[i];
    for (unsigned i=0;i<16;i++) volume|=p->volume[i];
    if (!key || !volume) return TIKU_MFS_ARG;
    if ((c->state!=TIKU_MT_CONTROL_STOPPED && c->state!=TIKU_MT_CONTROL_UNCONFIGURED) ||
        c->cleanup_required || free_slots(c)!=2) return TIKU_MFS_BUSY;
    if (c->generation==UINT64_MAX) return TIKU_MFS_LIMIT;
    c->profile=*p; *generation=++c->generation; c->state=TIKU_MT_CONTROL_STOPPED; c->error=0;
    return 0;
}
int tiku_mt_control_forget(tiku_mt_control_t *c)
{
    int rc=allowed(c); if (rc) return rc;
    if ((c->state!=TIKU_MT_CONTROL_STOPPED && c->state!=TIKU_MT_CONTROL_UNCONFIGURED) ||
        c->cleanup_required || free_slots(c)!=2) return TIKU_MFS_BUSY;
    memset(&c->profile,0,sizeof c->profile);
    if (c->generation!=UINT64_MAX) ++c->generation;
    c->state=TIKU_MT_CONTROL_UNCONFIGURED; c->error=0; return 0;
}
int tiku_mt_control_submit(tiku_mt_control_t *c, tiku_mt_control_command_t command, uint64_t generation, uint64_t *token)
{
    int rc=allowed(c); if (rc) return rc;
    if (!token || (command!=TIKU_MT_CONTROL_ENABLE && command!=TIKU_MT_CONTROL_STOP)) return TIKU_MFS_ARG;
    if (c->state==TIKU_MT_CONTROL_UNCONFIGURED) return TIKU_MFS_NOFS;
    if (generation!=c->generation) return TIKU_MFS_STALE;
    if (command==TIKU_MT_CONTROL_STOP && c->stopping>=0) {
        *token=c->results[c->stopping].token; return 0;
    }
    if (command==TIKU_MT_CONTROL_ENABLE && c->state!=TIKU_MT_CONTROL_STOPPED) return TIKU_MFS_BUSY;
    if (!free_slots(c)) return TIKU_MFS_BUSY;
    if (c->sequence==UINT64_MAX || (command==TIKU_MT_CONTROL_ENABLE && c->sequence==UINT64_MAX-1)) return TIKU_MFS_LIMIT;
    int slot=c->results[0].token ? 1 : 0;
    c->results[slot].token=++c->sequence; c->results[slot].done=0; c->results[slot].result=0;
    *token=c->sequence;
    if (command==TIKU_MT_CONTROL_ENABLE) {
        c->enabling=slot; c->enable_result=0; c->error=0; c->cleanup_required=1;
        c->phase=TIKU_MT_CONTROL_PREPARE; c->state=TIKU_MT_CONTROL_STARTING;
    } else {
        c->stopping=slot;
        if (!c->cleanup_required) { complete(c,&c->stopping,0); return 0; }
        if (c->enabling>=0 && !c->enable_result) c->enable_result=TIKU_MT_CONTROL_CANCELLED;
        if (c->state!=TIKU_MT_CONTROL_STOPPING) c->phase=TIKU_MT_CONTROL_QUIESCE;
        c->state=TIKU_MT_CONTROL_STOPPING;
    }
    return 0;
}
int tiku_mt_control_step(tiku_mt_control_t *c)
{
    int rc=allowed(c); if (rc) return rc;
    if (c->state!=TIKU_MT_CONTROL_STARTING && c->state!=TIKU_MT_CONTROL_STOPPING) return 0;
    c->dispatching=1; rc=c->ops.step(c->context,c->phase,&c->profile); c->dispatching=0;
    if (rc==TIKU_MT_CONTROL_PENDING || rc==TIKU_MFS_BUSY) return TIKU_MT_CONTROL_PENDING;
    if (rc>0) rc=TIKU_MFS_IO;
    if (rc) {
        c->error=rc;
        if (c->state==TIKU_MT_CONTROL_STARTING) {
            c->enable_result=rc; c->state=TIKU_MT_CONTROL_STOPPING; c->phase=TIKU_MT_CONTROL_QUIESCE;
            return TIKU_MT_CONTROL_PENDING;
        }
        c->state=TIKU_MT_CONTROL_FAILED;
        complete(c,&c->enabling,c->enable_result && c->enable_result!=TIKU_MT_CONTROL_CANCELLED ? c->enable_result : rc);
        complete(c,&c->stopping,rc); return 0;
    }
    if (c->state==TIKU_MT_CONTROL_STARTING) {
        if (c->phase==TIKU_MT_CONTROL_PREPARE) c->phase=TIKU_MT_CONTROL_ATTACH;
        else { c->state=TIKU_MT_CONTROL_READY; complete(c,&c->enabling,0); return 0; }
    } else if (c->phase==TIKU_MT_CONTROL_WIPE) {
        c->cleanup_required=0; c->state=TIKU_MT_CONTROL_STOPPED;
        complete(c,&c->enabling,c->enable_result); complete(c,&c->stopping,0); return 0;
    } else c->phase=(tiku_mt_control_phase_t)(c->phase+1);
    return TIKU_MT_CONTROL_PENDING;
}
int tiku_mt_control_observe(tiku_mt_control_t *c, tiku_mt_control_snapshot_t *out)
{
    int rc=allowed(c); if (rc) return rc;
    if (!out) return TIKU_MFS_ARG;
    *out=(tiku_mt_control_snapshot_t){.generation=c->generation,.state=c->state,.error=c->error,
        .enabling=c->enabling>=0 ? c->results[c->enabling].token : 0,
        .stopping=c->stopping>=0 ? c->results[c->stopping].token : 0,
        .cleanup_required=c->cleanup_required,.completion_slots_free=free_slots(c)};
    return 0;
}
int tiku_mt_control_take(tiku_mt_control_t *c, uint64_t token, int *result)
{
    int rc=allowed(c); if (rc) return rc;
    if (!token || !result) return TIKU_MFS_ARG;
    for (unsigned i=0;i<2;i++) if (c->results[i].token==token) {
        if (!c->results[i].done) return TIKU_MT_CONTROL_PENDING;
        *result=c->results[i].result; memset(&c->results[i],0,sizeof c->results[i]); return 0;
    }
    return TIKU_MFS_STALE;
}
int tiku_mt_control_release_one(tiku_media_store_t *s, uint32_t client)
{
    if (!s || !s->fs || !client) return TIKU_MFS_ARG;
    if (s->count || s->running) return TIKU_MFS_BUSY;
    tiku_media_request_t request={.client=client,.permissions=TIKU_MEDIA_READ|TIKU_MEDIA_WRITE};
    if (s->writer==client) request.op=TIKU_MEDIA_RELEASE;
    else {
        unsigned i=0;
        while (i<TIKU_MFS_READERS && (!s->readers[i].token || s->readers[i].client!=client)) ++i;
        if (i==TIKU_MFS_READERS) return 0;
        request.op=TIKU_MEDIA_CLOSE; request.id=s->readers[i].token;
    }
    int rc=tiku_media_store_submit(s,&request); if (rc) return rc;
    (void)tiku_media_store_step(s);
    return request.result ? request.result : TIKU_MT_CONTROL_PENDING;
}
int tiku_mt_control_wipe(void *buffer, size_t size, size_t *cursor)
{
    if (!cursor || *cursor>size || (!buffer && size)) return TIKU_MFS_ARG;
    size_t n=size-*cursor; if (n>512) n=512;
    if (n) memset((unsigned char *)buffer+*cursor,0,n);
    *cursor+=n; return *cursor==size ? 0 : TIKU_MT_CONTROL_PENDING;
}
