/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_persist_move.c - persist cells carried by key across a layout change.
 *
 * An update that adds or drops a durable variable moves every cell after it.
 * Each image records where it keeps its cells; the first boot of one whose
 * layout differs moves each cell the last image kept, by key (tiku_mem.h).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mem.h"
#include "hal/tiku_cpu.h"
#include <stddef.h>
#include <string.h>

/* Bytes of cell values one move can carry.  The values are copied out before
 * anything is written, because on an in-place part the old image IS the live
 * one; a cell that does not fit re-primes, as every cell did before moves. */
#define MOVE_SNAP_BYTES  1536u

/** @brief FNV-1a over the entry count and the entries in use. */
static uint32_t manifest_sum(const tiku_persist_manifest_t *m)
{
    const uint8_t *b = (const uint8_t *)m->at;
    size_t n = (size_t)m->count * sizeof m->at[0];
    uint32_t h = 2166136261u;
    size_t i;

    h = (h ^ (uint32_t)(m->count & 0xFFu)) * 16777619u;
    h = (h ^ (uint32_t)(m->count >> 8)) * 16777619u;
    for (i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

/** @brief A manifest whose every entry lies inside an image of @p len bytes. */
static int manifest_ok(const tiku_persist_manifest_t *m, size_t len)
{
    uint16_t i;

    if (m->magic != TIKU_PERSIST_MANIFEST_MAGIC ||
        m->count > TIKU_PERSIST_MANIFEST_MAX || m->check != manifest_sum(m)) {
        return 0;
    }
    for (i = 0; i < m->count; i++) {
        if ((size_t)m->at[i].gate + 4u > len ||
            (size_t)m->at[i].data + m->at[i].size > len) {
            return 0;
        }
    }
    return 1;
}

/** @brief This image's manifest: its cells that live inside the window. */
static void manifest_build(const tiku_persist_move_env_t *e,
                           tiku_persist_manifest_t *m)
{
    uintptr_t base = (uintptr_t)e->live;
    size_t i;

    memset(m, 0, sizeof *m);
    m->magic = TIKU_PERSIST_MANIFEST_MAGIC;
    for (i = 0; i < e->n_cells && m->count < TIKU_PERSIST_MANIFEST_MAX; i++) {
        const tiku_persist_cell_t *c = e->cells[i];
        uintptr_t g = (uintptr_t)c->gate;
        uintptr_t d = (uintptr_t)c->data;
        tiku_persist_where_t *w = &m->at[m->count];

        if (g < base || g + 4u > base + e->live_len ||
            d < base || d + c->size > base + e->live_len) {
            continue;                        /* not a durable cell here */
        }
        w->key  = c->key;
        w->gate = (uint16_t)(g - base);
        w->data = (uint16_t)(d - base);
        w->size = c->size;
        m->count++;
    }
    m->check = manifest_sum(m);
}

/**
 * @brief The one manifest the durable image holds, found by its check alone.
 *
 * The manifest sits wherever that image's link order put it.  Two different
 * valid ones say two images' records are both present; neither is trusted,
 * but every place is reported so the move can invalidate them all.
 */
static int manifest_find(const uint8_t *img, size_t len,
                         tiku_persist_manifest_t *out,
                         size_t *offs, int max_offs, int *n_offs)
{
    const size_t hdr = offsetof(tiku_persist_manifest_t, at);
    tiku_persist_manifest_t m;
    uint32_t magic;
    size_t off;
    int found = 0, ambiguous = 0;

    *n_offs = 0;
    if (img == NULL || len < hdr) {
        return 0;
    }
    for (off = 0; off + hdr <= len; off += 4u) {
        size_t n;

        memcpy(&magic, img + off, sizeof magic);
        if (magic != TIKU_PERSIST_MANIFEST_MAGIC) {
            continue;
        }
        memcpy(&m, img + off, hdr);
        if (m.count > TIKU_PERSIST_MANIFEST_MAX) {
            continue;
        }
        n = (size_t)m.count * sizeof m.at[0];
        if (off + hdr + n > len) {
            continue;
        }
        memcpy(m.at, img + off + hdr, n);
        if (!manifest_ok(&m, len)) {
            continue;
        }
        if (*n_offs < max_offs) {
            offs[(*n_offs)++] = off;
        }
        if (found && m.check != out->check) {
            ambiguous = 1;
        }
        if (!found) {
            *out = m;
        }
        found = 1;
    }
    return found && !ambiguous;
}

/* Every store a move makes goes through the NVM HAL, word stores included, so
 * a cut can land between any two of them; a torn gate reads as no key. */
static void move_word(uint8_t *dst, uint32_t v)
{
    tiku_mem_arch_nvm_write(dst, (const uint8_t *)&v, (tiku_mem_arch_size_t)4u);
}

/** @brief The entry for @p key, or NULL when it is absent or not unique. */
static const tiku_persist_where_t *
manifest_key(const tiku_persist_manifest_t *m, uint32_t key)
{
    const tiku_persist_where_t *hit = NULL;
    uint16_t i;

    for (i = 0; i < m->count; i++) {
        if (m->at[i].key == key) {
            if (hit != NULL) {
                return NULL;
            }
            hit = &m->at[i];
        }
    }
    return hit;
}

int tiku_persist_move(const tiku_persist_move_env_t *e)
{
    tiku_persist_manifest_t cur, old;
    uint8_t  snap[MOVE_SNAP_BYTES];
    uint16_t from[TIKU_PERSIST_MANIFEST_MAX];   /* snap offset per entry */
    uint8_t  moving[TIKU_PERSIST_MANIFEST_MAX];
    size_t   offs[4], used = 0;
    int      n_offs = 0, have_old, moved = 0, k;
    uint16_t i, saved;
    uint32_t key;
    tiku_mem_err_t status;

    if (e == NULL || e->live == NULL || e->manifest == NULL) {
        return -1;
    }
    manifest_build(e, &cur);
    if (e->preserve == NULL && manifest_ok(e->manifest, e->live_len) &&
        e->manifest->count == cur.count && e->manifest->check == cur.check &&
        memcmp(e->manifest->at, cur.at,
               (size_t)cur.count * sizeof cur.at[0]) == 0) {
        return 0;                           /* laid out as recorded */
    }

    /* Copy every movable value out before a single byte is written. */
    memset(moving, 0, sizeof moving);
    have_old = manifest_find(e->old, e->old_len, &old, offs, 4, &n_offs);
    for (i = 0; have_old && i < cur.count; i++) {
        const tiku_persist_where_t *o = manifest_key(&old, cur.at[i].key);

        if (o == NULL || o->size != cur.at[i].size ||
            manifest_key(&cur, cur.at[i].key) == NULL ||
            used + o->size > sizeof snap) {
            continue;
        }
        memcpy(&key, e->old + o->gate, sizeof key);
        if (key != cur.at[i].key) {
            continue;                       /* it was never valid there */
        }
        memcpy(snap + used, e->old + o->data, o->size);
        from[i]   = (uint16_t)used;
        moving[i] = 1;
        used     += o->size;
    }

    tiku_atomic_enter();
    saved = tiku_mpu_unlock_nvm();
    /* Old records first: a cut from here on finds none and moves nothing.
     * In place, the old image is the live one; a mirror keeps only what
     * lies inside this image's window. */
    for (k = 0; k < n_offs; k++) {
        size_t lim = (e->old == e->live) ? e->old_len : e->live_len;

        if (offs[k] + 4u <= lim) {
            move_word(e->live + offs[k], 0u);
        }
    }
    /* A missing manifest can be an interrupted move. No unmatched gate is
     * evidence of a valid value, even on a first manifest-aware boot. */
    for (i = 0; i < cur.count; i++) {
        memcpy(&key, e->live + cur.at[i].gate, sizeof key);
        if (!moving[i] && key == cur.at[i].key) {
            move_word(e->live + cur.at[i].gate, 0u);
        }
    }
    if (e->preserve != NULL) {
        e->preserve(e->preserve_ctx);
    }
    for (i = 0; i < cur.count; i++) {
        uint8_t *data = e->live + cur.at[i].data;
        uint8_t *gate = e->live + cur.at[i].gate;

        if (!moving[i]) {
            continue;
        }
        memcpy(&key, gate, sizeof key);
        if (key == cur.at[i].key &&
            memcmp(data, snap + from[i], cur.at[i].size) == 0) {
            continue;                       /* already where it belongs */
        }
        move_word(gate, 0u);
        tiku_mem_arch_nvm_write(data, snap + from[i], cur.at[i].size);
        move_word(gate, cur.at[i].key);     /* commit point, after the value */
        moved++;
    }
    /* The new record last, its magic after everything it vouches for. */
    key = cur.magic;
    cur.magic = 0u;
    tiku_mem_arch_nvm_write((uint8_t *)e->manifest, (const uint8_t *)&cur,
                            (tiku_mem_arch_size_t)sizeof cur);
    move_word((uint8_t *)e->manifest, key);
    status = tiku_mpu_lock_nvm_status(saved);
    tiku_atomic_exit();
    return (status == TIKU_MEM_OK) ? moved : -1;
}

/* What the boot move returned; /sys/persist/moved reads it. */
static int move_result;

int tiku_persist_moved(void)
{
    return move_result;
}

#if defined(TIKU_CELL_TABLE) && TIKU_CELL_TABLE
#include "tiku_layout.h"
/* The table every TIKU_PERSIST_CELL adds itself to (the linker scripts). */
extern const tiku_persist_cell_t *const __tiku_cells_start[];
extern const tiku_persist_cell_t *const __tiku_cells_end[];

/* Where this image keeps its cells; rewritten only when that changes. */
static TIKU_DURABLE tiku_persist_manifest_t cell_manifest;

/** @brief Capture ownership and cells before a shared durable-image rewrite. */
int tiku_persist_move_boot_env(tiku_persist_move_env_t *e)
{
    tiku_layout_record_t record;
    int result;

    if (e == NULL) {
        return -1;
    }
    e->preserve = NULL;
    e->preserve_ctx = NULL;
    if (tiku_layout_capture_record(e->old, e->old_len, &record)) {
        e->preserve = tiku_layout_restore_record;
        e->preserve_ctx = &record;
    }
    result = tiku_persist_move(e);
    if (e->preserve != NULL) {
        tiku_layout_restore_complete(result);
    }
    e->preserve = NULL;
    e->preserve_ctx = NULL;
    return result;
}

void tiku_persist_move_boot(void)
{
    tiku_persist_move_env_t e;

    memset(&e, 0, sizeof e);
    e.cells    = __tiku_cells_start;
    e.n_cells  = (size_t)(__tiku_cells_end - __tiku_cells_start);
    e.live     = tiku_mem_arch_durable_live(&e.live_len);
    e.old      = tiku_mem_arch_durable(&e.old_len);
    e.manifest = &cell_manifest;
    move_result = tiku_persist_move_boot_env(&e);
}
#endif
