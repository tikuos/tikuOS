/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fat.c - read-only FAT32 reader.
 *
 * No hardware headers: tools/fat32 builds this file on a Linux host and
 * regression-tests it against loopback images written by mkfs.vfat.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fat.h"

/*---------------------------------------------------------------------------*/
/* LITTLE-ENDIAN ACCESSORS                                                   */
/*---------------------------------------------------------------------------*/
/*
 * FAT structures are packed little-endian with fields at unaligned offsets:
 * the BPB's 16-bit fields at 0x0B, 0x11 and 0x13, the 32-bit start and length
 * of an MBR partition entry, the UTF-16 units of a long-name entry.  An
 * unaligned pointer access is undefined in C and faults on some targets, so
 * every field is read a byte at a time.
 */

/** @brief Little-endian u16 at @p p, read a byte at a time. */
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/** @brief Little-endian u32 at @p p, read a byte at a time. */
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

const char *tiku_fat_strerror(tiku_fat_err_t e)
{
    switch (e) {
    case TIKU_FAT_OK:            return "ok";
    case TIKU_FAT_ERR_IO:        return "IO";
    case TIKU_FAT_ERR_NOFS:      return "NOFS";
    case TIKU_FAT_ERR_NOT_FAT32: return "NOT_FAT32";
    case TIKU_FAT_ERR_GEOM:      return "GEOM";
    case TIKU_FAT_ERR_NOENT:     return "NOENT";
    case TIKU_FAT_ERR_NOTDIR:    return "NOTDIR";
    case TIKU_FAT_ERR_CORRUPT:   return "CORRUPT";
    case TIKU_FAT_ERR_ARG:       return "ARG";
    default:                     return "?";
    }
}

/*---------------------------------------------------------------------------*/
/* MOUNT                                                                     */
/*---------------------------------------------------------------------------*/

/* FAT entry values that mark a state, not a next cluster. */
#define CLUS_FREE     0x00000000u
#define CLUS_BAD      0x0FFFFFF7u
#define CLUS_EOC_MIN  0x0FFFFFF8u
#define CLUS_MASK     0x0FFFFFFFu   /* the top 4 bits are reserved         */

/**
 * @brief Validate a boot sector as a FAT BPB and fill in the geometry.
 *
 * The FAT width comes from the data cluster count (<4085 FAT12, <65525 FAT16,
 * else FAT32); the boot sector's type string, which the spec calls
 * informational, is not read.  Anything but FAT32 is TIKU_FAT_ERR_NOT_FAT32.
 */
