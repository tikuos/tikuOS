/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_backend_posix.c - local files, at full Tracker fidelity.
 *
 * Real permissions, real inodes, real extended attributes: everything Tracker
 * expects of a filesystem is here, so the compromises the device backend must
 * make never leak into the local experience.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_model.h"
#include "tiku_volume.h"
#include "tiku_state.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    tiku_store_t *store;
} posix_impl_t;

static tiku_store_t *px_store(tiku_backend_t *b);

/** @brief Fill a Model from a stat buffer. */
/** @brief Whether @p path holds anything at all, "." and ".." aside. */
/**
 * @brief Whether @p path is a volume's mount point, and its capacity.
 *
 * The mount table is re-read at most once a second: it changes when a
 * device is plugged in, which is rare, and a listing of a hundred rows
 * must not read /proc a hundred times.
 */
static int
volume_here(const char *path, tiku_model_t *m)
{
    static tiku_volumes_t vs;
    static time_t seen;
    time_t now = time(NULL);
    int i;

    if (now != seen) {
        (void)tiku_volumes_scan(&vs);
        seen = now;
    }
    for (i = 0; i < vs.n; i++) {
        if (strcmp(vs.v[i].mount, path) == 0) {
            m->facts.total = vs.v[i].total;
            m->facts.avail = vs.v[i].avail;
            m->facts.vol_flags = tiku_volume_flags(&vs.v[i]);
            /* A volume is shown by the VOLUME's name, not by the directory
             * entry it is mounted on: the boot volume's entry is "/", which
             * is a path and not a name (MA-021). */
            if (vs.v[i].name[0] != '\0') {
                snprintf(m->name, sizeof m->name, "%s", vs.v[i].name);
            }
            return 1;
        }
    }
    return 0;
}

static int
dir_has_entries(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;
    int any = 0;

    if (d == NULL) {
        return 0;
    }
    while (!any && (e = readdir(d)) != NULL) {
        any = (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0);
    }
    (void)closedir(d);
    return any;
}

/** @brief Whether this is the user's actual Desktop directory. */
static int
desktop_here(const char *path)
{
    char desk[TIKU_PATH_MAX];

    return strcmp(path, tiku_desktop_dir(desk, sizeof desk)) == 0;
}

/**
 * @brief The type a file's NAME says it is.
 *
 * A host filesystem carries no type attribute, so the name is the only
 * source there is; anything unrecognised stays the generic type rather
 * than being guessed at, which is what keeps "unknown" distinguishable
 * from "known to be data" (MA-002).
 */
static const char *
type_by_name(const char *name)
{
    static const struct { const char *ext, *type; } kByExt[] = {
        { ".txt",  "text/plain" },     { ".md",   "text/plain" },
        { ".c",    "text/x-source" },  { ".h",    "text/x-source" },
        { ".sh",   "text/x-script" },  { ".py",   "text/x-script" },
        { ".png",  "image/png" },      { ".bmp",  "image/bmp" },
        { ".jpg",  "image/jpeg" },     { ".jpeg", "image/jpeg" },
        { ".gif",  "image/gif" },      { ".html", "text/html" },
        { ".json", "text/json" },      { ".pdf",  "application/pdf" }
    };
    const char *dot;
    size_t i;

    if (name == NULL) {
        return "application/octet-stream";
    }
    dot = strrchr(name, '.');
    if (dot == NULL || dot == name) {
        return "application/octet-stream";
    }
    for (i = 0; i < sizeof kByExt / sizeof kByExt[0]; i++) {
        if (strcasecmp(dot, kByExt[i].ext) == 0) {
            return kByExt[i].type;
        }
    }
    return "application/octet-stream";
}

/**
 * @brief Whether @p path is an executable this system BUILT.
 *
 * Every TikuOS application carries a ".tikuos" section, put there by the
 * application kit it links.  Finding it costs the ELF header and the
 * section table -- a few hundred bytes off the front of the file -- and
 * never the body, so a listing can ask this of every program it shows.
 *
 * Deliberately not a guess: no reading the name for a prefix, no
 * scanning megabytes for a descriptor id.  A file either carries the
 * stamp or it does not.
 */
