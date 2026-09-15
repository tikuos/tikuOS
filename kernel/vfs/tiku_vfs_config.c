/* TikuOS -- bounded two-bank configuration journal.
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_vfs_config.h"
#include <string.h>

/* Wire layout v1, little endian: header[32], resources[2][48], history[4][64],
 * reserved[12], CRC32[4]. Gate is written last, outside the CRC. Revision zero
 * means unenrolled value (use the existing setting). No counters wrap. */
#define CFG_MAGIC 0x31474643UL
#define CFG_CRC 396u
#define CFG_RESOURCE(i) (32u + 48u * (i))
#define CFG_RECORD(i) (128u + 64u * (i))

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t v)
{
    unsigned i;
    for (i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8u * i));
}
uint32_t tiku_cfg_crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffUL;
    unsigned k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++) c = (c >> 1) ^
            ((0u - (c & 1u)) & 0xedb88320UL);
    }
    return ~c;
}
static int uniform(const uint8_t *p, size_t n, uint8_t v)
{
    while (n--) if (*p++ != v) return 0;
    return 1;
}
static int valid(const uint8_t *p)
{
    return get32(p) == CFG_MAGIC && get32(p + 8) != 0 &&
           get32(p + CFG_CRC) == tiku_cfg_crc32(p + 4, CFG_CRC - 4);
}
static int resource_index(const tiku_cfg_t *c, uint32_t id)
{
    unsigned i;
    for (i = 0; i < c->count; i++) if (c->resources[i].id == id) return (int)i;
    return -1;
}
static int permitted(const tiku_cfg_t *c, unsigned i, uint8_t cap)
{
    return (c->resources[i].required_cap & (uint8_t)~cap) == 0;
}

/* Preserve the active bank through all three ordered writes. On ANY I/O
 * failure the instance is poisoned until re-open: the last gate may already
 * have reached NVM even when its acknowledgement/readback failed. */
static int commit(tiku_cfg_t *c, uint8_t *next)
{
    static const uint8_t zero[4] = {0};
    uint8_t check[32];
    unsigned bank = c->active ^ 1u;
    size_t off;
    uint32_t generation = get32(c->image + 8);
    if (generation == UINT32_MAX) return TIKU_CFG_EXHAUSTED;
    put32(next, CFG_MAGIC);
    put32(next + 8, generation + 1u);
    put32(next + CFG_CRC, tiku_cfg_crc32(next + 4, CFG_CRC - 4));
    if (c->io.write(c->io.ctx, bank, 0, zero, 4) ||
        c->io.write(c->io.ctx, bank, 4, next + 4, TIKU_CFG_BANK_BYTES - 4) ||
        c->io.write(c->io.ctx, bank, 0, next, 4)) goto fail;
    for (off = 0; off < TIKU_CFG_BANK_BYTES; off += sizeof check) {
        size_t n = TIKU_CFG_BANK_BYTES - off;
        if (n > sizeof check) n = sizeof check;
        if (c->io.read(c->io.ctx, bank, off, check, n) ||
            memcmp(check, next + off, n)) goto fail;
    }
    memcpy(c->image, next, sizeof c->image);
    c->active = (uint8_t)bank;
    c->status = TIKU_CFG_OK;
    return 0;
fail:
    c->status = TIKU_CFG_IO;
    return TIKU_CFG_IO;
}

