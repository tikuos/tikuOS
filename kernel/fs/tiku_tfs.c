/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tfs.c - Tiku File Store implementation.  See tiku_tfs.h for the model.
 *
 * On-NVM layout:
 *   [ superblock | directory[MAX_FILES] | data[NSLOTS] ]
 *   superblock : magic (4) + version (4)
 *   dirent     : gate (4) + slot (4) + name[NAME_MAX]   (one per file)
 *   data slot  : length (4) + content[SLOT_DATA]        (NSLOTS = MAX_FILES+1)
 * Content + its length live together in a data slot; the dirent holds the slot
 * index.  Overwrite writes a fresh shadow slot then flips the dirent's slot
 * index in one aligned word -> a torn overwrite leaves the OLD file.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_tfs.h"

#include <string.h>

/*---------------------------------------------------------------------------*/
/* ON-NVM LAYOUT                                                             */
/*---------------------------------------------------------------------------*/

#define TFS_MAGIC    0x54465331u   /* "TFS1" -- store is formatted */
#define TFS_VERSION  1u
#define TFS_GATE     0x4C495645u   /* "LIVE" -- directory entry is in use */

#define TFS_ALIGN4(n)   (((n) + 3u) & ~3u)

#define TFS_SB_BYTES    8u                                  /* magic + version */
#define TFS_DE_BYTES    TFS_ALIGN4(8u + TIKU_TFS_NAME_MAX)  /* gate+slot+name  */
#define TFS_DIR_OFF     TFS_SB_BYTES
#define TFS_DIR_BYTES   (TFS_DE_BYTES * TIKU_TFS_MAX_FILES)
#define TFS_SLOT_BYTES  TFS_ALIGN4(4u + TIKU_TFS_SLOT_DATA) /* length+content  */
#define TFS_DATA_OFF    (TFS_DIR_OFF + TFS_DIR_BYTES)
#define TFS_REGION      (TFS_DATA_OFF + TFS_SLOT_BYTES * TIKU_TFS_NSLOTS)

/* The public TIKU_TFS_REGION_BYTES (tiku_tfs.h) must match this internal
 * layout -- C89-portable static assertion (the lib also builds under -std=c89). */
typedef char tfs_region_size_check[(TFS_REGION == TIKU_TFS_REGION_BYTES) ? 1 : -1];

/* field offsets within a dirent / a slot */
#define TFS_DE_GATE  0u
#define TFS_DE_SLOT  4u
#define TFS_DE_NAME  8u
#define TFS_SL_LEN   0u
#define TFS_SL_DATA  4u

/*---------------------------------------------------------------------------*/
/* LOW-LEVEL ACCESS                                                          */
/*---------------------------------------------------------------------------*/

static uint32_t rd32(tiku_tfs_t *fs, size_t off)
{
    uint32_t v;
    memcpy(&v, fs->be->base + off, sizeof v);   /* alignment-safe read */
    return v;
}

static int wr(tiku_tfs_t *fs, size_t off, const void *p, size_t n)
{
    return (fs->be->write(fs->be, off, p, n) == 0) ? TFS_OK : TFS_ERR_IO;
}

static int wr32(tiku_tfs_t *fs, size_t off, uint32_t v)
{
    return wr(fs, off, &v, sizeof v);
}

static size_t dirent_off(unsigned i) { return TFS_DIR_OFF + (size_t)i * TFS_DE_BYTES; }
static size_t slot_off(unsigned s)   { return TFS_DATA_OFF + (size_t)s * TFS_SLOT_BYTES; }

static uint32_t de_gate(tiku_tfs_t *fs, unsigned i) { return rd32(fs, dirent_off(i) + TFS_DE_GATE); }
static uint32_t de_slot(tiku_tfs_t *fs, unsigned i) { return rd32(fs, dirent_off(i) + TFS_DE_SLOT); }
static const char *de_name(tiku_tfs_t *fs, unsigned i)
{
    return (const char *)(fs->be->base + dirent_off(i) + TFS_DE_NAME);
}
static uint32_t sl_len(tiku_tfs_t *fs, unsigned s) { return rd32(fs, slot_off(s) + TFS_SL_LEN); }
static const uint8_t *sl_data(tiku_tfs_t *fs, unsigned s)
{
    return fs->be->base + slot_off(s) + TFS_SL_DATA;
}

