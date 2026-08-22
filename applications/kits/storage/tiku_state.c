/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_state.c - the three state stores behind one interface.
 *
 * xattr writes through to the filesystem immediately (the object owns its
 * state); sidecar and memory hold records in a list and the sidecar serialises
 * them on flush.  The sidecar file is a simple length-prefixed record stream:
 * values are binary blobs, not text.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_state.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/xattr.h>
#endif

#include <stdint.h>

#define STATE_NODE_MAX 256
#define STATE_ATTR_MAX 32
#define STATE_VAL_MAX  4096
#define SIDECAR_MAGIC  "TRKSTATE1"

enum store_kind { STORE_XATTR, STORE_SIDECAR, STORE_MEMORY };

typedef struct state_rec {
    char              node[STATE_NODE_MAX];
    char              attr[STATE_ATTR_MAX];
    unsigned char    *val;
    size_t            len;
    struct state_rec *next;
} state_rec_t;

struct tiku_store {
    enum store_kind kind;
    state_rec_t    *recs;
    char            path[512];      /* sidecar file */
    int             dirty;
    int             writable;
};

/*---------------------------------------------------------------------------*/
/* record list (sidecar + memory)                                            */
/*---------------------------------------------------------------------------*/

static state_rec_t *
rec_find(tiku_store_t *s, const char *node, const char *attr)
{
    state_rec_t *r;

    for (r = s->recs; r != NULL; r = r->next) {
        if (strcmp(r->node, node) == 0 && strcmp(r->attr, attr) == 0) {
            return r;
        }
    }
    return NULL;
}

static int
rec_set(tiku_store_t *s, const char *node, const char *attr,
        const void *buf, size_t len)
{
    state_rec_t *r = rec_find(s, node, attr);
    unsigned char *copy;

    if (len > STATE_VAL_MAX) {
        return -1;
    }
    copy = malloc(len ? len : 1u);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, buf, len);
    if (r == NULL) {
        r = calloc(1, sizeof *r);
        if (r == NULL) {
            free(copy);
            return -1;
        }
        snprintf(r->node, sizeof r->node, "%s", node);
        snprintf(r->attr, sizeof r->attr, "%s", attr);
        r->next = s->recs;
        s->recs = r;
    } else {
        free(r->val);
    }
    r->val = copy;
    r->len = len;
    s->dirty = 1;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* sidecar file                                                              */
/*---------------------------------------------------------------------------*/

/** @brief $XDG_DATA_HOME/tracker/devices, created if needed. */
static int
sidecar_dir(char *out, size_t max)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char base[400];

    if (xdg != NULL && xdg[0] != '\0') {
        snprintf(base, sizeof base, "%s", xdg);
    } else if (home != NULL && home[0] != '\0') {
        snprintf(base, sizeof base, "%s/.local/share", home);
    } else {
        return -1;
    }
    snprintf(out, max, "%s/tracker/devices", base);
    {
        char part[512];
        char *p;
        snprintf(part, sizeof part, "%s", out);
        for (p = part + 1; *p != '\0'; p++) {
            if (*p == '/') {
                *p = '\0';
                (void)mkdir(part, 0700);
                *p = '/';
            }
        }
        (void)mkdir(part, 0700);
    }
    return 0;
}

static int
sidecar_load(tiku_store_t *s)
{
    FILE *f = fopen(s->path, "rb");
    char magic[16];

    if (f == NULL) {
        return 0;                      /* first sight of this device */
    }
    if (fread(magic, 1, strlen(SIDECAR_MAGIC), f) != strlen(SIDECAR_MAGIC) ||
        memcmp(magic, SIDECAR_MAGIC, strlen(SIDECAR_MAGIC)) != 0) {
        (void)fclose(f);
        return -1;
    }
    for (;;) {
        uint32_t nl, al, vl;
        char node[STATE_NODE_MAX], attr[STATE_ATTR_MAX];
        unsigned char val[STATE_VAL_MAX];

        if (fread(&nl, sizeof nl, 1, f) != 1) {
            break;
        }
        if (fread(&al, sizeof al, 1, f) != 1 ||
            fread(&vl, sizeof vl, 1, f) != 1) {
            break;
        }
        if (nl >= sizeof node || al >= sizeof attr || vl > sizeof val) {
            break;                     /* corrupt: stop, keep what we have */
        }
        if (fread(node, 1, nl, f) != nl || fread(attr, 1, al, f) != al ||
            fread(val, 1, vl, f) != vl) {
            break;
        }
        node[nl] = '\0';
        attr[al] = '\0';
        (void)rec_set(s, node, attr, val, vl);
    }
    (void)fclose(f);
    s->dirty = 0;
    return 0;
}

