/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_volume.c - reading the mount table and answering for it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_volume.h"

#include <errno.h>
#include <sys/mount.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

/** @brief The leaf of a mount point, which is the name a volume shows. */
static void
name_of(const char *mount, char *out, size_t max)
{
    const char *slash = strrchr(mount, '/');

    if (strcmp(mount, "/") == 0) {
        snprintf(out, max, "Boot");
        return;
    }
    snprintf(out, max, "%.*s", (int)max - 1,
             (slash != NULL && slash[1] != '\0') ? slash + 1 : mount);
}

int
tiku_volumes_scan(tiku_volumes_t *vs)
{
    FILE *f;
    char line[1024];

    if (vs == NULL) {
        return -1;
    }
    memset(vs, 0, sizeof *vs);
    /* The kernel's own table rather than /etc/mtab: what is mounted NOW is
     * the question, and the two can disagree. */
    f = fopen("/proc/self/mounts", "r");
    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL &&
           vs->n < TIKU_VOLUME_MAX) {
        char dev[256], mount[TIKU_PATH_MAX], type[64], opts[256];
        tiku_volume_t *v;
        struct stat st;
        struct statvfs sv;

        if (sscanf(line, "%255s %511s %63s %255s", dev, mount, type,
                   opts) != 4) {
            continue;
        }
        /* Only volumes a person could browse: the kernel's own
         * bookkeeping filesystems are not places files live. */
        if (strcmp(type, "proc") == 0 || strcmp(type, "sysfs") == 0 ||
            strcmp(type, "cgroup") == 0 || strcmp(type, "cgroup2") == 0 ||
            strcmp(type, "devpts") == 0 || strcmp(type, "securityfs") == 0 ||
            strcmp(type, "debugfs") == 0 || strcmp(type, "tracefs") == 0 ||
            strcmp(type, "bpf") == 0 || strcmp(type, "pstore") == 0 ||
            strcmp(type, "mqueue") == 0 || strcmp(type, "hugetlbfs") == 0 ||
            strcmp(type, "configfs") == 0 || strcmp(type, "fusectl") == 0 ||
            strcmp(type, "binfmt_misc") == 0 ||
            strcmp(type, "autofs") == 0 || strcmp(type, "nsfs") == 0) {
            continue;
        }
        if (stat(mount, &st) != 0) {
            continue;
        }
        v = &vs->v[vs->n];
        memset(v, 0, sizeof *v);
        snprintf(v->mount, sizeof v->mount, "%s", mount);
        name_of(mount, v->name, sizeof v->name);
        v->dev = st.st_dev;
        snprintf(v->source, sizeof v->source, "%s", dev);
        /* "ro" as a whole option, not a substring: "rootcontext=" and
         * "errors=remount-ro" both contain it. */
        {
            const char *o = opts;

            while (o != NULL && *o != '\0') {
                if (strncmp(o, "ro", 2) == 0 &&
                    (o[2] == ',' || o[2] == '\0')) {
                    v->read_only = 1;
                    break;
                }
                o = strchr(o, ',');
                if (o != NULL) { o++; }
            }
        }
        snprintf(v->fstype, sizeof v->fstype, "%.*s",
                 (int)(sizeof v->fstype - 1u), type);
        /* Served over a network rather than by a disk.  It matters twice:
         * such a volume gets its own icon, and the setting that puts disks
         * on the desktop treats shares separately (IV-006, PVN-040). */
        v->shared = tiku_volume_shared_type(type);
        v->persistent = !(strcmp(type, "tmpfs") == 0 ||
                          strcmp(type, "devtmpfs") == 0 ||
                          strcmp(type, "ramfs") == 0 ||
                          strcmp(type, "overlay") == 0 ||
                          strcmp(type, "squashfs") == 0 ||
                          strncmp(type, "fuse.portal", 11) == 0);
        v->is_boot = (strcmp(mount, "/") == 0);
        v->removable = !v->is_boot;
        if (statvfs(mount, &sv) == 0) {
            v->total = (uint64_t)sv.f_blocks * (uint64_t)sv.f_frsize;
            v->avail = (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
        }
        vs->n++;
    }
    (void)fclose(f);
    return vs->n;
}