/* in-RAM data-slot allocation map */
static void bm_set(uint8_t *bm, unsigned i) { bm[i >> 3] |= (uint8_t)(1u << (i & 7u)); }
static void bm_clr(uint8_t *bm, unsigned i) { bm[i >> 3] &= (uint8_t)~(1u << (i & 7u)); }
static int  bm_get(const uint8_t *bm, unsigned i) { return (bm[i >> 3] >> (i & 7u)) & 1u; }

static int tfs_find(tiku_tfs_t *fs, const char *name)
{
    unsigned i;
    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        if (de_gate(fs, i) == TFS_GATE && strcmp(de_name(fs, i), name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int free_dirent(tiku_tfs_t *fs)
{
    unsigned i;
    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        if (de_gate(fs, i) != TFS_GATE) {
            return (int)i;
        }
    }
    return -1;
}

static int free_slot(tiku_tfs_t *fs)
{
    unsigned s;
    for (s = 0; s < TIKU_TFS_NSLOTS; s++) {
        if (!bm_get(fs->slot_used, s)) {
            return (int)s;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

size_t tiku_tfs_region_size(void)
{
    return TFS_REGION;
}

int tiku_tfs_format(tiku_tfs_t *fs)
{
    unsigned i;
    if (fs == NULL || fs->be == NULL || fs->be->base == NULL) {
        return TFS_ERR_INVAL;
    }
    if (fs->be->size < TFS_REGION) {
        return TFS_ERR_NOSPACE;
    }
    /* Free every directory entry BEFORE stamping the superblock, so a valid
     * magic always implies a clean directory (a torn format reads as virgin). */
    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        if (wr32(fs, dirent_off(i) + TFS_DE_GATE, 0u)) {
            return TFS_ERR_IO;
        }
    }
    if (wr32(fs, 4, TFS_VERSION) || wr32(fs, 0, TFS_MAGIC)) {
        return TFS_ERR_IO;
    }
    memset(fs->slot_used, 0, sizeof fs->slot_used);
    fs->mounted = 1;
    return TFS_OK;
}

int tiku_tfs_mount(tiku_tfs_t *fs, tiku_nvm_backend_t *be)
{
    unsigned i;
    if (fs == NULL || be == NULL || be->base == NULL || be->write == NULL) {
        return TFS_ERR_INVAL;
    }
    fs->be = be;
    fs->mounted = 0;
    if (be->size < TFS_REGION) {
        return TFS_ERR_NOSPACE;
    }
    if (rd32(fs, 0) != TFS_MAGIC) {
        return tiku_tfs_format(fs);            /* virgin / wrong-version NVM */
    }
    /* Rebuild the data-slot allocation map from the live directory. */
    memset(fs->slot_used, 0, sizeof fs->slot_used);
    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        if (de_gate(fs, i) == TFS_GATE) {
            uint32_t s = de_slot(fs, i);
            if (s >= TIKU_TFS_NSLOTS || sl_len(fs, (unsigned)s) > TIKU_TFS_SLOT_DATA) {
                return TFS_ERR_CORRUPT;
            }
            bm_set(fs->slot_used, (unsigned)s);
        }
    }
    fs->mounted = 1;
    return TFS_OK;
}

int tiku_tfs_create(tiku_tfs_t *fs, const char *name)
{
    size_t nl;
    int i, s;
    char nb[TIKU_TFS_NAME_MAX];

    if (fs == NULL || !fs->mounted || name == NULL) {
        return TFS_ERR_INVAL;
    }
    nl = strlen(name);
    if (nl == 0 || nl >= TIKU_TFS_NAME_MAX) {
        return TFS_ERR_NAMELEN;
    }
    if (tfs_find(fs, name) >= 0) {
        return TFS_ERR_EXISTS;
    }
    i = free_dirent(fs);
    if (i < 0) {
        return TFS_ERR_NOSPACE;
    }
    s = free_slot(fs);
    if (s < 0) {
        return TFS_ERR_NOSPACE;
    }
    if (wr32(fs, slot_off((unsigned)s) + TFS_SL_LEN, 0u)) {       /* empty slot */
        return TFS_ERR_IO;
    }
    memset(nb, 0, sizeof nb);
    memcpy(nb, name, nl);
    /* name + slot, then GATE last (the commit point). */
    if (wr(fs, dirent_off((unsigned)i) + TFS_DE_NAME, nb, TIKU_TFS_NAME_MAX) ||
        wr32(fs, dirent_off((unsigned)i) + TFS_DE_SLOT, (uint32_t)s) ||
        wr32(fs, dirent_off((unsigned)i) + TFS_DE_GATE, TFS_GATE)) {
        return TFS_ERR_IO;
    }
    bm_set(fs->slot_used, (unsigned)s);
    return TFS_OK;
}

int tiku_tfs_write(tiku_tfs_t *fs, const char *name, const void *data, size_t len)
{
    int i, ns;
    uint32_t old;

    if (fs == NULL || !fs->mounted || name == NULL || (len && data == NULL)) {
        return TFS_ERR_INVAL;
    }
    if (len > TIKU_TFS_SLOT_DATA) {
        return TFS_ERR_TOOBIG;
    }
    if (strlen(name) == 0 || strlen(name) >= TIKU_TFS_NAME_MAX) {
        return TFS_ERR_NAMELEN;
    }
    i = tfs_find(fs, name);
    if (i < 0) {
        int rc = tiku_tfs_create(fs, name);    /* create-on-write */
        if (rc != TFS_OK) {
            return rc;
        }
        i = tfs_find(fs, name);
        if (i < 0) {
            return TFS_ERR_CORRUPT;
        }
    }
    ns = free_slot(fs);
    if (ns < 0) {
        return TFS_ERR_NOSPACE;
    }
    /* Stage content + length in a fresh (shadow) slot. */
    if ((len && wr(fs, slot_off((unsigned)ns) + TFS_SL_DATA, data, len)) ||
        wr32(fs, slot_off((unsigned)ns) + TFS_SL_LEN, (uint32_t)len)) {
        return TFS_ERR_IO;
    }
    /* Atomic flip: one aligned word repoints the dirent at the new slot. A
     * power cut before this leaves the dirent on the OLD slot (old content). */
    old = de_slot(fs, (unsigned)i);
    bm_set(fs->slot_used, (unsigned)ns);
    if (wr32(fs, dirent_off((unsigned)i) + TFS_DE_SLOT, (uint32_t)ns)) {
        bm_clr(fs->slot_used, (unsigned)ns);   /* flip failed: shadow is free */
        return TFS_ERR_IO;
    }
    if (old < TIKU_TFS_NSLOTS) {
        bm_clr(fs->slot_used, (unsigned)old);  /* reclaim the old slot */
    }
    return TFS_OK;
}

int tiku_tfs_read(tiku_tfs_t *fs, const char *name, void *buf, size_t max, size_t *out_len)
{
    int i;
    uint32_t s, len;
    size_t n;

    if (fs == NULL || !fs->mounted || name == NULL || (max && buf == NULL)) {
        return TFS_ERR_INVAL;
    }
    i = tfs_find(fs, name);
    if (i < 0) {
        return TFS_ERR_NOTFOUND;
    }
    s = de_slot(fs, (unsigned)i);
    if (s >= TIKU_TFS_NSLOTS) {
        return TFS_ERR_CORRUPT;
    }
    len = sl_len(fs, (unsigned)s);
    if (len > TIKU_TFS_SLOT_DATA) {
        return TFS_ERR_CORRUPT;
    }
    n = (len < max) ? len : max;
    if (n) {
        memcpy(buf, sl_data(fs, (unsigned)s), n);
    }
    if (out_len) {
        *out_len = len;
    }
    return TFS_OK;
}

int tiku_tfs_map(tiku_tfs_t *fs, const char *name, const void **p, size_t *len)
{
    int i;
    uint32_t s;

    if (fs == NULL || !fs->mounted || name == NULL || p == NULL || len == NULL) {
        return TFS_ERR_INVAL;
    }
    i = tfs_find(fs, name);
    if (i < 0) {
        return TFS_ERR_NOTFOUND;
    }
    s = de_slot(fs, (unsigned)i);
    if (s >= TIKU_TFS_NSLOTS) {
        return TFS_ERR_CORRUPT;
    }
    *p = sl_data(fs, (unsigned)s);             /* points into the NVM region */
    *len = sl_len(fs, (unsigned)s);
    return TFS_OK;
}

int tiku_tfs_delete(tiku_tfs_t *fs, const char *name)
{
    int i;
    uint32_t s;

    if (fs == NULL || !fs->mounted || name == NULL) {
        return TFS_ERR_INVAL;
    }
    i = tfs_find(fs, name);
    if (i < 0) {
        return TFS_ERR_NOTFOUND;
    }
    s = de_slot(fs, (unsigned)i);
    if (wr32(fs, dirent_off((unsigned)i) + TFS_DE_GATE, 0u)) {   /* commit */
        return TFS_ERR_IO;
    }
    if (s < TIKU_TFS_NSLOTS) {
        bm_clr(fs->slot_used, (unsigned)s);
    }
    return TFS_OK;
}

int tiku_tfs_stat(tiku_tfs_t *fs, const char *name, size_t *len)
{
    int i;
    if (fs == NULL || !fs->mounted || name == NULL || len == NULL) {
        return TFS_ERR_INVAL;
    }
    i = tfs_find(fs, name);
    if (i < 0) {
        return TFS_ERR_NOTFOUND;
    }
    *len = sl_len(fs, de_slot(fs, (unsigned)i));
    return TFS_OK;
}

int tiku_tfs_list(tiku_tfs_t *fs, tiku_tfs_iter_cb cb, void *ctx)
{
    unsigned i;
    int n = 0;
    if (fs == NULL || !fs->mounted) {
        return TFS_ERR_INVAL;
    }
    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        if (de_gate(fs, i) == TFS_GATE) {
            if (cb) {
                cb(de_name(fs, i), sl_len(fs, de_slot(fs, i)), ctx);
            }
            n++;
        }
    }
    return n;
}

/* List the IMMEDIATE children under @p prefix, presenting the flat store as a
 * tree (path-as-name): a file directly in the directory is reported by its leaf
 * name; a deeper path contributes its first segment ONCE, with a trailing '/'
 * so the caller can tell folders from files.  prefix is "" for the store root
 * or "logs/" for a sub-folder; the empty marker entry "<dir>/" (mkdir) is
 * skipped here but still surfaces the folder one level up. */
int tiku_tfs_list_dir(tiku_tfs_t *fs, const char *prefix,
                      tiku_tfs_iter_cb cb, void *ctx)
{
    unsigned i, j;
    size_t   plen;
    int      n = 0;

    if (fs == NULL || !fs->mounted || prefix == NULL) {
        return TFS_ERR_INVAL;
    }
    plen = strlen(prefix);

    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        const char *name, *rest, *slash;
        if (de_gate(fs, i) != TFS_GATE) {
            continue;
        }
        name = de_name(fs, i);
        if (strncmp(name, prefix, plen) != 0) {
            continue;                          /* not under this directory */
        }
        rest = name + plen;
        if (*rest == '\0') {
            continue;                          /* the directory's own marker */
        }
        slash = strchr(rest, '/');
        if (slash == NULL) {                   /* a file in this directory */
            if (cb) {
                cb(rest, sl_len(fs, de_slot(fs, i)), ctx);
            }
            n++;
        } else {                               /* a sub-folder: first segment */
            size_t seglen = (size_t)(slash - rest) + 1;   /* include the '/' */
            int    dup = 0;
            for (j = 0; j < i; j++) {           /* emit once: dedup vs earlier */
                const char *nm2;
                if (de_gate(fs, j) != TFS_GATE) {
                    continue;
                }
                nm2 = de_name(fs, j);
                if (strncmp(nm2, prefix, plen) == 0 &&
                    strncmp(nm2 + plen, rest, seglen) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (!dup && cb) {
                char fbuf[TIKU_TFS_NAME_MAX + 1];
                if (seglen < sizeof fbuf) {
                    memcpy(fbuf, rest, seglen);          /* "<segment>/" */
                    fbuf[seglen] = '\0';
                    cb(fbuf, 0, ctx);
                    n++;
                }
            }
        }
    }
    return n;
}

size_t tiku_tfs_free_files(tiku_tfs_t *fs)
{
    unsigned i;
    size_t f = 0;
    if (fs == NULL || !fs->mounted) {
        return 0;
    }
    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        if (de_gate(fs, i) != TFS_GATE) {
            f++;
        }
    }
    return f;
}

/*===========================================================================*/
/* HOST UNIT TEST  (clang -DTFS_TEST tiku_tfs.c -o tfs_test && ./tfs_test)    */
/*===========================================================================*/

#ifdef TFS_TEST
#include <stdio.h>

/* RAM backend with optional power-cut injection: when fail_after reaches 0 the
 * next write is dropped and returns -1, simulating a power loss at that write. */
typedef struct { uint8_t *buf; size_t size; long fail_after; } ram_ctx_t;

static int ram_write(tiku_nvm_backend_t *be, size_t off, const void *src, size_t len)
{
    ram_ctx_t *c = (ram_ctx_t *)be->ctx;
    if (off + len > c->size) {
        return -1;
    }
    if (c->fail_after == 0) {
        return -1;                              /* power cut: this write is lost */
    }
    if (c->fail_after > 0) {
        c->fail_after--;
    }
    memcpy(c->buf + off, src, len);
    return 0;
}

static int g_fails;
static int g_listn;
static void count_cb(const char *name, size_t len, void *ctx)
{
    (void)name; (void)len; (void)ctx;
    g_listn++;
}

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); g_fails++; }      \
        else         { printf("  ok  : %s\n", (msg)); }                \
    } while (0)