static int
is_tiku_app(const char *path)
{
    unsigned char h[64];
    FILE *f = fopen(path, "rb");
    unsigned long shoff, stroff = 0;
    unsigned shentsize, shnum, shstrndx, i;
    int found = 0;

    if (f == NULL) {
        return 0;
    }
    if (fread(h, 1u, sizeof h, f) != sizeof h ||
        h[0] != 0x7f || h[1] != 'E' || h[2] != 'L' || h[3] != 'F' ||
        h[4] != 2 /* 64-bit; the only shape this ships */ ||
        h[5] != 1 /* little-endian */) {
        (void)fclose(f);
        return 0;
    }
    shoff = 0;
    for (i = 0; i < 8u; i++) {
        shoff |= (unsigned long)h[40 + i] << (8u * i);
    }
    shentsize = (unsigned)h[58] | ((unsigned)h[59] << 8);
    shnum = (unsigned)h[60] | ((unsigned)h[61] << 8);
    shstrndx = (unsigned)h[62] | ((unsigned)h[63] << 8);
    if (shoff == 0 || shentsize < 64u || shnum == 0u ||
        shstrndx >= shnum || shnum > 4096u) {
        (void)fclose(f);
        return 0;
    }
    {
        /* The section-name table's own header, to find the names. */
        unsigned char sh[64];

        if (fseek(f, (long)(shoff + (unsigned long)shstrndx * shentsize),
                  SEEK_SET) != 0 ||
            fread(sh, 1u, sizeof sh, f) != sizeof sh) {
            (void)fclose(f);
            return 0;
        }
        for (i = 0; i < 8u; i++) {
            stroff |= (unsigned long)sh[24 + i] << (8u * i);
        }
    }
    for (i = 0; i < shnum && !found; i++) {
        unsigned char sh[64];
        unsigned long name_at = 0;
        char name[16];
        size_t got;

        if (fseek(f, (long)(shoff + (unsigned long)i * shentsize),
                  SEEK_SET) != 0 ||
            fread(sh, 1u, sizeof sh, f) != sizeof sh) {
            break;
        }
        name_at = (unsigned long)sh[0] | ((unsigned long)sh[1] << 8) |
                  ((unsigned long)sh[2] << 16) |
                  ((unsigned long)sh[3] << 24);
        if (fseek(f, (long)(stroff + name_at), SEEK_SET) != 0) {
            break;
        }
        got = fread(name, 1u, sizeof name - 1u, f);
        name[got] = '\0';
        if (strcmp(name, ".tikuos") == 0) {
            found = 1;
        }
    }
    (void)fclose(f);
    return found;
}

static void
from_stat(tiku_model_t *m, const char *path, const struct stat *st,
          int is_link)
{
    const char *slash = strrchr(path, '/');

    memset(m, 0, sizeof *m);
    snprintf(m->path, sizeof m->path, "%s", path);
    snprintf(m->name, sizeof m->name, "%s",
             (slash != NULL && slash[1] != '\0') ? slash + 1 : path);
    m->node_id = (uint64_t)st->st_ino;
    /* Which filesystem it is on, carried so a departing volume can be
     * swept for without re-stat()ing every open window (PVN-043). */
    m->facts.dev = (uint64_t)st->st_dev;
    m->facts.size = (int64_t)st->st_size;
    m->facts.mode = (unsigned)st->st_mode;
    m->facts.mode_known = 1;
    m->facts.mtime = (int64_t)st->st_mtime;
    m->facts.cap_known = 1;
    snprintf(m->facts.req_cap, sizeof m->facts.req_cap, "-");
    snprintf(m->facts.meta, sizeof m->facts.meta, "-");

    if (is_link) {
        m->kind = TIKU_KIND_SYMLINK;
    } else if (S_ISDIR(st->st_mode) && volume_here(path, m)) {
        /* A mount point is a VOLUME, not a folder that happens to sit
         * where one is: what may be done to it differs at every turn --
         * it cannot be renamed, dragging it to the Trash unmounts it, and
         * it is the only kind that has a capacity. */
        m->kind = (strcmp(path, "/") == 0) ? TIKU_KIND_ROOT
                                             : TIKU_KIND_VOLUME;
    } else if (S_ISDIR(st->st_mode)) {
        /* A directory called ".Trash" IS the Trash, recognised by name the
         * way a folder called "Desktop" is (tiku_model.c).  The store
         * has no other way to know: the Trash is a convention about a
         * location, not a property of the inode. */
        m->kind = (strcmp(m->name, ".Trash") == 0) ? TIKU_KIND_TRASH
                : desktop_here(path) ? TIKU_KIND_DESKTOP
                                     : TIKU_KIND_DIRECTORY;
        if (m->kind == TIKU_KIND_TRASH) {
            m->facts.trash_full = dir_has_entries(path);
        }
    } else {
        m->kind = TIKU_KIND_FILE;
    }
    /* Permission as the current user sees it: what the UI must reflect is
     * whether THIS user can act, not the raw mode bits. */
    if (access(path, R_OK) == 0) { m->facts.perm |= TIKU_P_READ; }
    if (access(path, W_OK) == 0) { m->facts.perm |= TIKU_P_WRITE; }
    if (access(path, X_OK) == 0) { m->facts.perm |= TIKU_P_EXEC; }
    snprintf(m->type, sizeof m->type, "%s",
             m->kind == TIKU_KIND_ROOT ? "application/x-vnd.Be-root"
             : S_ISDIR(st->st_mode) ? "application/x-vnd.Be-directory"
             /* An executable this system built says so for itself, and
              * gets the application type rather than the blob one: it is
              * the difference between a program a person can start and
              * whatever else happens to carry the execute bit. */
             : ((m->facts.perm & TIKU_P_EXEC) && is_tiku_app(path))
                                  ? "application/x-vnd.be-app"
                                  : type_by_name(m->name));
}