int
tiku_volume_shared_type(const char *fstype)
{
    if (fstype == NULL) {
        return 0;
    }
    return (strcmp(fstype, "nfs") == 0 || strcmp(fstype, "nfs4") == 0 ||
            strcmp(fstype, "cifs") == 0 || strcmp(fstype, "smb3") == 0 ||
            strcmp(fstype, "smbfs") == 0 || strcmp(fstype, "9p") == 0 ||
            strncmp(fstype, "fuse.sshfs", 10) == 0);
}

unsigned
tiku_volume_flags(const tiku_volume_t *v)
{
    unsigned f = 0u;

    if (v == NULL) {
        return 0u;
    }
    if (v->read_only) { f |= TIKU_VOL_READ_ONLY; }
    if (v->is_boot)   { f |= TIKU_VOL_BOOT; }
    if (v->removable) { f |= TIKU_VOL_REMOVABLE; }
    if (v->shared)    { f |= TIKU_VOL_SHARED; }
    return f;
}

const tiku_volume_t *
tiku_volume_of(const tiku_volumes_t *vs, const char *path)
{
    const tiku_volume_t *best = NULL;
    struct stat st;
    int i;

    if (vs == NULL || path == NULL || stat(path, &st) != 0) {
        return NULL;
    }
    /* By device rather than by longest prefix: a bind mount and a symlink
     * both make the prefix lie, and the device does not. */
    for (i = 0; i < vs->n; i++) {
        if (vs->v[i].dev == st.st_dev) {
            /* The shortest mount point on that device is the volume; a
             * deeper one is a mount of the same device elsewhere. */
            if (best == NULL ||
                strlen(vs->v[i].mount) < strlen(best->mount)) {
                best = &vs->v[i];
            }
        }
    }
    return best;
}

int
tiku_volume_is_root(const tiku_volumes_t *vs, const char *path)
{
    int i;

    if (vs == NULL || path == NULL) {
        return 0;
    }
    for (i = 0; i < vs->n; i++) {
        if (strcmp(vs->v[i].mount, path) == 0) {
            return 1;
        }
    }
    return 0;
}

int
tiku_volume_may_unmount(const tiku_volumes_t *vs, const char *path,
                            char *why, size_t max)
{
    const tiku_volume_t *v;

    if (!tiku_volume_is_root(vs, path)) {
        return 0;                   /* not a volume: not this gesture */
    }
    v = tiku_volume_of(vs, path);
    if (v == NULL) {
        return 0;
    }
    if (v->is_boot) {
        if (why != NULL) {
            snprintf(why, max, "You cannot unmount the boot volume "
                               "\"%.40s\".", v->name);
        }
        return 0;
    }
    return 1;
}

int
tiku_volume_may_write(const tiku_volumes_t *vs, const char *path,
                          int write, char *why, size_t max)
{
    const tiku_volume_t *v;

    if (!write) {
        return 1;                   /* reading is always allowed */
    }
    v = tiku_volume_of(vs, path);
    if (v == NULL || !v->read_only) {
        return 1;
    }
    if (why != NULL) {
        /* The original's words: it names the volume, because a user with
         * several mounted needs to know which one refused. */
        snprintf(why, max, "Files cannot be moved or deleted from a "
                           "read-only volume (\"%.40s\").", v->name);
    }
    return 0;
}

int
tiku_volume_shows_icon(const tiku_volumes_t *vs,
                           const tiku_volume_t *v)
{
    size_t n;
    int i;

    if (vs == NULL || v == NULL) {
        return 0;
    }
    if (v->is_boot) {
        return 1;                   /* everything is inside it */
    }
    n = strlen(v->mount);
    for (i = 0; i < vs->n; i++) {
        const char *o = vs->v[i].mount;
        size_t on = strlen(o);

        if (&vs->v[i] == v || vs->v[i].is_boot) {
            continue;
        }
        /* Mounted under another volume, on a whole path component: a
         * detail of that one rather than a second disk. */
        if (on < n && strncmp(v->mount, o, on) == 0 &&
            (o[on - 1u] == '/' || v->mount[on] == '/')) {
            return 0;
        }
    }
    return 1;
}