static tiku_fat_err_t bpb_parse(tiku_fat_t *fs, const uint8_t *sec,
                                uint32_t base)
{
    uint32_t root_ent, root_sec, fat_sz, tot_sec, data_sec;

    if (rd16(&sec[510]) != 0xAA55u) { return TIKU_FAT_ERR_NOFS; }

    fs->bytes_per_sec = rd16(&sec[11]);
    fs->sec_per_clus  = sec[13];
    fs->rsvd_sec      = rd16(&sec[14]);
    fs->num_fats      = sec[16];
    root_ent          = rd16(&sec[17]);

    /* Reject the impossible before dividing by any of it. */
    if (fs->bytes_per_sec != TIKU_FAT_SECTOR) { return TIKU_FAT_ERR_GEOM; }
    if (fs->sec_per_clus == 0u ||
        (fs->sec_per_clus & (uint8_t)(fs->sec_per_clus - 1u)) != 0u) {
        return TIKU_FAT_ERR_GEOM;      /* must be a power of two           */
    }
    if (fs->rsvd_sec == 0u)                 { return TIKU_FAT_ERR_GEOM; }
    if (fs->num_fats == 0u || fs->num_fats > 2u) { return TIKU_FAT_ERR_GEOM; }

    fat_sz  = rd16(&sec[22]);
    if (fat_sz == 0u) { fat_sz = rd32(&sec[36]); }
    tot_sec = rd16(&sec[19]);
    if (tot_sec == 0u) { tot_sec = rd32(&sec[32]); }
    if (fat_sz == 0u || tot_sec == 0u) { return TIKU_FAT_ERR_GEOM; }

    root_sec = ((root_ent * 32u) + (uint32_t)fs->bytes_per_sec - 1u) /
               fs->bytes_per_sec;

    {
        uint32_t used = (uint32_t)fs->rsvd_sec +
                        ((uint32_t)fs->num_fats * fat_sz) + root_sec;
        if (used >= tot_sec) { return TIKU_FAT_ERR_GEOM; }
        data_sec = tot_sec - used;
    }
    fs->clusters = data_sec / fs->sec_per_clus;

    /* The FAT width, from the cluster count. */
    if (fs->clusters < 65525u) { return TIKU_FAT_ERR_NOT_FAT32; }

    /* FAT32 also has no fixed root directory and no 16-bit FAT size. */
    if (root_ent != 0u || rd16(&sec[22]) != 0u) {
        return TIKU_FAT_ERR_NOT_FAT32;
    }

    fs->fat_sectors = fat_sz;
    fs->total_sec   = tot_sec;
    fs->root_clus   = rd32(&sec[44]) & CLUS_MASK;
    if (fs->root_clus < 2u || fs->root_clus >= fs->clusters + 2u) {
        return TIKU_FAT_ERR_GEOM;
    }
    fs->part_lba = base;
    fs->fat_lba  = base + fs->rsvd_sec;
    fs->data_lba = fs->fat_lba + ((uint32_t)fs->num_fats * fat_sz) + root_sec;
    return TIKU_FAT_OK;
}

tiku_fat_err_t tiku_fat_mount(tiku_fat_t *fs, tiku_fat_read_fn read, void *ctx)
{
    uint8_t sec[TIKU_FAT_SECTOR];
    tiku_fat_err_t rc;
    unsigned i;
    int wrong_width;

    if (fs == NULL || read == NULL) { return TIKU_FAT_ERR_ARG; }
    fs->read = read;
    fs->ctx  = ctx;

    if (read(0u, 1u, sec, ctx) != 0) { return TIKU_FAT_ERR_IO; }

    /* Whole-device filesystem?  Try that first: a superfloppy has no
     * partition table and its BPB sits right here. */
    rc = bpb_parse(fs, sec, 0u);
    if (rc == TIKU_FAT_OK) { return rc; }
    wrong_width = (rc == TIKU_FAT_ERR_NOT_FAT32);

    /*
     * Otherwise look for an MBR.  The partition type byte (0x0B / 0x0C for
     * FAT32) only skips empty entries; every volume has to pass bpb_parse(),
     * so a partition mislabelled by a formatter mounts if it is really FAT32,
     * and one labelled 0x0C that is really FAT16 is refused.
     */
    if (rd16(&sec[510]) != 0xAA55u) { return TIKU_FAT_ERR_NOFS; }

    for (i = 0u; i < 4u; i++) {
        const uint8_t *e = &sec[446u + (i * 16u)];
        uint32_t start = rd32(&e[8]);
        uint32_t count = rd32(&e[12]);
        uint8_t  type  = e[4];
        uint8_t  part[TIKU_FAT_SECTOR];

        if (type == 0u || start == 0u || count == 0u) { continue; }
        if (read(start, 1u, part, ctx) != 0) { continue; }
        rc = bpb_parse(fs, part, start);
        if (rc == TIKU_FAT_OK) { return rc; }
        if (rc == TIKU_FAT_ERR_NOT_FAT32) { wrong_width = 1; }
    }
    /* A FAT of the wrong width anywhere on the device returns
     * TIKU_FAT_ERR_NOT_FAT32, whichever candidate was parsed last. */
    return wrong_width ? TIKU_FAT_ERR_NOT_FAT32 : TIKU_FAT_ERR_NOFS;
}