static int
px_stat(tiku_backend_t *b, const char *path, tiku_model_t *out)
{
    struct stat st, lst;
    posix_impl_t *impl = (b != NULL) ? b->impl : NULL;

    if (path == NULL || out == NULL) {
        return -1;
    }
    if (lstat(path, &lst) != 0) {
        return -1;
    }
    if (S_ISLNK(lst.st_mode)) {
        /* Report the link, but size/kind of the target when it resolves --
         * Tracker shows a broken link differently from a live one. */
        if (stat(path, &st) == 0) {
            from_stat(out, path, &st, 1);
            /* The link's IDENTITY is its own inode, not its target's: a
             * link beside its target would otherwise share the target's
             * identity, and an identity-keyed diff would never see the
             * new row (the node_ref of the link, as the source keeps). */
            out->node_id = (uint64_t)lst.st_ino;
        } else {
            /* The target has gone.  The LINK has not, so it keeps its row
             * and carries the fact -- a broken link is a thing to see, not
             * an entry to remove. */
            from_stat(out, path, &lst, 1);
            out->facts.link_broken = 1;
        }
    } else {
        from_stat(out, path, &lst, 0);
    }
    /* The Desktop directory is a special Tracker surface.  Seed its icon
     * descriptor the first time it is resolved so a foreign filesystem gets
     * the same stable art as the home Desktop (FS-082).  The write is best
     * effort: read-only volumes still list normally. */
    if (out->kind == TIKU_KIND_DESKTOP && impl != NULL) {
        tiku_store_t *store = px_store(b);
        char existing[16];

        if (store == NULL || !tiku_store_writable(store)) {
            out->backend = b;
            return 0;
        }
        if (tiku_state_read(store, path, "_trk/icon", existing,
                                sizeof existing) >= 0) {
            out->backend = b;
            return 0;
        }
        static const char desk_icon[] = "desktop";

        (void)tiku_state_write(store, path, "_trk/icon",
                                   desk_icon, sizeof desk_icon);
    }
    out->backend = b;
    return 0;
}

