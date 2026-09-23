/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_layout.c - staged memory budgets and the boot that applies them.
 *
 * The record establishes ownership; a region with nothing on it is provisioned
 * on first use.  A found header never grants ownership: that takes recovery.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_layout.h"
#include "kernel/fs/tiku_tfs.h"

#include <string.h>

/* MSP430 keeps its store and NVM tier in fixed arrays; no knob moves them. */
#if !defined(PLATFORM_MSP430)

/*---------------------------------------------------------------------------*/
/* KNOBS                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Bounds of the NVM tier: whole steps, leaving the store its minimum. */
static int
nvm_tier_bounds(const tiku_layout_env_t *e, tiku_layout_knob_t *k)
{
    size_t size = e->region.size;
    size_t min_store = tiku_tfs_region_size();

    if (e->region.base == NULL || e->step == 0u || size <= min_store) {
        return 0;
    }
    k->step    = e->step;
    k->floor   = e->step;
    k->ceiling = (uint32_t)(((size - min_store) / e->step) * e->step);
    k->dflt    = e->default_tier;
    return k->ceiling >= k->floor;
}

typedef struct {
    uint8_t     id;
    uint8_t     effect;
    const char *name;
    int       (*bounds)(const tiku_layout_env_t *e, tiku_layout_knob_t *k);
} knob_desc_t;

static const knob_desc_t knobs[] = {
    { TIKU_KNOB_NVM_TIER, TIKU_LAYOUT_EFFECT_STORE, "nvm.tier",
      nvm_tier_bounds },
};

#define KNOB_COUNT  (sizeof knobs / sizeof knobs[0])

int
tiku_layout_knob_env(const tiku_layout_env_t *env, unsigned i,
                     tiku_layout_knob_t *out)
{
    if (env == NULL || out == NULL || i >= KNOB_COUNT) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->id     = knobs[i].id;
    out->effect = knobs[i].effect;
    out->name   = knobs[i].name;
    return knobs[i].bounds(env, out);
}