/*---------------------------------------------------------------------------*/
/* THE FAT                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief First absolute sector of a data cluster. */
static uint32_t clus_lba(const tiku_fat_t *fs, uint32_t clus)
{
    return fs->data_lba + ((clus - 2u) * fs->sec_per_clus);
}

/**
 * @brief Follow one link of the chain.
 *
 * Classifies every value the FAT can hold: an out-of-range link, a bad cluster
 * and a free cluster inside a chain return TIKU_FAT_ERR_CORRUPT.  An
 * end-of-chain mark sets *next to 0.
 */
static tiku_fat_err_t fat_next(tiku_fat_t *fs, uint32_t clus, uint32_t *next)
{
    uint8_t sec[TIKU_FAT_SECTOR];
    uint32_t off, lba, v;

    if (clus < 2u || clus >= fs->clusters + 2u) {
        return TIKU_FAT_ERR_CORRUPT;
    }
    off = clus * 4u;
    lba = fs->fat_lba + (off / fs->bytes_per_sec);
    if (fs->read(lba, 1u, sec, fs->ctx) != 0) { return TIKU_FAT_ERR_IO; }

    v = rd32(&sec[off % fs->bytes_per_sec]) & CLUS_MASK;
    if (v >= CLUS_EOC_MIN) { *next = 0u; return TIKU_FAT_OK; }  /* end     */
    if (v == CLUS_FREE || v == CLUS_BAD) { return TIKU_FAT_ERR_CORRUPT; }
    if (v < 2u || v >= fs->clusters + 2u) { return TIKU_FAT_ERR_CORRUPT; }
    *next = v;
    return TIKU_FAT_OK;
}

/*---------------------------------------------------------------------------*/
/* DIRECTORIES                                                               */
/*---------------------------------------------------------------------------*/

#define ATTR_RO     0x01u
#define ATTR_HIDDEN 0x02u
#define ATTR_SYSTEM 0x04u
#define ATTR_VOLID  0x08u
#define ATTR_DIR    0x10u
#define ATTR_LFN    0x0Fu

/** @brief The 8.3 checksum a long-name sequence must agree with. */
static uint8_t sfn_checksum(const uint8_t *sfn)
{
    uint8_t sum = 0u;
    unsigned i;
    for (i = 0u; i < 11u; i++) {
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + sfn[i]);
    }
    return sum;
}

/** @brief Render the 8.3 name as "NAME.EXT". */
static void sfn_render(const uint8_t *e, char *out)
{
    unsigned i, n = 0u;
    for (i = 0u; i < 8u && e[i] != ' '; i++) { out[n++] = (char)e[i]; }
    if (e[8] != ' ') {
        out[n++] = '.';
        for (i = 8u; i < 11u && e[i] != ' '; i++) { out[n++] = (char)e[i]; }
    }
    out[n] = '\0';
}

/** @brief Pull the 13 UTF-16 units out of one long-name entry. */
static void lfn_chars(const uint8_t *e, char *dst)
{
    static const uint8_t off[13] = { 1,3,5,7,9, 14,16,18,20,22,24, 28,30 };
    unsigned i;
    for (i = 0u; i < 13u; i++) {
        uint16_t u = rd16(&e[off[i]]);
        /* Non-ASCII renders as '?', which keeps the name's length. */
        dst[i] = (u == 0u || u == 0xFFFFu) ? '\0'
               : (u < 0x80u ? (char)u : '?');
    }
}

/** @brief Point @p dir at the first entry of the directory at @p clus. */
static tiku_fat_err_t dir_start(tiku_fat_t *fs, uint32_t clus,
                                tiku_fat_dir_t *dir)
{
    (void)fs;
    dir->clus        = clus;
    dir->sec_in_clus = 0u;
    dir->ent_in_sec  = 0u;
    dir->steps       = 0u;
    dir->done        = 0u;
    return TIKU_FAT_OK;
}