int tiku_cfg_open(tiku_cfg_t *c, const tiku_cfg_io_t *io,
                  const tiku_cfg_resource_t *resources, unsigned count)
{
    uint8_t other[TIKU_CFG_BANK_BYTES];
    unsigned i, j;
    int a, b;
    if (!c || !io || !io->read || !io->write || !resources || !count ||
        count > TIKU_CFG_RESOURCES) return TIKU_CFG_INVALID;
    memset(c, 0, sizeof *c);
    c->status = TIKU_CFG_INVALID;
    for (i = 0; i < count; i++) {
        if (!resources[i].id || !resources[i].schema ||
            !resources[i].normalize || !resources[i].reconcile) return c->status;
        for (j = 0; j < i; j++) if (resources[i].id == resources[j].id)
            return c->status;
    }
    c->io = *io; c->resources = resources; c->count = count;
    c->status = TIKU_CFG_IO;
    if (io->read(io->ctx, 0, 0, c->image, sizeof c->image) ||
        io->read(io->ctx, 1, 0, other, sizeof other)) return c->status;
    a = valid(c->image); b = valid(other);
    if (!a && !b) {
        c->status = ((uniform(c->image, sizeof c->image, 0) ||
                      uniform(c->image, sizeof c->image, 255)) &&
                     (uniform(other, sizeof other, 0) ||
                      uniform(other, sizeof other, 255)))
            ? TIKU_CFG_UNINITIALIZED : TIKU_CFG_CORRUPT;
        memset(c->image, 0, sizeof c->image);
        return c->status;
    }
    if (a && b && (memcmp(c->image + 12, other + 12, TIKU_CFG_TOKEN) ||
        (get32(c->image + 8) == get32(other + 8) &&
         memcmp(c->image, other, sizeof other)))) {
        c->status = TIKU_CFG_CORRUPT;
        return c->status;
    }
    if (b && (!a || get32(other + 8) > get32(c->image + 8))) {
        memcpy(c->image, other, sizeof other); c->active = 1;
    }
    c->status = TIKU_CFG_INCOMPATIBLE;
    if (get32(c->image + 4) != 1 || get32(c->image + 28) != count)
        return c->status;
    for (i = 0; i < count; i++) {
        const uint8_t *p = c->image + CFG_RESOURCE(i);
        if (get32(p) != resources[i].id || get32(p + 4) != resources[i].schema)
            return c->status;
        if (p[44] > TIKU_CFG_VALUE || p[45] > TIKU_CFG_BLOCKED ||
            (!get32(p + 8)) != (!p[44]) || (!p[44]) != (!p[45])) {
            c->status = TIKU_CFG_CORRUPT; return c->status;
        }
    }
    for (i = 0; i < TIKU_CFG_HISTORY; i++) {
        const uint8_t *p = c->image + CFG_RECORD(i);
        if (p[24] > TIKU_CFG_VALUE || p[28] > TIKU_CFG_BLOCKED) {
            c->status = TIKU_CFG_CORRUPT; return c->status;
        }
    }
    c->status = TIKU_CFG_OK;
    return 0;
}

int tiku_cfg_provision(tiku_cfg_t *c, const uint8_t incarnation[TIKU_CFG_TOKEN],
                       uint8_t caller_cap)
{
    uint8_t next[TIKU_CFG_BANK_BYTES];
    unsigned i;
    int rc;
    if (!c || !incarnation || uniform(incarnation, TIKU_CFG_TOKEN, 0))
        return TIKU_CFG_INVALID;
    for (i = 0; i < c->count; i++) if (!permitted(c, i, caller_cap))
        return TIKU_CFG_DENIED;
    if (c->busy) return TIKU_CFG_BUSY;
    if (c->status == 0) return memcmp(c->image + 12, incarnation, TIKU_CFG_TOKEN)
        ? TIKU_CFG_CONFLICT : 0;
    if (c->status != TIKU_CFG_UNINITIALIZED) return c->status;
    memset(next, 0, sizeof next);
    put32(next + 4, 1); put32(next + 28, c->count);
    memcpy(next + 12, incarnation, TIKU_CFG_TOKEN);
    for (i = 0; i < c->count; i++) {
        put32(next + CFG_RESOURCE(i), c->resources[i].id);
        put32(next + CFG_RESOURCE(i) + 4, c->resources[i].schema);
    }
    c->busy = 1; rc = commit(c, next); c->busy = 0;
    return rc;
}

static int request_check(tiku_cfg_t *c, const tiku_cfg_request_t *q, uint8_t cap,
                         char value[TIKU_CFG_VALUE], int *length)
{
    int i;
    if (!c || !q || !q->value || !q->length || q->length > TIKU_CFG_VALUE ||
        uniform(q->token, TIKU_CFG_TOKEN, 0)) return TIKU_CFG_INVALID;
    if (c->status) return c->status;
    i = resource_index(c, q->resource);
    if (i < 0) return TIKU_CFG_INVALID;
    if (!permitted(c, (unsigned)i, cap)) return TIKU_CFG_DENIED;
    if (memcmp(c->image + 12, q->incarnation, TIKU_CFG_TOKEN)) return TIKU_CFG_STALE;
    *length = c->resources[i].normalize(q->value, q->length, value);
    if (*length <= 0 || *length > (int)TIKU_CFG_VALUE) return TIKU_CFG_INVALID;
    return i;
}