/** @brief The descriptor index of knob @p id, or -1. */
static int
knob_index(uint8_t id)
{
    unsigned i;

    for (i = 0u; i < KNOB_COUNT; i++) {
        if (knobs[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------*/
/* RECORD HELPERS                                                            */
/*---------------------------------------------------------------------------*/

uint32_t
tiku_layout_kv_get(const tiku_layout_kv_t *kv, uint8_t n, uint8_t id,
                   uint32_t dflt)
{
    uint8_t i;

    for (i = 0u; i < n && i < TIKU_LAYOUT_KNOBS_MAX; i++) {
        if (kv[i].id == id) {
            return kv[i].value;
        }
    }
    return dflt;
}

/** @brief Set @p id to @p v in a knob list, appending while there is room. */
static void
kv_set(tiku_layout_kv_t *kv, uint8_t *n, uint8_t id, uint32_t v)
{
    uint8_t i;

    for (i = 0u; i < *n; i++) {
        if (kv[i].id == id) {
            kv[i].value = v;
            return;
        }
    }
    if (*n < TIKU_LAYOUT_KNOBS_MAX) {
        memset(&kv[*n], 0, sizeof kv[*n]);
        kv[*n].id = id;
        kv[*n].value = v;
        (*n)++;
    }
}

/** @brief Fold one 32-bit word into an FNV-1a hash. */
static uint32_t
fnv(uint32_t h, uint32_t v)
{
    unsigned i;

    for (i = 0u; i < 4u; i++) {
        h ^= (v >> (8u * i)) & 0xFFu;
        h *= 16777619u;
    }
    return h;
}

/** @brief FNV-1a over every byte of @p r before its check field. */
static uint32_t
rec_sum(const tiku_layout_record_t *r)
{
    const uint8_t *b = (const uint8_t *)r;
    uint32_t h = 2166136261u;
    size_t i;

    for (i = 0u; i < offsetof(tiku_layout_record_t, check); i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

void
tiku_layout_seal(tiku_layout_record_t *r)
{
    r->check = rec_sum(r);
}

/** @brief A record this build can act on. */
static int
rec_valid(const tiku_layout_record_t *r)
{
    static const uint8_t zero[TIKU_LAYOUT_ID_BYTES];
    return r->schema == TIKU_LAYOUT_SCHEMA && r->check == rec_sum(r) &&
           memcmp(r->identity, zero, sizeof zero) != 0 &&
           r->request_n <= TIKU_LAYOUT_KNOBS_MAX &&
           r->request_method <= TIKU_LAYOUT_METHOD_ERASE &&
           r->n_applied <= TIKU_LAYOUT_KNOBS_MAX &&
           r->n_pending <= TIKU_LAYOUT_KNOBS_MAX &&
           r->phase <= TIKU_LAYOUT_PHASE_REWRITING &&
           r->method <= TIKU_LAYOUT_METHOD_ERASE;
}

/** @brief The image identity a record is written under. */
static uint32_t
contract_of(const tiku_layout_env_t *e)
{
    uint32_t h = 2166136261u;
    unsigned i;

    h = fnv(h, TIKU_LAYOUT_SCHEMA);
    h = fnv(h, (uint32_t)e->region.size);
    h = fnv(h, (uint32_t)(uintptr_t)e->region.base);
    h = fnv(h, e->step);
    h = fnv(h, e->default_tier);
    h = fnv(h, (uint32_t)tiku_tfs_region_size());
    h = fnv(h, (uint32_t)TIKU_TFS_SLOT_BYTES);
    for (i = 0u; i < KNOB_COUNT; i++) {
        h = fnv(h, ((uint32_t)knobs[i].id << 8) | knobs[i].effect);
    }
    return h;
}

/** @brief A fresh record for this image. */
static void
rec_fresh(const tiku_layout_env_t *e, tiku_layout_record_t *r)
{
    memset(r, 0, sizeof *r);
    r->schema   = TIKU_LAYOUT_SCHEMA;
    r->contract = contract_of(e);
}

static int
commit(const tiku_layout_env_t *e, tiku_layout_record_t *r)
{
    tiku_layout_seal(r);
    return (e->commit != NULL && e->commit(e->commit_ctx, r) == 0) ? TIKU_LAYOUT_OK
                                               : TIKU_LAYOUT_E_IO;
}

/*---------------------------------------------------------------------------*/
/* THE STORE, SEEN THROUGH THE REGION                                        */
/*---------------------------------------------------------------------------*/

typedef struct {
    const tiku_layout_env_t *e;
    size_t                   off;
} sub_ctx_t;

/** @brief Backend write for a store at an offset inside the region. */
static int
sub_write(tiku_nvm_backend_t *be, size_t off, const void *src, size_t len)
{
    const sub_ctx_t *c = (const sub_ctx_t *)be->ctx;

    if (off > be->size || len > be->size - off) {
        return -1;
    }
    return c->e->write(c->e->write_ctx, c->off + off, src, len);
}

/** @brief A backend for the store whose header sits at @p off. */
static void
sub_backend(const tiku_layout_env_t *e, size_t off, sub_ctx_t *c,
            tiku_nvm_backend_t *be)
{
    c->e   = e;
    c->off = off;
    be->base  = e->region.base + off;
    be->size  = e->region.size - off;
    be->write = sub_write;
    be->erase = NULL;
    be->ctx   = c;
}

static int
region_present(const tiku_layout_env_t *e)
{
    return e->region.base != NULL && e->region.size > tiku_tfs_region_size();
}

/** @brief Probe the store at offset @p off of the region. */
static void
probe_at(const tiku_layout_env_t *e, size_t off, tiku_tfs_probe_t *p)
{
    tiku_nvm_backend_t be;
    sub_ctx_t c;

    memset(p, 0, sizeof *p);
    if (off >= e->region.size) {
        p->kind = TFS_PROBE_TOOSMALL;
        return;
    }
    sub_backend(e, off, &c, &be);
    (void)tiku_tfs_probe(&be, p);
}

/** @brief Whether the store at @p off can be rewritten without losing files. */
static int
store_empty(const tiku_layout_env_t *e, size_t off, tiku_tfs_probe_t *p)
{
    probe_at(e, off, p);
    if (p->kind == TFS_PROBE_COMPATIBLE) {
        return p->live == 0u;
    }
    return p->kind == TFS_PROBE_BLANK &&
           tiku_tfs_locate(&e->region, e->step, NULL, 0) == 0;
}

/*---------------------------------------------------------------------------*/
/* VALIDATION                                                                */
/*---------------------------------------------------------------------------*/

/** @brief The applied NVM tier: the record's, or the default. */
static uint32_t
applied_tier(const tiku_layout_env_t *e, const tiku_layout_record_t *r)
{
    return tiku_layout_kv_get(r->applied, r->n_applied, TIKU_KNOB_NVM_TIER,
                              e->default_tier);
}

/**
 * @brief Check every knob of a request against its bounds.
 *
 * Fills @p plan's effect and, on a refusal, the knob and a value that passes.
 */
static int
check_kvs(const tiku_layout_env_t *e, const tiku_layout_record_t *r,
          const tiku_layout_kv_t *kv, uint8_t n, tiku_layout_plan_t *plan)
{
    uint8_t i, j;

    if (n == 0u || n > TIKU_LAYOUT_KNOBS_MAX) {
        return TIKU_LAYOUT_E_INVAL;
    }
    for (i = 0u; i < n; i++) {
        tiku_layout_knob_t k;
        int idx = knob_index(kv[i].id);
        uint32_t now;

        for (j = 0u; j < i; j++) {
            if (kv[j].id == kv[i].id) {
                plan->bad_knob = kv[i].id;
                return TIKU_LAYOUT_E_INVAL;          /* named twice */
            }
        }
        if (idx < 0 || !tiku_layout_knob_env(e, (unsigned)idx, &k)) {
            plan->bad_knob = kv[i].id;
            return TIKU_LAYOUT_E_KNOB;
        }
        if (kv[i].value < k.floor || kv[i].value > k.ceiling) {
            plan->bad_knob = kv[i].id;
            plan->nearest  = (kv[i].value < k.floor) ? k.floor : k.ceiling;
            return TIKU_LAYOUT_E_RANGE;
        }
        if (k.step > 1u && kv[i].value % k.step != 0u) {
            plan->bad_knob = kv[i].id;
            plan->nearest  = kv[i].value - kv[i].value % k.step;
            if (plan->nearest < k.floor) {
                plan->nearest += k.step;
            }
            return TIKU_LAYOUT_E_STEP;
        }
        now = tiku_layout_kv_get(r->applied, r->n_applied, k.id, k.dflt);
        if (kv[i].value != now && k.effect > plan->effect) {
            plan->effect = k.effect;
        }
    }
    return TIKU_LAYOUT_OK;
}

/** Validate an extent before any pointer arithmetic or persistent write. */
static int
base_valid(const tiku_layout_env_t *e, uint32_t base)
{
    tiku_layout_knob_t k;
    return tiku_layout_knob_env(e, 0u, &k) && base >= k.floor &&
           base <= k.ceiling && base % k.step == 0u;
}

static int
record_usable(const tiku_layout_env_t *e, const tiku_layout_record_t *r)
{
    if (!rec_valid(r)) {
        return TIKU_LAYOUT_E_RECOVERY;
    }
    if (r->contract != contract_of(e)) {
        return TIKU_LAYOUT_E_STALE;
    }
    if (r->n_applied != 1u || r->applied[0].id != TIKU_KNOB_NVM_TIER ||
        !base_valid(e, r->applied[0].value)) {
        return TIKU_LAYOUT_E_RECOVERY;
    }
    return TIKU_LAYOUT_OK;
}

int
tiku_layout_plan_env(const tiku_layout_env_t *e,
                     const tiku_layout_request_t *req,
                     tiku_layout_plan_t *plan)
{
    tiku_layout_record_t r;
    tiku_tfs_probe_t p;
    int rc;

    if (e == NULL || e->rec == NULL || req == NULL || plan == NULL) {
        return TIKU_LAYOUT_E_INVAL;
    }
    memset(plan, 0, sizeof *plan);
    if (req->method > TIKU_LAYOUT_METHOD_ERASE) {
        return TIKU_LAYOUT_E_INVAL;
    }
    r = *e->rec;
    rc = record_usable(e, &r);
    if (rc != TIKU_LAYOUT_OK) {
        return rc;
    }
    rc = check_kvs(e, &r, req->kv, req->n, plan);
    if (rc != TIKU_LAYOUT_OK || plan->effect != TIKU_LAYOUT_EFFECT_STORE) {
        return rc;
    }
    if (!store_empty(e, applied_tier(e, &r), &p)) {
        plan->files = p.live;
        plan->bytes = p.bytes;
        if (req->method != TIKU_LAYOUT_METHOD_ERASE) {
            return TIKU_LAYOUT_E_LOSS;
        }
    }
    return TIKU_LAYOUT_OK;
}

/*---------------------------------------------------------------------------*/
/* REQUESTS                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Whether a pending request is the one @p req describes. */
static int
same_request(const tiku_layout_record_t *r, const tiku_layout_request_t *req)
{
    uint8_t i;

    if (r->request_n != req->n || r->request_method != req->method ||
        r->request_gen != req->expect_gen || r->request_rev != req->expect_rev) {
        return 0;
    }
    for (i = 0u; i < req->n; i++) {
        if (tiku_layout_kv_get(r->pending, r->request_n, req->kv[i].id,
                               ~req->kv[i].value) != req->kv[i].value) {
            return 0;
        }
    }
    return 1;
}

int
tiku_layout_stage_env(const tiku_layout_env_t *e,
                      const tiku_layout_request_t *req,
                      tiku_layout_plan_t *plan)
{
    tiku_layout_record_t r;
    uint8_t i;
    int rc;

    if (e == NULL || e->rec == NULL || req == NULL || plan == NULL ||
        req->op == 0u || !req->has_expect || req->n == 0u || req->n > TIKU_LAYOUT_KNOBS_MAX ||
        req->method > TIKU_LAYOUT_METHOD_ERASE) {
        return TIKU_LAYOUT_E_INVAL;
    }
    memset(plan, 0, sizeof *plan);
    r = *e->rec;
    rc = record_usable(e, &r);
    if (rc != TIKU_LAYOUT_OK) {
        return rc;
    }
    if (memcmp(req->identity, r.identity, sizeof r.identity) != 0) {
        return TIKU_LAYOUT_E_STALE;
    }
    rc = check_kvs(e, &r, req->kv, req->n, plan);
    if (rc != TIKU_LAYOUT_OK) {
        return rc;
    }
    if (r.phase != TIKU_LAYOUT_PHASE_NONE) {
        return TIKU_LAYOUT_E_BUSY;
    }
    if (r.n_pending != 0u) {
        if (r.op != req->op) {
            return TIKU_LAYOUT_E_BUSY;
        }
        return same_request(&r, req) ? TIKU_LAYOUT_OK : TIKU_LAYOUT_E_REUSED;
    }
    if (req->op == r.receipt_op) {
        return same_request(&r, req) ? r.receipt : TIKU_LAYOUT_E_REUSED;
    }
    if (req->expect_gen != r.generation || req->expect_rev != r.revision) {
        return TIKU_LAYOUT_E_STALE;
    }
    rc = tiku_layout_plan_env(e, req, plan);
    if (rc != TIKU_LAYOUT_OK || plan->effect == TIKU_LAYOUT_EFFECT_NONE) {
        return rc;                               /* a no-op writes nothing */
    }
    if (r.revision == UINT32_MAX || r.generation == UINT32_MAX) {
        return TIKU_LAYOUT_E_RANGE;              /* never wrap replay counters */
    }
    r.n_pending = 0u;
    for (i = 0u; i < req->n; i++) {
        kv_set(r.pending, &r.n_pending, req->kv[i].id, req->kv[i].value);
    }
    r.op       = req->op;
    r.method   = req->method;
    r.request_n = req->n;
    r.request_method = req->method;
    r.request_gen = req->expect_gen;
    r.request_rev = req->expect_rev;
    r.revision++;
    r.contract = contract_of(e);
    return commit(e, &r);
}

int
tiku_layout_cancel_env(const tiku_layout_env_t *e, uint32_t op)
{
    tiku_layout_record_t r;

    if (e == NULL || e->rec == NULL) {
        return TIKU_LAYOUT_E_INVAL;
    }
    r = *e->rec;
    if (record_usable(e, &r) != TIKU_LAYOUT_OK) {
        return TIKU_LAYOUT_E_RECOVERY;
    }
    if (!rec_valid(&r) || r.n_pending == 0u || r.op != op ||
        r.phase != TIKU_LAYOUT_PHASE_NONE) {
        return TIKU_LAYOUT_E_PHASE;
    }
    r.n_pending  = 0u;
    r.receipt    = TIKU_LAYOUT_CANCELLED;
    r.receipt_op = op;
    r.op         = 0u;
    r.method     = TIKU_LAYOUT_METHOD_NONE;
    if (r.revision != UINT32_MAX) { r.revision++; }
    return commit(e, &r);
}

/*---------------------------------------------------------------------------*/
/* BOOT                                                                      */
/*---------------------------------------------------------------------------*/

/** @brief End a pending request unrun, recording why. */
static void
refuse(const tiku_layout_env_t *e, tiku_layout_record_t *r,
       tiku_layout_state_t *st, int why)
{
    st->outcome    = (int16_t)why;
    st->outcome_op = r->op;
    r->n_pending   = 0u;
    r->receipt     = (int16_t)why;
    r->receipt_op  = r->op;
    r->op          = 0u;
    r->method      = TIKU_LAYOUT_METHOD_NONE;
    if (r->revision != UINT32_MAX) { r->revision++; }
    if (commit(e, r) != TIKU_LAYOUT_OK) {
        st->store = TIKU_LAYOUT_STORE_HELD;
        st->held = TIKU_LAYOUT_HELD_IO;
        st->tier = 0u;
        st->outcome = TIKU_LAYOUT_E_IO;
    }
}

/**
 * @brief Carry out a consumed rewrite: retire the old header, format the new
 *        store, then record the operation as finished.
 *
 * Runs at boot and on resume.  A failure leaves the record saying rewriting,
 * so the store stays held until an explicit resume.
 */
static int
finish_rewrite(const tiku_layout_env_t *e, tiku_layout_record_t *r,
               tiku_layout_state_t *st)
{
    static tiku_tfs_t fs;
    static const uint8_t zero[TIKU_TFS_SB_BYTES];
    tiku_nvm_backend_t be;
    sub_ctx_t c;
    uint32_t dst = r->dst_base;
    tiku_layout_record_t done;

    if (record_usable(e, r) != TIKU_LAYOUT_OK ||
        !base_valid(e, r->src_base) || !base_valid(e, dst) ||
        applied_tier(e, r) != dst || r->src_base == dst ||
        r->op == 0u || r->n_pending != 0u || e->write == NULL) {
        return TIKU_LAYOUT_E_STALE;
    }

    st->store = TIKU_LAYOUT_STORE_HELD;
    st->held  = TIKU_LAYOUT_HELD_INTERRUPTED;
    st->tier  = 0u;
    st->outcome_op = r->op;
    st->outcome = TIKU_LAYOUT_E_IO;
    /* Retire the whole old header, not only its magic: matching geometry
     * words alone still read as a torn store to a later locate. */
    if (r->src_base != dst &&
        r->src_base <= e->region.size - TIKU_TFS_SB_BYTES &&
        memcmp(e->region.base + r->src_base, zero, sizeof zero) != 0 &&
        e->write(e->write_ctx, r->src_base, zero, sizeof zero) != 0) {
        return TIKU_LAYOUT_E_IO;
    }
    sub_backend(e, dst, &c, &be);
    if (tiku_tfs_init(&fs, &be) != TFS_OK) {
        return TIKU_LAYOUT_E_IO;
    }
    done = *r;
    done.phase      = TIKU_LAYOUT_PHASE_NONE;
    done.receipt    = TIKU_LAYOUT_OK;
    done.receipt_op = r->op;
    done.op         = 0u;
    done.method     = TIKU_LAYOUT_METHOD_NONE;
    done.src_base   = 0u;
    done.dst_base   = 0u;
    if (commit(e, &done) != TIKU_LAYOUT_OK) {
        return TIKU_LAYOUT_E_IO;
    }
    *r = done;
    st->store   = TIKU_LAYOUT_STORE_READY;
    st->held    = TIKU_LAYOUT_HELD_NONE;
    st->tier    = dst;
    st->outcome = TIKU_LAYOUT_OK;
    return TIKU_LAYOUT_OK;
}

/** @brief Run the pending request of a valid record. */
static void
boot_request(const tiku_layout_env_t *e, tiku_layout_record_t *r,
             tiku_layout_state_t *st)
{
    tiku_layout_plan_t plan;
    tiku_layout_record_t w;
    tiku_tfs_probe_t p;
    uint32_t src, dst;
    uint8_t i;
    int rc;

    memset(&plan, 0, sizeof plan);
    rc = check_kvs(e, r, r->pending, r->n_pending, &plan);
    if (rc != TIKU_LAYOUT_OK) {
        refuse(e, r, st, rc);
        return;
    }
    if (r->generation == UINT32_MAX) {
        refuse(e, r, st, TIKU_LAYOUT_E_RANGE);
        return;
    }
    src = applied_tier(e, r);
    dst = tiku_layout_kv_get(r->pending, r->n_pending, TIKU_KNOB_NVM_TIER, src);
    if (src != dst && !store_empty(e, src, &p) &&
        r->method != TIKU_LAYOUT_METHOD_ERASE) {
        refuse(e, r, st, TIKU_LAYOUT_E_LOSS);    /* files arrived since staging */
        return;
    }
    w = *r;
    for (i = 0u; i < r->n_pending; i++) {
        kv_set(w.applied, &w.n_applied, r->pending[i].id, r->pending[i].value);
    }
    w.n_pending = 0u;
    w.generation++;
    if (src == dst) {
        w.receipt    = TIKU_LAYOUT_OK;
        w.receipt_op = r->op;
        w.op         = 0u;
        w.method     = TIKU_LAYOUT_METHOD_NONE;
        st->outcome_op = r->op;
        st->outcome = (int16_t)commit(e, &w);
        if (st->outcome == TIKU_LAYOUT_OK) {
            *r = w;
        }
        return;
    }
    /* The authorisation is consumed before the first destructive write, so a
     * cut from here on leaves a record that says rewriting, never pending. */
    w.phase    = TIKU_LAYOUT_PHASE_REWRITING;
    w.src_base = src;
    w.dst_base = dst;
    if (commit(e, &w) != TIKU_LAYOUT_OK) {
        st->outcome    = TIKU_LAYOUT_E_IO;
        st->outcome_op = r->op;
        st->store      = TIKU_LAYOUT_STORE_HELD;
        st->held       = TIKU_LAYOUT_HELD_IO;
        return;
    }
    *r = w;
    (void)finish_rewrite(e, r, st);
}

/** @brief Map a probe kind at the base to the reason the store is held. */
static uint8_t
held_for(tfs_probe_kind_t kind)
{
    switch (kind) {
    case TFS_PROBE_TORN:     return TIKU_LAYOUT_HELD_TORN;
    case TFS_PROBE_GEOMETRY: return TIKU_LAYOUT_HELD_GEOMETRY;
    case TFS_PROBE_VERSION:  return TIKU_LAYOUT_HELD_VERSION;
    default:                 return TIKU_LAYOUT_HELD_ELSEWHERE;
    }
}

/** @brief Probe only the extent named by validated ownership metadata. */
static void
boot_store(const tiku_layout_env_t *e, tiku_layout_record_t *r,
           tiku_layout_state_t *st)
{
    tiku_tfs_probe_t p;
    uint32_t base = applied_tier(e, r);

    probe_at(e, base, &p);
    if (p.kind == TFS_PROBE_COMPATIBLE) {
        st->tier  = base;
        st->store = TIKU_LAYOUT_STORE_READY;
        return;
    }
    st->tier  = 0u;
    st->store = TIKU_LAYOUT_STORE_HELD;
    st->held  = held_for(p.kind);
}

int
tiku_layout_boot_env(const tiku_layout_env_t *e, tiku_layout_state_t *st)
{
    tiku_layout_record_t r;

    if (e == NULL || st == NULL || e->rec == NULL) {
        return TIKU_LAYOUT_E_INVAL;
    }
    memset(st, 0, sizeof *st);
    r = *e->rec;
    if (!rec_valid(&r)) {
        st->record = TIKU_LAYOUT_RECORD_ABSENT;
    } else if (r.contract != contract_of(e)) {
        st->record = TIKU_LAYOUT_RECORD_FOREIGN;
    } else {
        st->record = TIKU_LAYOUT_RECORD_VALID;
    }
    if (!region_present(e)) {
        st->store = TIKU_LAYOUT_STORE_NONE;
        return TIKU_LAYOUT_OK;
    }
    if (st->record == TIKU_LAYOUT_RECORD_ABSENT &&
        tiku_tfs_may_provision(&e->region, e->default_tier, e->step)) {
        /* Nothing on the medium to own: the first use creates the store. */
        st->tier  = e->default_tier;
        st->store = TIKU_LAYOUT_STORE_PROVISION;
        return TIKU_LAYOUT_OK;
    }
    if (record_usable(e, &r) != TIKU_LAYOUT_OK) {
        st->store = TIKU_LAYOUT_STORE_HELD;
        st->held = (st->record == TIKU_LAYOUT_RECORD_FOREIGN)
                 ? TIKU_LAYOUT_HELD_CONTRACT : TIKU_LAYOUT_HELD_CONTROL;
        return TIKU_LAYOUT_OK;
    }
    if (st->record != TIKU_LAYOUT_RECORD_ABSENT &&
        r.phase == TIKU_LAYOUT_PHASE_REWRITING) {
        st->store = TIKU_LAYOUT_STORE_HELD;
        st->held  = TIKU_LAYOUT_HELD_INTERRUPTED;
        return TIKU_LAYOUT_OK;
    }
    if (r.n_pending != 0u) {
        boot_request(e, &r, st);
        if (r.phase == TIKU_LAYOUT_PHASE_REWRITING ||
            st->store != TIKU_LAYOUT_STORE_NONE) {
            return TIKU_LAYOUT_OK;               /* the rewrite decided it */
        }
    }
    boot_store(e, &r, st);
    return TIKU_LAYOUT_OK;
}

int
tiku_layout_resume_env(const tiku_layout_env_t *e, tiku_layout_state_t *st,
                       uint32_t op)
{
    tiku_layout_record_t r;

    if (e == NULL || e->rec == NULL || st == NULL) {
        return TIKU_LAYOUT_E_INVAL;
    }
    r = *e->rec;
    if (!rec_valid(&r) || r.phase != TIKU_LAYOUT_PHASE_REWRITING ||
        r.op != op || op == 0u) {
        return TIKU_LAYOUT_E_PHASE;
    }
    if (record_usable(e, &r) != TIKU_LAYOUT_OK) {
        return TIKU_LAYOUT_E_STALE;
    }
    return finish_rewrite(e, &r, st);
}

int
tiku_layout_recover_env(const tiku_layout_env_t *e, tiku_layout_state_t *st,
                        uint32_t base)
{
    tiku_layout_record_t r;
    tiku_nvm_backend_t be;
    tiku_tfs_t fs;
    sub_ctx_t c;
    static const uint8_t zero[TIKU_LAYOUT_ID_BYTES];
    int rc;

    if (e == NULL || e->rec == NULL || st == NULL || !base_valid(e, base)) {
        return TIKU_LAYOUT_E_RANGE;
    }
    if ((st->store != TIKU_LAYOUT_STORE_PROVISION &&
         (st->store != TIKU_LAYOUT_STORE_HELD || st->tier != 0u)) ||
        st->held == TIKU_LAYOUT_HELD_REBOOT ||
        (record_usable(e, e->rec) == TIKU_LAYOUT_OK &&
         (e->rec->phase != TIKU_LAYOUT_PHASE_NONE || e->rec->n_pending != 0u))) {
        return TIKU_LAYOUT_E_BUSY;
    }
    sub_backend(e, base, &c, &be);
    if (tiku_tfs_mount(&fs, &be) != TFS_OK) {
        return TIKU_LAYOUT_E_RECOVERY;
    }
    rec_fresh(e, &r);
    if (e->random == NULL ||
        e->random(e->random_ctx, r.identity, sizeof r.identity) != 0 ||
        memcmp(r.identity, zero, sizeof zero) == 0 ||
        memcmp(r.identity, e->rec->identity, sizeof r.identity) == 0) {
        return TIKU_LAYOUT_E_ENTROPY;
    }
    kv_set(r.applied, &r.n_applied, TIKU_KNOB_NVM_TIER, base);
    rc = commit(e, &r);
    if (rc != TIKU_LAYOUT_OK) {
        st->held = TIKU_LAYOUT_HELD_IO;
        return rc;
    }
    st->record = TIKU_LAYOUT_RECORD_VALID;
    st->held = TIKU_LAYOUT_HELD_REBOOT;
    return TIKU_LAYOUT_OK;
}

/*---------------------------------------------------------------------------*/
/* PARSING AND NAMES                                                         */
/*---------------------------------------------------------------------------*/

/** @brief Parse an unsigned number: decimal, 0x hex, or a K or M suffix. */
static int
parse_u32(const char *s, size_t len, uint32_t *out)
{
    uint32_t v = 0u, base = 10u, mul = 1u;
    size_t i = 0u;

    if (len >= 2u && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16u;
        i = 2u;
    } else if (len >= 1u && (s[len - 1u] == 'K' || s[len - 1u] == 'k')) {
        mul = 1024u;
        len--;
    } else if (len >= 1u && (s[len - 1u] == 'M' || s[len - 1u] == 'm')) {
        mul = 1024u * 1024u;
        len--;
    }
    if (i >= len) {
        return -1;
    }
    for (; i < len; i++) {
        char ch = s[i];
        uint32_t d;

        if (ch >= '0' && ch <= '9') {
            d = (uint32_t)(ch - '0');
        } else if (base == 16u && ch >= 'a' && ch <= 'f') {
            d = (uint32_t)(ch - 'a' + 10);
        } else if (base == 16u && ch >= 'A' && ch <= 'F') {
            d = (uint32_t)(ch - 'A' + 10);
        } else {
            return -1;
        }
        if (v > (0xFFFFFFFFu - d) / base) {
            return -1;
        }
        v = v * base + d;
    }
    if (mul > 1u && v > 0xFFFFFFFFu / mul) {
        return -1;
    }
    *out = v * mul;
    return 0;
}

/** @brief Whether @p s of length @p n equals the NUL-terminated @p lit. */
static int
tok_is(const char *s, size_t n, const char *lit)
{
    return strlen(lit) == n && memcmp(s, lit, n) == 0;
}

int
tiku_layout_parse(const char *text, tiku_layout_request_t *req)
{
    const char *p = text;
    unsigned seen = 0u;

    if (text == NULL || req == NULL) {
        return -1;
    }
    memset(req, 0, sizeof *req);
    while (*p != '\0') {
        const char *key, *eq, *val;
        size_t klen, vlen;
        unsigned i;

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        key = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' &&
               *p != '\r') {
            p++;
        }
        eq = memchr(key, '=', (size_t)(p - key));
        if (eq == NULL) {
            return -1;
        }
        klen = (size_t)(eq - key);
        val  = eq + 1;
        vlen = (size_t)(p - val);
        if (tok_is(key, klen, "op")) {
            if ((seen & 1u) || parse_u32(val, vlen, &req->op) != 0) {
                return -1;
            }
            seen |= 1u;
        } else if (tok_is(key, klen, "expect")) {
            const char *colon = memchr(val, ':', vlen);
            if ((seen & 2u) || colon == NULL ||
                parse_u32(val, (size_t)(colon - val), &req->expect_gen) != 0 ||
                parse_u32(colon + 1, vlen - (size_t)(colon - val) - 1u,
                          &req->expect_rev) != 0) {
                return -1;
            }
            seen |= 2u;
            req->has_expect = 1u;
        } else if (tok_is(key, klen, "identity")) {
            if ((seen & 4u) || vlen != 2u * TIKU_LAYOUT_ID_BYTES) {
                return -1;
            }
            seen |= 4u;
            for (i = 0u; i < TIKU_LAYOUT_ID_BYTES; i++) {
                char byte[4] = { '0', 'x', val[2u*i], val[2u*i+1u] };
                uint32_t v;
                if (parse_u32(byte, sizeof byte, &v) != 0) { return -1; }
                req->identity[i] = (uint8_t)v;
            }
        } else if (tok_is(key, klen, "method")) {
            if (seen & 8u) { return -1; }
            seen |= 8u;
            if (tok_is(val, vlen, "erase")) {
                req->method = TIKU_LAYOUT_METHOD_ERASE;
            } else if (tok_is(val, vlen, "none")) {
                req->method = TIKU_LAYOUT_METHOD_NONE;
            } else {
                return -1;
            }
        } else {
            for (i = 0u; i < KNOB_COUNT; i++) {
                if (tok_is(key, klen, knobs[i].name)) {
                    break;
                }
            }
            if (i == KNOB_COUNT || req->n >= TIKU_LAYOUT_KNOBS_MAX) {
                return -1;
            }
            req->kv[req->n].id = knobs[i].id;
            if (parse_u32(val, vlen, &req->kv[req->n].value) != 0) {
                return -1;
            }
            req->n++;
        }
    }
    return 0;
}

void
tiku_layout_identity_text(const uint8_t id[TIKU_LAYOUT_ID_BYTES], char out[33])
{
    static const char hex[] = "0123456789abcdef";
    unsigned i;
    for (i = 0; i < TIKU_LAYOUT_ID_BYTES; i++) {
        out[2u*i] = hex[id[i] >> 4];
        out[2u*i+1u] = hex[id[i] & 15u];
    }
    out[32] = '\0';
}

const char *
tiku_layout_err_name(int err)
{
    switch (err) {
    case TIKU_LAYOUT_OK:        return "ok";
    case TIKU_LAYOUT_E_INVAL:   return "invalid";
    case TIKU_LAYOUT_E_KNOB:    return "unknown-knob";
    case TIKU_LAYOUT_E_RANGE:   return "out-of-range";
    case TIKU_LAYOUT_E_STEP:    return "off-step";
    case TIKU_LAYOUT_E_STALE:   return "stale";
    case TIKU_LAYOUT_E_BUSY:    return "busy";
    case TIKU_LAYOUT_E_LOSS:    return "would-lose-files";
    case TIKU_LAYOUT_E_IO:      return "io";
    case TIKU_LAYOUT_E_PHASE:   return "no-such-operation";
    case TIKU_LAYOUT_E_REUSED:  return "op-reused";
    case TIKU_LAYOUT_CANCELLED: return "cancelled";
    case TIKU_LAYOUT_E_RECOVERY: return "recovery-required";
    case TIKU_LAYOUT_E_ENTROPY: return "entropy-unavailable";
    default:                    return "unknown";
    }
}

const char *
tiku_layout_store_name(uint8_t store)
{
    switch (store) {
    case TIKU_LAYOUT_STORE_READY:     return "ready";
    case TIKU_LAYOUT_STORE_PROVISION: return "provision";
    case TIKU_LAYOUT_STORE_HELD:      return "held";
    default:                          return "none";
    }
}

const char *
tiku_layout_held_name(uint8_t held)
{
    switch (held) {
    case TIKU_LAYOUT_HELD_TORN:        return "torn";
    case TIKU_LAYOUT_HELD_GEOMETRY:    return "geometry";
    case TIKU_LAYOUT_HELD_VERSION:     return "version";
    case TIKU_LAYOUT_HELD_AMBIGUOUS:   return "ambiguous";
    case TIKU_LAYOUT_HELD_ELSEWHERE:   return "elsewhere";
    case TIKU_LAYOUT_HELD_INTERRUPTED: return "interrupted";
    case TIKU_LAYOUT_HELD_IO:          return "io";
    case TIKU_LAYOUT_HELD_CONTROL:     return "control-missing";
    case TIKU_LAYOUT_HELD_CONTRACT:    return "foreign-contract";
    case TIKU_LAYOUT_HELD_REBOOT:      return "reboot-required";
    default:                           return "-";
    }
}

/*---------------------------------------------------------------------------*/
/* THIS BOARD                                                                */
/*---------------------------------------------------------------------------*/

#if !defined(TIKU_LAYOUT_CORE_ONLY)

#include "tiku_mem.h"
#include "tiku_nvm_region.h"

#if defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_trng_arch.h>
#elif defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_trng_arch.h>
#elif defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_trng_arch.h>
#elif defined(PLATFORM_STM32N6)
#include <arch/stm32n6/tiku_trng_arch.h>
#elif defined(PLATFORM_RA8P1) && TIKU_KIT_CRYPTO_ENABLE
#include <arch/ra8p1/tiku_trng_arch.h>
#endif

static TIKU_DURABLE tiku_layout_record_t layout_rec;
TIKU_PERSIST_CELL(layout_cell, layout_rec, 0x4C415932UL, NULL, 0);
static const tiku_layout_record_t absent_record;

static tiku_layout_env_t   board_env;
static tiku_layout_state_t board_state;
static uint8_t             board_booted;

/** @brief Commit the record through the checked cell write. */
static int
board_commit(void *ctx, const tiku_layout_record_t *r)
{
    int ok;
    (void)ctx;
    ok = (tiku_persist_cell_commit_status(&layout_cell, r,
                                            (uint16_t)sizeof *r)
            == TIKU_MEM_OK);
    board_env.rec = ok ? &layout_rec : &absent_record;
    return ok ? 0 : -1;
}

/** Called only by explicit recovery, once normal board startup is complete. */
static int
board_random(void *ctx, uint8_t *out, size_t len)
{
    (void)ctx;
#if defined(TIKU_TRNG_OK)
    return tiku_trng_arch_read_bytes(out, len) == TIKU_TRNG_OK ? 0 : -1;
#else
    (void)out;
    (void)len;
    return -1;                  /* no clock/time/pseudo-random fallback */
#endif
}

/** @brief Write into the region through the owning backend. */
static int
board_write(void *ctx, size_t off, const void *src, size_t len)
{
    const tiku_nvm_backend_t *rgn = tiku_nvm_backend_get();

    (void)ctx;
    if (rgn == NULL || off > rgn->size || len > rgn->size - off) {
        return -1;
    }
    return (tiku_tier_nvm_write(rgn->base + off, src,
                                (tiku_mem_arch_size_t)len) == TIKU_MEM_OK)
           ? 0 : -1;
}

void
tiku_layout_boot(void)
{
    const tiku_nvm_backend_t *rgn;

    if (board_booted) {
        return;
    }
    board_booted = 1u;
    rgn = tiku_nvm_backend_get();
    memset(&board_env, 0, sizeof board_env);
    board_env.rec          = tiku_persist_cell_valid(&layout_cell)
                           ? &layout_rec : &absent_record;
    board_env.commit       = board_commit;
    board_env.write        = board_write;
    board_env.random       = board_random;
    board_env.default_tier = (uint32_t)TIKU_NVM_TIER_BYTES;
    board_env.step         = (uint32_t)TIKU_TFS_LOCATE_STEP;
    if (rgn != NULL && rgn->base != NULL) {
        board_env.region.base = rgn->base;
        board_env.region.size = rgn->size;
    }
    (void)tiku_layout_boot_env(&board_env, &board_state);
}

const tiku_layout_state_t *
tiku_layout_state(void)
{
    tiku_layout_boot();
    return &board_state;
}

const tiku_layout_record_t *
tiku_layout_record(void)
{
    tiku_layout_boot();
    return board_env.rec;
}

int
tiku_layout_have_record(void)
{
    tiku_layout_boot();
    return rec_valid(board_env.rec);
}

const tiku_layout_env_t *
tiku_layout_env(void)
{
    tiku_layout_boot();
    return &board_env;
}

uint32_t
tiku_layout_base(void)
{
    tiku_layout_boot();
    if (board_state.tier != 0u) {
        return board_state.tier;
    }
    if (record_usable(&board_env, board_env.rec) == TIKU_LAYOUT_OK) {
        return applied_tier(&board_env, board_env.rec);
    }
    return board_env.default_tier;
}

int
tiku_layout_adopt(uint32_t base)
{
    tiku_layout_boot();
    /* mkfs is explicit erase consent, but never resets an existing identity. */
    if (record_usable(&board_env, board_env.rec) != TIKU_LAYOUT_OK) {
        int provisioned = (board_state.store == TIKU_LAYOUT_STORE_PROVISION &&
                           board_state.tier == base);
        int rc = tiku_layout_recover_env(&board_env, &board_state, base);

        if (rc == TIKU_LAYOUT_OK && provisioned) {
            /* The tier was published at this base from boot: nothing waits. */
            board_state.store = TIKU_LAYOUT_STORE_READY;
            board_state.held  = TIKU_LAYOUT_HELD_NONE;
        }
        return rc;
    }
    if (applied_tier(&board_env, board_env.rec) != base) { return -1; }
    if (board_state.tier == 0u) {
        board_state.held = TIKU_LAYOUT_HELD_REBOOT;
        return 0;
    }
    board_state.tier  = base;
    board_state.store = TIKU_LAYOUT_STORE_READY;
    board_state.held  = TIKU_LAYOUT_HELD_NONE;
    return 0;
}

int
tiku_layout_resume(uint32_t op)
{
    int rc;
    tiku_layout_boot();
    rc = tiku_layout_resume_env(&board_env, &board_state, op);
    if (rc == TIKU_LAYOUT_OK) {
        board_state.store = TIKU_LAYOUT_STORE_HELD;
        board_state.held = TIKU_LAYOUT_HELD_REBOOT;
        board_state.tier = 0u;
    }
    return rc;
}

int tiku_layout_recover(uint32_t base)
{
    tiku_layout_boot();
    return tiku_layout_recover_env(&board_env, &board_state, base);
}

#endif /* !TIKU_LAYOUT_CORE_ONLY */

#endif /* !PLATFORM_MSP430 */