tiku_fat_err_t tiku_fat_readdir(tiku_fat_t *fs, tiku_fat_dir_t *dir,
                                tiku_fat_dirent_t *out)
{
    uint8_t  sec[TIKU_FAT_SECTOR];
    char     lfn[TIKU_FAT_NAME_MAX];
    uint8_t  lfn_sum = 0u;
    int      lfn_have = 0;
    unsigned lfn_next = 0u;            /* ordinal the next piece must carry */

    if (fs == NULL || dir == NULL || out == NULL) { return TIKU_FAT_ERR_ARG; }
    lfn[0] = '\0';

    while (!dir->done) {
        const uint8_t *e;
        uint32_t lba;

        /* Bounded at 2^20 sector reads, so a directory chain that loops
         * (corruption or a crafted volume) ends the walk with ERR_CORRUPT. */
        if (dir->steps++ > (1u << 20)) { return TIKU_FAT_ERR_CORRUPT; }

        if (dir->sec_in_clus >= fs->sec_per_clus) {
            uint32_t next;
            tiku_fat_err_t rc = fat_next(fs, dir->clus, &next);
            if (rc != TIKU_FAT_OK) { return rc; }
            if (next == 0u) { dir->done = 1u; return TIKU_FAT_ERR_NOENT; }
            dir->clus = next;
            dir->sec_in_clus = 0u;
            dir->ent_in_sec  = 0u;
        }

        lba = clus_lba(fs, dir->clus) + dir->sec_in_clus;
        if (fs->read(lba, 1u, sec, fs->ctx) != 0) { return TIKU_FAT_ERR_IO; }

        while (dir->ent_in_sec < (TIKU_FAT_SECTOR / 32u)) {
            e = &sec[dir->ent_in_sec * 32u];
            dir->ent_in_sec++;

            if (e[0] == 0x00u) { dir->done = 1u; return TIKU_FAT_ERR_NOENT; }
            if (e[0] == 0xE5u) { lfn_have = 0; lfn[0] = '\0'; continue; }

            if ((e[11] & ATTR_LFN) == ATTR_LFN) {
                unsigned ord = e[0] & 0x1Fu;
                char part[13];
                unsigned k;
                if (ord == 0u || ord > 20u) { lfn_have = 0; continue; }
                if (e[0] & 0x40u) {            /* last piece: restart      */
                    lfn[0] = '\0';
                    lfn_sum = e[13];
                    lfn_have = 1;
                    lfn_next = ord;
                }
                /* Pieces count down to 1 with none missing; a piece out of
                 * sequence drops the long name, since a gap leaves unwritten
                 * bytes in it. */
                if (!lfn_have || e[13] != lfn_sum || ord != lfn_next) {
                    lfn_have = 0;
                    continue;
                }
                lfn_next = ord - 1u;
                lfn_chars(e, part);
                /* Pieces arrive last first, so piece `ord` occupies
                 * characters (ord-1)*13 onward. */
                for (k = 0u; k < 13u; k++) {
                    unsigned idx = ((ord - 1u) * 13u) + k;
                    if (idx >= TIKU_FAT_NAME_MAX - 1u) { break; }
                    lfn[idx] = part[k];
                    if (part[k] == '\0') { break; }
                }
                if ((e[0] & 0x40u) && (ord * 13u) < TIKU_FAT_NAME_MAX) {
                    /* A name that fills its last piece carries no NUL of
                     * its own, so the end is marked here. */
                    lfn[ord * 13u] = '\0';
                }
                continue;
            }

            if (e[11] & ATTR_VOLID) { lfn_have = 0; lfn[0] = '\0'; continue; }

            /*
             * A short entry ends the run.  The long name is used only if it
             * reached piece 1 and its checksum matches this entry; an orphaned,
             * incomplete or mismatched sequence is discarded and the 8.3 name
             * used instead.
             */
            if (lfn_have && lfn_next == 0u && lfn_sum == sfn_checksum(e) &&
                lfn[0] != '\0') {
                unsigned k;
                for (k = 0u; k < TIKU_FAT_NAME_MAX - 1u && lfn[k]; k++) {
                    out->name[k] = lfn[k];
                }
                out->name[k] = '\0';
            } else {
                sfn_render(e, out->name);
            }
            out->is_dir     = (e[11] & ATTR_DIR) ? 1u : 0u;
            out->size       = rd32(&e[28]);
            out->first_clus = (((uint32_t)rd16(&e[20]) << 16) |
                               rd16(&e[26])) & CLUS_MASK;
            return TIKU_FAT_OK;
        }

        dir->ent_in_sec = 0u;
        dir->sec_in_clus++;
    }
    return TIKU_FAT_ERR_NOENT;
}