int main(void)
{
    static uint8_t buf[TFS_REGION + 16];
    ram_ctx_t ctx = { buf, sizeof buf, -1 };
    tiku_nvm_backend_t be = { buf, sizeof buf, ram_write, NULL, &ctx };
    tiku_tfs_t fs, fs2;
    char rb[1024];
    size_t n;
    const void *mp;
    size_t ml;
    char nm[16];
    int k, created;
    int rc;
    char longname[TIKU_TFS_NAME_MAX + 4];

    printf("TFS region = %zu bytes  (MAX_FILES=%u, NAME_MAX=%u, SLOT_DATA=%u)\n\n",
           tiku_tfs_region_size(), (unsigned)TIKU_TFS_MAX_FILES,
           (unsigned)TIKU_TFS_NAME_MAX, (unsigned)TIKU_TFS_SLOT_DATA);

    /* --- format on virgin NVM --- */
    memset(buf, 0, sizeof buf);
    CHECK(tiku_tfs_mount(&fs, &be) == TFS_OK, "mount virgin region (formats)");
    CHECK(tiku_tfs_free_files(&fs) == TIKU_TFS_MAX_FILES, "all dir slots free");

    /* --- create + write + read --- */
    CHECK(tiku_tfs_write(&fs, "cfg.txt", "mode=eco\n", 9) == TFS_OK, "write cfg.txt");
    CHECK(tiku_tfs_read(&fs, "cfg.txt", rb, sizeof rb, &n) == TFS_OK &&
          n == 9 && memcmp(rb, "mode=eco\n", 9) == 0, "read cfg.txt back");

    CHECK(tiku_tfs_write(&fs, "blink.bas", "10 LED 0,1", 10) == TFS_OK, "write blink.bas");
    g_listn = 0; tiku_tfs_list(&fs, count_cb, NULL);
    CHECK(g_listn == 2, "list shows 2 files");

    /* --- atomic overwrite (longer content) --- */
    CHECK(tiku_tfs_write(&fs, "cfg.txt", "mode=turbo;x=1\n", 15) == TFS_OK, "overwrite cfg.txt");
    CHECK(tiku_tfs_read(&fs, "cfg.txt", rb, sizeof rb, &n) == TFS_OK &&
          n == 15 && memcmp(rb, "mode=turbo;x=1\n", 15) == 0, "read overwritten cfg.txt");

    /* --- zero-copy map points into the region --- */
    CHECK(tiku_tfs_map(&fs, "blink.bas", &mp, &ml) == TFS_OK && ml == 10 &&
          memcmp(mp, "10 LED 0,1", 10) == 0 &&
          (const uint8_t *)mp >= buf && (const uint8_t *)mp < buf + sizeof buf,
          "map blink.bas (zero-copy, into region)");

    /* --- delete --- */
    CHECK(tiku_tfs_delete(&fs, "cfg.txt") == TFS_OK, "delete cfg.txt");
    CHECK(tiku_tfs_read(&fs, "cfg.txt", rb, sizeof rb, &n) == TFS_ERR_NOTFOUND, "cfg.txt is gone");
    g_listn = 0; tiku_tfs_list(&fs, count_cb, NULL);
    CHECK(g_listn == 1, "list shows 1 file");

    /* --- error paths --- */
    CHECK(tiku_tfs_create(&fs, "blink.bas") == TFS_ERR_EXISTS, "create existing -> EXISTS");
    CHECK(tiku_tfs_write(&fs, "big", rb, TIKU_TFS_SLOT_DATA + 1) == TFS_ERR_TOOBIG, "oversize -> TOOBIG");
    memset(longname, 'a', sizeof longname - 1); longname[sizeof longname - 1] = 0;
    CHECK(tiku_tfs_write(&fs, longname, "x", 1) == TFS_ERR_NAMELEN, "long name -> NAMELEN");

    /* --- fill the store --- */
    created = 0;
    for (k = 0; k < TIKU_TFS_MAX_FILES + 2; k++) {
        snprintf(nm, sizeof nm, "f%d", k);
        if (tiku_tfs_write(&fs, nm, "y", 1) == TFS_OK) created++;
    }
    CHECK(tiku_tfs_free_files(&fs) == 0, "store reports full");
    printf("  (created %d files before full)\n", created);

    /* --- PERSISTENCE: re-mount the same backing buffer --- */
    CHECK(tiku_tfs_mount(&fs2, &be) == TFS_OK, "re-mount existing store");
    CHECK(tiku_tfs_read(&fs2, "blink.bas", rb, sizeof rb, &n) == TFS_OK &&
          n == 10 && memcmp(rb, "10 LED 0,1", 10) == 0,
          "blink.bas SURVIVED re-mount (durable)");

    /* --- ATOMICITY: a torn overwrite must leave the OLD file intact --- */
    memset(buf, 0, sizeof buf); ctx.fail_after = -1;
    tiku_tfs_mount(&fs, &be);
    tiku_tfs_write(&fs, "a", "OLD", 3);
    ctx.fail_after = 2;                          /* allow content+length, fail the flip */
    rc = tiku_tfs_write(&fs, "a", "NEWNEWNEW", 9);
    ctx.fail_after = -1;
    CHECK(rc == TFS_ERR_IO, "torn overwrite returns IO");
    CHECK(tiku_tfs_mount(&fs2, &be) == TFS_OK, "re-mount after torn write");
    CHECK(tiku_tfs_read(&fs2, "a", rb, sizeof rb, &n) == TFS_OK &&
          n == 3 && memcmp(rb, "OLD", 3) == 0,
          "torn overwrite left OLD intact (power-cut safe)");

    printf("\n%s  (%d failure%s)\n", g_fails ? "*** FAILURES ***" : "ALL PASS",
           g_fails, g_fails == 1 ? "" : "s");
    return g_fails ? 1 : 0;
}
#endif /* TFS_TEST */