tiku_space_t
tiku_volume_space(const tiku_volume_t *v, int *used_pct)
{
    if (v == NULL) {
        if (used_pct != NULL) { *used_pct = 0; }
        return TIKU_SPACE_FREE;
    }
    return tiku_volume_space_facts(v->total, v->avail, used_pct);
}

tiku_space_t
tiku_volume_space_facts(uint64_t total, uint64_t avail, int *used_pct)
{
    struct { uint64_t total, avail; } vv;
    const void *vp = &vv;
    uint64_t used;
    int pct = 0;

    vv.total = total;
    vv.avail = avail;
    (void)vp;
    if (used_pct != NULL) {
        *used_pct = 0;
    }
    if (total == 0u) {
        /* A volume that will not say its size shows an empty bar rather
         * than a full one: guessing "full" would warn about every device
         * whose driver has no free-space call. */
        return TIKU_SPACE_FREE;
    }
    used = (total > avail) ? (total - avail) : 0u;
    pct = (int)((used * 100u + total / 2u) / total);
    if (pct > 100) { pct = 100; }
    if (used_pct != NULL) {
        *used_pct = pct;
    }
    /* The threshold is on what is LEFT, not on what is used: a rounded
     * used-share of 95 covers anything from 94.5 to 95.5, and the volume
     * that matters is the one with under a twentieth free. */
    if (avail * 100u < total * (uint64_t)TIKU_VOLUME_WARN_PCT) {
        return TIKU_SPACE_WARNING;
    }
    return TIKU_SPACE_USED;
}

/*---------------------------------------------------------------------------*/
/* Watching                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Whether @p a names the same mounted volume as @p b. */
static int
same_volume(const tiku_volume_t *a, const tiku_volume_t *b)
{
    /* Both, not either: a mount point reused by another device is one
     * volume leaving and another arriving, and a device remounted
     * somewhere else is too. */
    return strcmp(a->mount, b->mount) == 0 && a->dev == b->dev;
}

void
tiku_volwatch_init(tiku_volwatch_t *w)
{
    if (w == NULL) {
        return;
    }
    memset(w, 0, sizeof *w);
    if (tiku_volumes_scan(&w->was) >= 0) {
        w->primed = 1;
    }
}

int
tiku_volwatch_diff(const tiku_volumes_t *was,
                       const tiku_volumes_t *now,
                       tiku_volchange_t *out, int max)
{
    int i, j, n = 0;

    if (was == NULL || now == NULL || out == NULL || max <= 0) {
        return 0;
    }
    for (i = 0; i < was->n && n < max; i++) {
        for (j = 0; j < now->n; j++) {
            if (same_volume(&was->v[i], &now->v[j])) {
                if (strcmp(was->v[i].name, now->v[j].name) != 0) {
                    /* The volume stayed and its label did not: a rename,
                     * not a departure (MA-021). */
                    memset(&out[n], 0, sizeof out[n]);
                    out[n].v = now->v[j];
                    out[n].renamed = 1;
                    n++;
                }
                break;
            }
        }
        if (j == now->n && n < max) {
            memset(&out[n], 0, sizeof out[n]);
            out[n].v = was->v[i];
            out[n].mounted = 0;
            n++;
        }
    }
    for (j = 0; j < now->n && n < max; j++) {
        for (i = 0; i < was->n; i++) {
            if (same_volume(&was->v[i], &now->v[j])) {
                break;
            }
        }
        if (i == was->n) {
            memset(&out[n], 0, sizeof out[n]);
            out[n].v = now->v[j];
            out[n].mounted = 1;
            n++;
        }
    }
    return n;
}

int
tiku_volwatch_poll(tiku_volwatch_t *w, tiku_volchange_t *out,
                       int max)
{
    tiku_volumes_t now;
    int n;

    if (w == NULL || out == NULL || max <= 0) {
        return 0;
    }
    if (!w->primed) {
        tiku_volwatch_init(w);
        return 0;               /* the first look is the baseline */
    }
    if (tiku_volumes_scan(&now) < 0) {
        return 0;
    }
    n = tiku_volwatch_diff(&w->was, &now, out, max);
    /* The baseline moves whether or not anything was reported, and whether
     * or not the caller had room: a change dropped for want of space must
     * not be reported for ever. */
    w->was = now;
    return n;
}