/*---------------------------------------------------------------------------*/
/* PATHS                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Whether the first @p n chars of @p a equal all of @p b, ignoring
 *  ASCII case, as FAT names do. */
static int name_eq(const char *a, const char *b, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') { x = (char)(x - 32); }
        if (y >= 'a' && y <= 'z') { y = (char)(y - 32); }
        if (x != y) { return 0; }
    }
    return (b[n] == '\0');
}

/** @brief Resolve @p path, returning the entry it names. */
static tiku_fat_err_t path_walk(tiku_fat_t *fs, const char *path,
                                tiku_fat_dirent_t *out)
{
    uint32_t clus = fs->root_clus;
    const char *p = path;
    unsigned n;

    for (n = 0u; path[n] != '\0'; n++) {
        if (n >= TIKU_FAT_PATH_MAX) { return TIKU_FAT_ERR_ARG; }
    }

    out->is_dir     = 1u;
    out->size       = 0u;
    out->first_clus = clus;
    out->name[0]    = '/';
    out->name[1]    = '\0';

    while (*p == '/') { p++; }
    while (*p != '\0') {
        const char *seg = p;
        unsigned len = 0u;
        tiku_fat_dir_t dir;
        tiku_fat_dirent_t e;
        int found = 0;

        while (p[len] != '\0' && p[len] != '/') { len++; }
        if (len == 0u || len >= TIKU_FAT_NAME_MAX) {
            return TIKU_FAT_ERR_ARG;
        }
        p += len;
        while (*p == '/') { p++; }

        if (!out->is_dir) { return TIKU_FAT_ERR_NOTDIR; }

        dir_start(fs, clus, &dir);
        for (;;) {
            tiku_fat_err_t rc = tiku_fat_readdir(fs, &dir, &e);
            if (rc == TIKU_FAT_ERR_NOENT) { break; }
            if (rc != TIKU_FAT_OK)        { return rc; }
            if (name_eq(seg, e.name, len)) { found = 1; break; }
        }
        if (!found) { return TIKU_FAT_ERR_NOENT; }

        *out = e;
        /* A directory entry whose first cluster is 0 names the root (".."
         * in a first-level directory), as the on-disk format specifies. */
        clus = (e.first_clus == 0u) ? fs->root_clus : e.first_clus;
    }
    return TIKU_FAT_OK;
}

tiku_fat_err_t tiku_fat_opendir(tiku_fat_t *fs, const char *path,
                                tiku_fat_dir_t *dir)
{
    tiku_fat_dirent_t e;
    tiku_fat_err_t rc;

    if (fs == NULL || path == NULL || dir == NULL) { return TIKU_FAT_ERR_ARG; }
    rc = path_walk(fs, path, &e);
    if (rc != TIKU_FAT_OK) { return rc; }
    if (!e.is_dir)         { return TIKU_FAT_ERR_NOTDIR; }
    return dir_start(fs, e.first_clus ? e.first_clus : fs->root_clus, dir);
}