static int
sidecar_save(tiku_store_t *s)
{
    char tmp[540];
    FILE *f;
    state_rec_t *r;

    snprintf(tmp, sizeof tmp, "%s.new", s->path);
    f = fopen(tmp, "wb");
    if (f == NULL) {
        return -1;
    }
    if (fwrite(SIDECAR_MAGIC, 1, strlen(SIDECAR_MAGIC), f)
        != strlen(SIDECAR_MAGIC)) {
        (void)fclose(f);
        return -1;
    }
    for (r = s->recs; r != NULL; r = r->next) {
        uint32_t nl = (uint32_t)strlen(r->node);
        uint32_t al = (uint32_t)strlen(r->attr);
        uint32_t vl = (uint32_t)r->len;

        if (fwrite(&nl, sizeof nl, 1, f) != 1 ||
            fwrite(&al, sizeof al, 1, f) != 1 ||
            fwrite(&vl, sizeof vl, 1, f) != 1 ||
            fwrite(r->node, 1, nl, f) != nl ||
            fwrite(r->attr, 1, al, f) != al ||
            (vl > 0u && fwrite(r->val, 1, vl, f) != vl)) {
            (void)fclose(f);
            (void)unlink(tmp);
            return -1;
        }
    }
    if (fclose(f) != 0) {
        (void)unlink(tmp);
        return -1;
    }
    /* Rename over the old file: a crash mid-write loses the newest change,
     * never the whole arrangement. */
    if (rename(tmp, s->path) != 0) {
        (void)unlink(tmp);
        return -1;
    }
    s->dirty = 0;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* public                                                                    */
/*---------------------------------------------------------------------------*/

static tiku_store_t *
store_new(enum store_kind kind)
{
    tiku_store_t *s = calloc(1, sizeof *s);

    if (s != NULL) {
        s->kind = kind;
        s->writable = 1;
    }
    return s;
}

tiku_store_t *
tiku_store_xattr_open(void)
{
    return store_new(STORE_XATTR);
}

tiku_store_t *
tiku_store_memory_open(void)
{
    return store_new(STORE_MEMORY);
}

tiku_store_t *
tiku_store_sidecar_open(const char *devid)
{
    tiku_store_t *s;
    char dir[512];

    if (devid == NULL || devid[0] == '\0') {
        return NULL;
    }
    s = store_new(STORE_SIDECAR);
    if (s == NULL) {
        return NULL;
    }
    if (sidecar_dir(dir, sizeof dir) != 0) {
        s->writable = 0;               /* nowhere to persist: degrade */
        return s;
    }
    /* Refuse rather than truncate: a shortened path would collide two
     * devices' arrangements into one file. */
    if (strlen(dir) + strlen(devid) + 8u >= sizeof s->path) {
        s->writable = 0;
        return s;
    }
    /* Assembled by hand: the length is proven above, and a formatter here
     * only teaches the compiler to doubt it. */
    {
        size_t dl = strlen(dir), il = strlen(devid);
        memcpy(s->path, dir, dl);
        s->path[dl] = '/';
        memcpy(s->path + dl + 1u, devid, il);
        memcpy(s->path + dl + 1u + il, ".state", 7u);
    }
    (void)sidecar_load(s);
    return s;
}

void
tiku_store_free(tiku_store_t *s)
{
    state_rec_t *r;

    if (s == NULL) {
        return;
    }
    if (s->kind == STORE_SIDECAR && s->dirty && s->writable) {
        (void)sidecar_save(s);
    }
    r = s->recs;
    while (r != NULL) {
        state_rec_t *next = r->next;
        free(r->val);
        free(r);
        r = next;
    }
    free(s);
}

int
tiku_store_writable(const tiku_store_t *s)
{
    return (s != NULL) ? s->writable : 0;
}

/** @brief Map an attribute name into the user xattr namespace. */
static void
xattr_name(const char *attr, char *out, size_t max)
{
    snprintf(out, max, "user.%s", attr);
}

int
tiku_state_read(tiku_store_t *s, const char *node, const char *attr,
                    void *buf, size_t max)
{
    if (s == NULL || node == NULL || attr == NULL || buf == NULL) {
        return -1;
    }
    if (s->kind == STORE_XATTR) {
#if defined(__linux__)
        char name[STATE_ATTR_MAX + 8];
        ssize_t n;

        xattr_name(attr, name, sizeof name);
        n = getxattr(node, name, buf, max);
        return (n >= 0) ? (int)n : -1;
#else
        return -1;
#endif
    }
    {
        state_rec_t *r = rec_find(s, node, attr);
        size_t n;

        if (r == NULL) {
            return -1;
        }
        n = (r->len < max) ? r->len : max;
        memcpy(buf, r->val, n);
        return (int)n;
    }
}

int
tiku_state_write(tiku_store_t *s, const char *node, const char *attr,
                     const void *buf, size_t len)
{
    if (s == NULL || node == NULL || attr == NULL || buf == NULL) {
        return -1;
    }
    if (!s->writable) {
        return -1;                     /* read-only: the caller degrades */
    }
    if (s->kind == STORE_XATTR) {
#if defined(__linux__)
        char name[STATE_ATTR_MAX + 8];

        xattr_name(attr, name, sizeof name);
        return (setxattr(node, name, buf, len, 0) == 0) ? 0 : -1;
#else
        return -1;
#endif
    }
    return rec_set(s, node, attr, buf, len);
}

int
tiku_state_each(tiku_store_t *s, const char *node,
                    int (*fn)(const char *attr, const void *val, size_t len,
                              void *ctx), void *ctx)
{
    int n = 0;

    if (s == NULL || node == NULL || fn == NULL) {
        return 0;
    }
    if (s->kind == STORE_XATTR) {
#if defined(__linux__)
        char names[4096];
        ssize_t len = listxattr(node, names, sizeof names);
        ssize_t at;

        if (len <= 0) {
            return 0;
        }
        for (at = 0; at < len; at += (ssize_t)strlen(names + at) + 1) {
            char val[512];
            const char *nm = names + at;
            ssize_t got;

            /* Only ours.  Another program's attributes are on the file too
             * and are none of this window's business. */
            if (strncmp(nm, "user._trk/", 10) != 0) {
                continue;
            }
            got = getxattr(node, nm, val, sizeof val);
            if (got < 0) {
                got = 0;
            }
            n++;
            if (fn(nm + 5, val, (size_t)got, ctx) != 0) {
                break;
            }
        }
        return n;
#else
        return 0;
#endif
    }
    {
        state_rec_t *r;

        for (r = s->recs; r != NULL; r = r->next) {
            if (strcmp(r->node, node) != 0) {
                continue;
            }
            n++;
            if (fn(r->attr, r->val, r->len, ctx) != 0) {
                break;
            }
        }
    }
    return n;
}

int
tiku_state_forget(tiku_store_t *s, const char *node)
{
    state_rec_t **p;
    int n = 0;

    if (s == NULL || node == NULL) {
        return -1;
    }
    p = &s->recs;
    while (*p != NULL) {
        if (strcmp((*p)->node, node) == 0) {
            state_rec_t *dead = *p;
            *p = dead->next;
            free(dead->val);
            free(dead);
            n++;
            s->dirty = 1;
        } else {
            p = &(*p)->next;
        }
    }
    return n;
}

int
tiku_store_flush(tiku_store_t *s)
{
    if (s == NULL) {
        return -1;
    }
    if (s->kind == STORE_SIDECAR && s->dirty && s->writable) {
        return sidecar_save(s);
    }
    return 0;
}

int
tiku_store_count(const tiku_store_t *s)
{
    const state_rec_t *r;
    int n = 0;

    for (r = (s != NULL) ? s->recs : NULL; r != NULL; r = r->next) {
        n++;
    }
    return n;
}

const char *
tiku_desktop_dir(char *out, size_t max)
{
    const char *set = getenv("TIKU_DESKTOP");
    const char *home = getenv("HOME");

    if (out == NULL || max == 0u) {
        return out;
    }
    if (set != NULL && set[0] != '\0') {
        snprintf(out, max, "%s", set);
        return out;
    }
    snprintf(out, max, "%s/.config/tracker/Desktop",
             (home != NULL && home[0] != '\0') ? home : "");
    return out;
}

const char *
tiku_fonts_dir(char *out, size_t max)
{
    const char *set = getenv("TIKU_FONTS");
    const char *home = getenv("HOME");

    if (out == NULL || max == 0u) {
        return out;
    }
    if (set != NULL && set[0] != '\0') {
        snprintf(out, max, "%s", set);
        return out;
    }
    snprintf(out, max, "%s/.config/tracker/fonts",
             (home != NULL && home[0] != '\0') ? home : "");
    return out;
}

int
tiku_state_mkparents(const char *path)
{
    char dir[1024];
    char *slash;
    size_t i;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash == NULL || slash == dir) {
        return 0;               /* no directory part to make */
    }
    *slash = '\0';
    /* Each component in turn, because a fresh home has neither .config nor
     * what is under it. */
    for (i = 1u; dir[i] != '\0'; i++) {
        if (dir[i] == '/') {
            dir[i] = '\0';
            (void)mkdir(dir, 0755);
            dir[i] = '/';
        }
    }
    (void)mkdir(dir, 0755);
    return 0;
}