/* expected_revision is PART of the operation identity. A client must freeze
 * the entire request across retries; changing it creates a new operation.
 * Thus an evicted request cannot execute: its revision is necessarily older. */
static int lookup(tiku_cfg_t *c, const tiku_cfg_request_t *q,
                   const char *value, int length, tiku_cfg_receipt_t *out)
{
    unsigned i;
    for (i = 0; i < TIKU_CFG_HISTORY; i++) {
        const uint8_t *p = c->image + CFG_RECORD(i);
        if (get32(p) != q->resource || get32(p + 4) == 0 ||
            get32(p + 4) - 1u != q->expected_revision) continue;
        if (memcmp(p + 8, q->token, TIKU_CFG_TOKEN) ||
            p[24] != length || memcmp(p + 32, value, (size_t)length))
            return TIKU_CFG_CONFLICT;
        out->revision = get32(p + 4); out->state = p[28]; out->duplicate = 1;
        return 0;
    }
    return TIKU_CFG_STALE;
}

int tiku_cfg_lookup(tiku_cfg_t *c, const tiku_cfg_request_t *q, uint8_t cap,
                    tiku_cfg_receipt_t *out)
{
    char value[TIKU_CFG_VALUE];
    int i, length;
    if (!out) return TIKU_CFG_INVALID;
    memset(out, 0, sizeof *out);
    if (c && c->busy) return TIKU_CFG_BUSY;
    i = request_check(c, q, cap, value, &length);
    return i < 0 ? i : lookup(c, q, value, length, out);
}

static int reconcile(tiku_cfg_t *c, unsigned i, uint8_t *next)
{
    const uint8_t *r = c->image + CFG_RESOURCE(i);
    char normalized[TIKU_CFG_VALUE];
    unsigned j;
    int state, n;
    if (!r[44]) return 0;
    n = c->resources[i].normalize((const char *)r + 12, r[44], normalized);
    state = (n == r[44] && !memcmp(normalized, r + 12, (size_t)n))
        ? c->resources[i].reconcile((const char *)r + 12, r[44]) : TIKU_CFG_BLOCKED;
    if (state != TIKU_CFG_APPLIED && state != TIKU_CFG_RESTART)
        state = TIKU_CFG_BLOCKED;
    /* Live setting state is persisted with the current desired revision. */
    next[CFG_RESOURCE(i) + 45] = (uint8_t)state;
    for (j = 0; j < TIKU_CFG_HISTORY; j++) {
        uint8_t *h = next + CFG_RECORD(j);
        if (get32(h) == get32(r) && get32(h + 4) == get32(r + 8))
            h[28] = (uint8_t)state;
    }
    return 0;
}

int tiku_cfg_submit(tiku_cfg_t *c, const tiku_cfg_request_t *q, uint8_t cap,
                    tiku_cfg_receipt_t *out)
{
    uint8_t next[TIKU_CFG_BANK_BYTES], *r, *h;
    char value[TIKU_CFG_VALUE];
    uint32_t revision;
    int i, n, rc;
    if (!out) return TIKU_CFG_INVALID;
    memset(out, 0, sizeof *out);
    if (c && c->busy) return TIKU_CFG_BUSY;
    i = request_check(c, q, cap, value, &n);
    if (i < 0) return i;
    rc = lookup(c, q, value, n, out);
    if (rc == 0 || rc == TIKU_CFG_CONFLICT) return rc;
    revision = get32(c->image + CFG_RESOURCE((unsigned)i) + 8);
    if (revision != q->expected_revision) return TIKU_CFG_CONFLICT;
    /* Reserve generation headroom for both intent and outcome. */
    if (revision == UINT32_MAX || get32(c->image + 8) >= UINT32_MAX - 1u)
        return TIKU_CFG_EXHAUSTED;
    memcpy(next, c->image, sizeof next);
    r = next + CFG_RESOURCE((unsigned)i);
    put32(r + 8, revision + 1u); memset(r + 12, 0, TIKU_CFG_VALUE);
    memcpy(r + 12, value, (size_t)n); r[44] = (uint8_t)n;
    r[45] = TIKU_CFG_ACCEPTED;
    memmove(next + CFG_RECORD(1), next + CFG_RECORD(0),
            (TIKU_CFG_HISTORY - 1u) * 64u);
    h = next + CFG_RECORD(0); memset(h, 0, 64);
    put32(h, q->resource); put32(h + 4, revision + 1u);
    memcpy(h + 8, q->token, TIKU_CFG_TOKEN);
    h[24] = (uint8_t)n; h[28] = TIKU_CFG_ACCEPTED;
    memcpy(h + 32, value, (size_t)n);
    c->busy = 1;
    rc = commit(c, next);
    if (rc == 0) {
        reconcile(c, (unsigned)i, next);
        rc = commit(c, next);
    }
    c->busy = 0;
    if (rc == 0) {
        out->revision = revision + 1u;
        out->state = c->image[CFG_RESOURCE((unsigned)i) + 45];
    }
    return rc;
}