tiku_fat_err_t tiku_fat_open(tiku_fat_t *fs, const char *path,
                             tiku_fat_file_t *f)
{
    tiku_fat_dirent_t e;
    tiku_fat_err_t rc;

    if (fs == NULL || path == NULL || f == NULL) { return TIKU_FAT_ERR_ARG; }
    rc = path_walk(fs, path, &e);
    if (rc != TIKU_FAT_OK) { return rc; }
    if (e.is_dir)          { return TIKU_FAT_ERR_NOTDIR; }

    f->first_clus = e.first_clus;
    f->size       = e.size;
    f->pos        = 0u;
    f->clus       = e.first_clus;
    f->clus_idx   = 0u;
    return TIKU_FAT_OK;
}

/*---------------------------------------------------------------------------*/
/* READING                                                                   */
/*---------------------------------------------------------------------------*/

tiku_fat_err_t tiku_fat_seek(tiku_fat_t *fs, tiku_fat_file_t *f, uint32_t pos)
{
    uint32_t bytes_per_clus, want, i;

    if (fs == NULL || f == NULL) { return TIKU_FAT_ERR_ARG; }
    if (pos > f->size)           { return TIKU_FAT_ERR_ARG; }

    bytes_per_clus = (uint32_t)fs->sec_per_clus * fs->bytes_per_sec;
    /* On a boundary the cursor stays on the cluster ending there, as after a
     * sequential read: the next read follows the link, and a seek to the end
     * of a file that ends on a boundary needs no cluster past its last. */
    want = (pos == 0u) ? 0u : (pos - 1u) / bytes_per_clus;

    /* Each seek walks the chain from the first cluster, one FAT read per
     * cluster; tiku_fat_read() does not seek. */
    f->clus = f->first_clus;
    for (i = 0u; i < want; i++) {
        uint32_t next;
        tiku_fat_err_t rc = fat_next(fs, f->clus, &next);
        if (rc != TIKU_FAT_OK) { return rc; }
        if (next == 0u)        { return TIKU_FAT_ERR_CORRUPT; }
        f->clus = next;
    }
    f->clus_idx = want;
    f->pos      = pos;
    return TIKU_FAT_OK;
}

int32_t tiku_fat_read(tiku_fat_t *fs, tiku_fat_file_t *f, void *buf,
                      uint32_t n)
{
    uint8_t *dst = (uint8_t *)buf;
    uint32_t got = 0u;
    uint32_t bytes_per_clus;

    if (fs == NULL || f == NULL || buf == NULL) {
        return -(int32_t)TIKU_FAT_ERR_ARG;
    }
    if (f->pos >= f->size) { return 0; }
    if (n > (f->size - f->pos)) { n = f->size - f->pos; }
    bytes_per_clus = (uint32_t)fs->sec_per_clus * fs->bytes_per_sec;

    while (got < n) {
        uint8_t  sec[TIKU_FAT_SECTOR];
        uint32_t off_in_clus, sec_in_clus, off_in_sec, chunk, i;

        /*
         * Advance to the next cluster when the current one is exhausted.  The
         * read stops at the file size, so it follows no more links than the
         * size implies.  A chain that loops back within that count is caught
         * by tiku_fat_verify(), not here.
         */
        if ((f->pos % bytes_per_clus) == 0u && f->pos != 0u) {
            uint32_t next;
            tiku_fat_err_t rc = fat_next(fs, f->clus, &next);
            if (rc != TIKU_FAT_OK) { return -(int32_t)rc; }
            if (next == 0u) { return -(int32_t)TIKU_FAT_ERR_CORRUPT; }
            f->clus = next;
            f->clus_idx++;
        }

        off_in_clus = f->pos % bytes_per_clus;
        sec_in_clus = off_in_clus / fs->bytes_per_sec;
        off_in_sec  = off_in_clus % fs->bytes_per_sec;

        /*
         * Whole sectors go straight into the caller's buffer: a sector-aligned
         * read of at least a sector takes its whole sectors, up to the end of
         * the cluster, in one block command with no bounce copy.  A cluster is
         * contiguous, so crossing into the next one goes back around the loop
         * and through fat_next(), which is where fragmentation is handled.
         */
        if (off_in_sec == 0u && (n - got) >= fs->bytes_per_sec) {
            uint32_t want = (n - got) / fs->bytes_per_sec;
            uint32_t avail = fs->sec_per_clus - sec_in_clus;
            if (want > avail) { want = avail; }
            if (fs->read(clus_lba(fs, f->clus) + sec_in_clus, want,
                         &dst[got], fs->ctx) != 0) {
                return -(int32_t)TIKU_FAT_ERR_IO;
            }
            chunk = want * fs->bytes_per_sec;
            got    += chunk;
            f->pos += chunk;
            continue;
        }

        /* Otherwise a partial sector: bounce through a local. */
        chunk = fs->bytes_per_sec - off_in_sec;
        if (chunk > (n - got)) { chunk = n - got; }
        if (fs->read(clus_lba(fs, f->clus) + sec_in_clus, 1u, sec,
                     fs->ctx) != 0) {
            return -(int32_t)TIKU_FAT_ERR_IO;
        }
        for (i = 0u; i < chunk; i++) { dst[got + i] = sec[off_in_sec + i]; }
        got    += chunk;
        f->pos += chunk;
    }
    return (int32_t)got;
}