int
tiku_partitions_scan(tiku_partition_t *out, int max)
{
    tiku_volumes_t vs;
    FILE *f;
    char line[256];
    int n = 0, have_vols, i, j;

    if (out == NULL || max <= 0) {
        return 0;
    }
    have_vols = tiku_volumes_scan(&vs) > 0;
    f = fopen("/proc/partitions", "r");
    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof line, f) != NULL && n < max) {
        unsigned major, minor;
        unsigned long long kb;
        char name[32];

        if (sscanf(line, "%u %u %llu %31s", &major, &minor, &kb,
                   name) != 4) {
            continue;
        }
        /* The kernel's own ram, loop and zram devices are not disks a
         * person mounts. */
        if (strncmp(name, "ram", 3) == 0 || strncmp(name, "loop", 4) == 0 ||
            strncmp(name, "zram", 4) == 0) {
            continue;
        }
        memset(&out[n], 0, sizeof out[n]);
        snprintf(out[n].name, sizeof out[n].name, "%s", name);
        out[n].kb = kb;
        if (have_vols) {
            char devpath[80];

            snprintf(devpath, sizeof devpath, "/dev/%s", name);
            for (i = 0; i < vs.n; i++) {
                if (strcmp(vs.v[i].source, devpath) == 0) {
                    out[n].mounted = 1;
                    snprintf(out[n].mount, sizeof out[n].mount, "%s",
                             vs.v[i].mount);
                    break;
                }
            }
        }
        n++;
    }
    (void)fclose(f);
    /* A disk that is carved into partitions is not itself the mountable
     * thing: drop any entry another entry's name extends. */
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            size_t li = strlen(out[i].name);

            if (i != j && strncmp(out[j].name, out[i].name, li) == 0 &&
                strlen(out[j].name) > li) {
                out[i] = out[--n];
                i--;
                break;
            }
        }
    }
    return n;
}

int
tiku_volume_mount(const char *devname, char *why, size_t max)
{
    char dev[80];

    if (why != NULL && max > 0u) {
        why[0] = '\0';
    }
    if (devname == NULL || devname[0] == '\0') {
        return -1;
    }
    snprintf(dev, sizeof dev, "/dev/%s", devname);
    /* The permission check comes before anything else the kernel would
     * object to, so an unprivileged browser learns the true reason. */
    if (mount(dev, "/mnt", "ext4", 0, NULL) == 0) {
        return 0;
    }
    if (why != NULL) {
        snprintf(why, max, (errno == EPERM)
                     ? "Mounting \"%.40s\" needs the system's leave."
                     : "\"%.40s\" could not be mounted.", devname);
    }
    return -1;
}

tiku_unmount_t
tiku_volume_unmount(const tiku_volumes_t *vs, const char *path,
                        tiku_before_unmount_fn before, void *ctx,
                        char *why, size_t max)
{
    const tiku_volume_t *v;

    if (why != NULL && max > 0u) {
        why[0] = '\0';
    }
    if (!tiku_volume_may_unmount(vs, path, why, max)) {
        return TIKU_UNMOUNT_REFUSED;
    }
    v = tiku_volume_of(vs, path);
    if (v == NULL) {
        return TIKU_UNMOUNT_REFUSED;
    }
    if (before != NULL) {
        /* While the volume is still THERE: afterwards there is nowhere to
         * write, and the arrangement of everything open on that disk is
         * lost the moment it leaves (AW-096). */
        before(v->dev, ctx);
    }
    if (umount(v->mount) != 0) {
        if (why != NULL) {
            snprintf(why, max, (errno == EBUSY)
                         ? "Something is still using \"%.40s\"."
                         : "\"%.40s\" could not be unmounted.", v->name);
        }
        return (errno == EBUSY) ? TIKU_UNMOUNT_BUSY
                                : TIKU_UNMOUNT_FAILED;
    }
    return TIKU_UNMOUNT_OK;
}