int tiku_cfg_get(tiku_cfg_t *c, uint32_t id, char value[TIKU_CFG_VALUE],
                 size_t *length, tiku_cfg_receipt_t *out)
{
    const uint8_t *r;
    int i;
    if (!c || !value || !length || !out) return TIKU_CFG_INVALID;
    *length = 0; memset(out, 0, sizeof *out); memset(value, 0, TIKU_CFG_VALUE);
    if (c->busy) return TIKU_CFG_BUSY;
    if (c->status) return c->status;
    i = resource_index(c, id);
    if (i < 0) return TIKU_CFG_INVALID;
    r = c->image + CFG_RESOURCE((unsigned)i);
    *length = r[44]; memcpy(value, r + 12, *length);
    out->revision = get32(r + 8); out->state = r[45];
    return 0;
}

int tiku_cfg_recover(tiku_cfg_t *c)
{
    uint8_t next[TIKU_CFG_BANK_BYTES];
    unsigned i;
    int rc = 0;
    if (!c) return TIKU_CFG_INVALID;
    if (c->status) return c->status;
    if (c->busy) return TIKU_CFG_BUSY;
    if (get32(c->image + 8) == UINT32_MAX) return TIKU_CFG_EXHAUSTED;
    memcpy(next, c->image, sizeof next);
    c->busy = 1;
    for (i = 0; i < c->count; i++) reconcile(c, i, next);
    if (memcmp(next, c->image, sizeof next)) rc = commit(c, next);
    c->busy = 0;
    return rc;
}

const uint8_t *tiku_cfg_incarnation(const tiku_cfg_t *c)
{
    return c && c->status == 0 ? c->image + 12 : NULL;
}
int tiku_cfg_history(tiku_cfg_t *c, unsigned index, tiku_cfg_request_t *q,
                     char value[TIKU_CFG_VALUE], tiku_cfg_receipt_t *out)
{
    const uint8_t *h;
    if (!c || !q || !value || !out || index >= TIKU_CFG_HISTORY)
        return TIKU_CFG_INVALID;
    if (c->status) return c->status;
    if (c->busy) return TIKU_CFG_BUSY;
    memset(q, 0, sizeof *q); memset(out, 0, sizeof *out);
    h = c->image + CFG_RECORD(index);
    if (!get32(h)) return TIKU_CFG_STALE;
    memcpy(q->incarnation, c->image + 12, TIKU_CFG_TOKEN);
    memcpy(q->token, h + 8, TIKU_CFG_TOKEN);
    q->resource = get32(h); q->expected_revision = get32(h + 4) - 1u;
    q->length = h[24]; memcpy(value, h + 32, q->length); q->value = value;
    out->revision = get32(h + 4); out->state = h[28];
    return 0;
}
const char *tiku_cfg_state_name(unsigned state)
{
    switch (state) {
    case TIKU_CFG_UNSET: return "unset";
    case TIKU_CFG_ACCEPTED: return "accepted";
    case TIKU_CFG_APPLIED: return "applied";
    case TIKU_CFG_RESTART: return "restart";
    default: return "blocked";
    }
}