static int
px_list(tiku_backend_t *b, const char *path, tiku_entry_fn fn,
        void *ctx)
{
    DIR *d = opendir(path);
    struct dirent *e;
    int n = 0;

    if (d == NULL) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char child[TIKU_PATH_MAX];
        tiku_model_t m;
        size_t pl = strlen(path);

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (pl + strlen(e->d_name) + 2u > sizeof child) {
            continue;
        }
        memcpy(child, path, pl);
        if (pl > 0u && path[pl - 1u] != '/') { child[pl++] = '/'; }
        memcpy(child + pl, e->d_name, strlen(e->d_name) + 1u);

        if (px_stat(b, child, &m) != 0) {
            /* Unreadable right now -- busy, mid-write, a transient error.
             * It is still an ENTRY, so it is reported with what is known and
             * marked unreadable; dropping it makes a file disappear from a
             * listing because it was briefly locked, and reappear later as
             * though it had just been created. */
            memset(&m, 0, sizeof m);
            (void)snprintf(m.path, sizeof m.path, "%s", child);
            (void)snprintf(m.name, sizeof m.name, "%s", e->d_name);
            (void)snprintf(m.facts.meta, sizeof m.facts.meta, "-");
            (void)snprintf(m.facts.req_cap, sizeof m.facts.req_cap, "?");
            m.kind = TIKU_KIND_UNKNOWN;
            m.facts.cap_known = 0;
            m.backend = b;
        }
        n++;
        if (fn != NULL && fn(&m, ctx) != 0) {
            break;
        }
    }
    (void)closedir(d);
    return n;
}

static int
px_read(tiku_backend_t *b, const char *path, void *buf, size_t max)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    (void)b;
    if (f == NULL) {
        return -1;
    }
    n = fread(buf, 1, max, f);
    (void)fclose(f);
    return (int)n;
}

static int
px_write(tiku_backend_t *b, const char *path, const void *buf, size_t len,
         char *err, size_t errmax)
{
    FILE *f;

    (void)b;
    f = fopen(path, "wb");
    if (f == NULL) {
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "cannot write '%s' (%s)", path,
                     strerror(errno));
        }
        return -1;
    }
    if (len > 0u && fwrite(buf, 1, len, f) != len) {
        (void)fclose(f);
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "short write to '%s'", path);
        }
        return -1;
    }
    return (fclose(f) == 0) ? 0 : -1;
}

static int
px_pump(tiku_backend_t *b, int timeout_ms)
{
    /* inotify lands with the liveness layer; local files are quiet until
     * then, which is honest rather than a pretend zero. */
    (void)b;
    (void)timeout_ms;
    return 0;
}

static tiku_store_t *
px_store(tiku_backend_t *b)
{
    posix_impl_t *impl = b->impl;

    if (impl->store == NULL) {
        impl->store = tiku_store_xattr_open();
    }
    return impl->store;
}

static void
px_close(tiku_backend_t *b)
{
    posix_impl_t *impl = b->impl;

    if (impl != NULL) {
        tiku_store_free(impl->store);
        free(impl);
    }
    free(b);
}

/** @brief Apply read/write/execute to a local file. */
static int
px_setperm(tiku_backend_t *b, const char *path, unsigned perm, char *err,
           size_t errmax)
{
    struct stat st;
    mode_t mode;

    (void)b;
    if (path == NULL || stat(path, &st) != 0) {
        snprintf(err, errmax, "%s", strerror(errno));
        return -1;
    }
    /* The OWNER's three bits, which is what the grid edits; group and other
     * are left exactly as they were rather than being derived from it. */
    mode = st.st_mode & (mode_t)~(S_IRUSR | S_IWUSR | S_IXUSR);
    if (perm & TIKU_P_READ)  { mode |= S_IRUSR; }
    if (perm & TIKU_P_WRITE) { mode |= S_IWUSR; }
    if (perm & TIKU_P_EXEC)  { mode |= S_IXUSR; }
    if (chmod(path, mode) != 0) {
        snprintf(err, errmax, "%s", strerror(errno));
        return -1;
    }
    return 0;
}

static const tiku_backend_ops_t POSIX_OPS = {
    /* A local filesystem has no change ring to ask, so it says so and
     * the view polls -- which is what NULL here means. */
    /* No change ring, so no identity to resolve either. */
    "posix", px_stat, px_list, px_read, px_write, px_pump, NULL, NULL,
    px_setperm, px_store, px_close
};

tiku_backend_t *
tiku_backend_posix_open(void)
{
    tiku_backend_t *b = calloc(1, sizeof *b);
    posix_impl_t *impl = calloc(1, sizeof *impl);

    if (b == NULL || impl == NULL) {
        free(b);
        free(impl);
        return NULL;
    }
    b->ops = &POSIX_OPS;
    b->impl = impl;
    return b;
}