tiku_fat_err_t tiku_fat_verify(tiku_fat_t *fs, tiku_fat_file_t *f)
{
    uint32_t per_clus, want, clus, n = 0u;

    if (fs == NULL || f == NULL) { return TIKU_FAT_ERR_ARG; }
    if (f->size == 0u)           { return TIKU_FAT_OK; }

    per_clus = (uint32_t)fs->sec_per_clus * fs->bytes_per_sec;
    want     = (f->size + per_clus - 1u) / per_clus;
    clus     = f->first_clus;

    for (;;) {
        uint32_t next;
        tiku_fat_err_t rc;

        if (++n > want) { return TIKU_FAT_ERR_CORRUPT; }  /* too long/loops */
        rc = fat_next(fs, clus, &next);
        if (rc != TIKU_FAT_OK) { return rc; }
        if (next == 0u) { break; }                        /* end of chain   */
        clus = next;
    }
    return (n == want) ? TIKU_FAT_OK : TIKU_FAT_ERR_CORRUPT;  /* too short  */
}

tiku_fat_err_t tiku_fat_runs(tiku_fat_t *fs, tiku_fat_file_t *f,
                             int (*cb)(uint32_t lba, uint32_t nsec, void *ctx),
                             void *ctx)
{
    uint32_t clus, run_start, run_len, left, per_clus, steps = 0u;

    if (fs == NULL || f == NULL || cb == NULL) { return TIKU_FAT_ERR_ARG; }
    if (f->size == 0u) { return TIKU_FAT_OK; }

    per_clus  = fs->sec_per_clus;
    left      = (f->size + fs->bytes_per_sec - 1u) / fs->bytes_per_sec;
    clus      = f->first_clus;
    run_start = clus;
    run_len   = 0u;

    while (left > 0u) {
        uint32_t next;
        tiku_fat_err_t rc;

        /* Bounded by the file's own length in clusters, so a chain that
         * loops back cannot walk forever. */
        if (steps++ > (f->size / (per_clus * fs->bytes_per_sec)) + 2u) {
            return TIKU_FAT_ERR_CORRUPT;
        }

        run_len += per_clus;
        rc = fat_next(fs, clus, &next);
        if (rc != TIKU_FAT_OK) { return rc; }

        if (next != clus + 1u || run_len >= left) {
            uint32_t nsec = (run_len > left) ? left : run_len;
            if (cb(clus_lba(fs, run_start), nsec, ctx) != 0) {
                return TIKU_FAT_OK;          /* callback asked to stop      */
            }
            left      -= nsec;
            run_len    = 0u;
            run_start  = next;
        }
        if (next == 0u) { break; }
        clus = next;
    }
    return TIKU_FAT_OK;
}
