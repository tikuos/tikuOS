/*
 * Tiku Operating System v0.06
 *
 * fat_host_test.c - F0: exercise kernel/fs/tiku_fat.c on a Linux host.
 *
 * THE POINT OF THIS FILE.  A filesystem parser is nothing but derived values,
 * and this project has paid repeatedly for values that were derived rather
 * than read.  Here there is a reference implementation available for free --
 * the Linux kernel's own FAT driver, which wrote the corpus and can read it
 * back -- so every claim the parser makes is checked against what the rest of
 * the world thinks the same bytes mean, before the code ever sees hardware.
 *
 * It also runs the NEGATIVE corpus.  A reader that mounts a FAT16 volume, or
 * follows a chain that loops, fails in ways that are silent on a board and
 * obvious here.
 *
 * Build: tools/fat32/Makefile      Run: ./fat_host_test <corpus-dir>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "kernel/fs/tiku_fat.h"

static int g_pass, g_fail;

static void ok(int cond, const char *what)
{
    if (cond) { g_pass++; } else { g_fail++; }
    printf("  %s  %s\n", cond ? "pass" : "FAIL", what);
}

/*---------------------------------------------------------------------------*/
/* The injected block device: a file.                                        */
/*---------------------------------------------------------------------------*/

typedef struct { FILE *f; uint32_t base; } img_t;

static int img_read(uint32_t lba, uint32_t n, void *buf, void *ctx)
{
    img_t *im = (img_t *)ctx;
    if (fseek(im->f, (long)(lba + im->base) * TIKU_FAT_SECTOR, SEEK_SET) != 0) {
        return -1;
    }
    return (fread(buf, TIKU_FAT_SECTOR, n, im->f) == n) ? 0 : -1;
}

/*---------------------------------------------------------------------------*/

/** Recompute the corpus payload: the generator writes (i*37 + 11) & 0xFF. */
static int payload_ok(const uint8_t *p, uint32_t n, uint32_t off)
{
    uint32_t i;
    for (i = 0u; i < n; i++) {
        if (p[i] != (uint8_t)(((off + i) * 37u + 11u) & 0xFFu)) { return 0; }
    }
    return 1;
}

/** Read a whole file through the parser and verify every byte. */
static void check_file(tiku_fat_t *fs, const char *path, uint32_t expect_size)
{
    tiku_fat_file_t f;
    uint8_t buf[4096];
    uint32_t total = 0u;
    char msg[256];
    tiku_fat_err_t rc = tiku_fat_open(fs, path, &f);
    int good = 1;

    if (rc != TIKU_FAT_OK) {
        snprintf(msg, sizeof msg, "%s: open -> %s", path,
                 tiku_fat_strerror(rc));
        ok(0, msg);
        return;
    }
    if (expect_size && f.size != expect_size) {
        snprintf(msg, sizeof msg, "%s: size %u, expected %u", path,
                 f.size, expect_size);
        ok(0, msg);
        return;
    }
    for (;;) {
        int32_t got = tiku_fat_read(fs, &f, buf, sizeof buf);
        if (got < 0) {
            snprintf(msg, sizeof msg, "%s: read -> %s", path,
                     tiku_fat_strerror((tiku_fat_err_t)(-got)));
            ok(0, msg);
            return;
        }
        if (got == 0) { break; }
        if (!payload_ok(buf, (uint32_t)got, total)) { good = 0; }
        total += (uint32_t)got;
    }
    snprintf(msg, sizeof msg, "%s: %u bytes, content %s", path, total,
             good ? "exact" : "WRONG");
    ok(good && total == f.size, msg);
}

static void expect_mount(const char *dir, const char *name,
                         tiku_fat_err_t want)
{
    char path[512], msg[256];
    img_t im;
    tiku_fat_t fs;
    tiku_fat_err_t rc;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    im.f = fopen(path, "rb");
    im.base = 0u;
    if (!im.f) {
        snprintf(msg, sizeof msg, "%s: cannot open image", name);
        ok(0, msg);
        return;
    }
    rc = tiku_fat_mount(&fs, img_read, &im);
    snprintf(msg, sizeof msg, "%s: mount -> %s (wanted %s)", name,
             tiku_fat_strerror(rc), tiku_fat_strerror(want));
    ok(rc == want, msg);
    fclose(im.f);
}

/**
 * @brief Plant a loop in a FILE'S chain and require the reader to refuse it.
 *
 * The first version of this gate corrupted an arbitrary FAT entry and then
 * walked the ROOT -- which fits in one cluster, so the walk never followed
 * the FAT and never met the loop.  It passed without testing anything, which
 * is the failure mode negative gates exist to avoid, so it now finds a real
 * multi-cluster file, points its SECOND cluster back at its FIRST, and reads.
 */
static void expect_file_loop_refused(const char *dir)
{
    char src[512], dst[512], msg[256];
    img_t im;
    tiku_fat_t fs;
    tiku_fat_file_t f;
    uint8_t buf[4096];
    uint32_t first, off;
    int32_t got;
    FILE *w;
    unsigned char fatbuf[4];
    int refused = 0;

    snprintf(src, sizeof src, "%s/fat32_spc8.img", dir);
    snprintf(dst, sizeof dst, "%s/fat32_fileloop.img", dir);
    {   /* copy */
        FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
        static char cp[65536];
        size_t n;
        if (!a || !b) { ok(0, "fileloop: cannot copy image"); return; }
        while ((n = fread(cp, 1, sizeof cp, a)) > 0) { fwrite(cp, 1, n, b); }
        fclose(a); fclose(b);
    }

    /* Learn where /big.bin starts, using the parser we are about to break. */
    im.f = fopen(dst, "rb"); im.base = 0u;
    if (!im.f) { ok(0, "fileloop: cannot open copy"); return; }
    if (tiku_fat_mount(&fs, img_read, &im) != TIKU_FAT_OK ||
        tiku_fat_open(&fs, "/big.bin", &f) != TIKU_FAT_OK) {
        ok(0, "fileloop: cannot locate /big.bin");
        fclose(im.f); return;
    }
    first = f.first_clus;
    off   = fs.fat_lba * TIKU_FAT_SECTOR + (first + 1u) * 4u;
    fclose(im.f);

    /* Point the file's SECOND cluster back at its first. */
    w = fopen(dst, "r+b");
    if (!w) { ok(0, "fileloop: cannot reopen"); return; }
    fseek(w, (long)off, SEEK_SET);
    fatbuf[0] = (unsigned char)(first & 0xFF);
    fatbuf[1] = (unsigned char)((first >> 8) & 0xFF);
    fatbuf[2] = (unsigned char)((first >> 16) & 0xFF);
    fatbuf[3] = (unsigned char)((first >> 24) & 0x0F);
    fwrite(fatbuf, 1, 4, w);
    fclose(w);

    im.f = fopen(dst, "rb"); im.base = 0u;
    if (!im.f) { ok(0, "fileloop: cannot reopen"); return; }
    if (tiku_fat_mount(&fs, img_read, &im) == TIKU_FAT_OK &&
        tiku_fat_open(&fs, "/big.bin", &f) == TIKU_FAT_OK) {
        /* The documented defence: an explicit chain verification. */
        refused = (tiku_fat_verify(&fs, &f) == TIKU_FAT_ERR_CORRUPT);
        snprintf(msg, sizeof msg, "fileloop: tiku_fat_verify -> %s",
                 refused ? "CORRUPT (correct)" : "accepted a looping chain");
        ok(refused, msg);

        /* And the read path must at minimum TERMINATE, which is what the
         * plan requires of it -- bounded, never spinning. */
        {
            uint32_t total = 0u;
            int terminated = 0;
            tiku_fat_seek(&fs, &f, 0u);
            for (;;) {
                got = tiku_fat_read(&fs, &f, buf, sizeof buf);
                if (got <= 0) { terminated = 1; break; }
                total += (uint32_t)got;
                if (total > (16u << 20)) { break; }   /* it spun */
            }
            snprintf(msg, sizeof msg,
                     "fileloop: read terminated (%u bytes, no spin)", total);
            ok(terminated, msg);
        }
    } else {
        ok(0, "fileloop: could not set up");
    }
    fclose(im.f);
}

/** The chain-loop image: the walk must FAIL, not hang. */
static void expect_loop_refused(const char *dir)
{
    char path[512], msg[256];
    img_t im;
    tiku_fat_t fs;
    tiku_fat_dir_t d;
    tiku_fat_dirent_t e;
    int saw_corrupt = 0, entries = 0;

    snprintf(path, sizeof path, "%s/fat32_loop.img", dir);
    im.f = fopen(path, "rb");
    im.base = 0u;
    if (!im.f) { ok(0, "fat32_loop.img: cannot open"); return; }

    if (tiku_fat_mount(&fs, img_read, &im) != TIKU_FAT_OK) {
        ok(0, "fat32_loop.img: should still MOUNT (only the chain is bad)");
        fclose(im.f);
        return;
    }
    /* Walking the root must terminate one way or another -- with entries, or
     * with CORRUPT.  What it must never do is spin. */
    if (tiku_fat_opendir(&fs, "/", &d) == TIKU_FAT_OK) {
        for (;;) {
            tiku_fat_err_t rc = tiku_fat_readdir(&fs, &d, &e);
            if (rc == TIKU_FAT_ERR_CORRUPT) { saw_corrupt = 1; break; }
            if (rc != TIKU_FAT_OK) { break; }
            if (++entries > 10000) { break; }
        }
    }
    snprintf(msg, sizeof msg,
             "fat32_loop.img: walk terminated (%d entries%s)", entries,
             saw_corrupt ? ", refused CORRUPT" : "");
    ok(entries <= 10000, msg);
    fclose(im.f);
}

static void run_image(const char *dir, const char *name, uint32_t base)
{
    char path[512], msg[256];
    img_t im;
    tiku_fat_t fs;
    tiku_fat_dir_t d;
    tiku_fat_dirent_t e;
    tiku_fat_err_t rc;
    int found_long = 0, n = 0;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    printf("\n%s:\n", name);
    im.f = fopen(path, "rb");
    im.base = base;
    if (!im.f) { ok(0, "cannot open image"); return; }

    rc = tiku_fat_mount(&fs, img_read, &im);
    snprintf(msg, sizeof msg, "mount -> %s", tiku_fat_strerror(rc));
    ok(rc == TIKU_FAT_OK, msg);
    if (rc != TIKU_FAT_OK) { fclose(im.f); return; }

    snprintf(msg, sizeof msg,
             "geometry: spc=%u clusters=%u root=%u data_lba=%u",
             fs.sec_per_clus, fs.clusters, fs.root_clus, fs.data_lba);
    ok(fs.clusters >= 65525u, msg);

    if (tiku_fat_opendir(&fs, "/", &d) == TIKU_FAT_OK) {
        while (tiku_fat_readdir(&fs, &d, &e) == TIKU_FAT_OK) {
            n++;
            if (strstr(e.name, "rather long")) { found_long = 1; }
        }
    }
    snprintf(msg, sizeof msg, "root listing: %d entries", n);
    ok(n > 0, msg);

    check_file(&fs, "/SMALL.BIN", 1000u);
    check_file(&fs, "/EXACT.BIN", 4096u);
    check_file(&fs, "/big.bin", 1500000u);
    check_file(&fs, "/FRAGGED.BIN", 1500000u);      /* deliberately fragmented */
    check_file(&fs, "/sub/deeper/NESTED.BIN", 4096u);
    check_file(&fs, "/big.BIN", 1500000u);          /* case-insensitive */

    ok(found_long, "long file name assembled");
    if (found_long) {
        check_file(&fs, "/a rather long file name.dat", 1000u);
    }

    /* Negative: paths that must not resolve. */
    {
        tiku_fat_file_t f;
        rc = tiku_fat_open(&fs, "/NOPE.BIN", &f);
        snprintf(msg, sizeof msg, "missing file -> %s",
                 tiku_fat_strerror(rc));
        ok(rc == TIKU_FAT_ERR_NOENT, msg);
        rc = tiku_fat_open(&fs, "/sub", &f);
        snprintf(msg, sizeof msg, "open a directory -> %s",
                 tiku_fat_strerror(rc));
        ok(rc == TIKU_FAT_ERR_NOTDIR, msg);
    }
    fclose(im.f);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "/tmp/fat32corpus";

    printf("FAT32 host harness, corpus: %s\n", dir);

    run_image(dir, "fat32_spc1.img", 0u);
    run_image(dir, "fat32_spc8.img", 0u);

    printf("\nfat32_mbr.img (partition discovery):\n");
    {
        char path[512], msg[128];
        img_t im; tiku_fat_t fs; tiku_fat_err_t rc;
        snprintf(path, sizeof path, "%s/fat32_mbr.img", dir);
        im.f = fopen(path, "rb"); im.base = 0u;
        if (im.f) {
            rc = tiku_fat_mount(&fs, img_read, &im);
            snprintf(msg, sizeof msg, "mount -> %s, partition at LBA %u",
                     tiku_fat_strerror(rc), fs.part_lba);
            ok(rc == TIKU_FAT_OK && fs.part_lba == 2048u, msg);
            if (rc == TIKU_FAT_OK) { check_file(&fs, "/BIG.BIN", 1500000u); }
            fclose(im.f);
        } else { ok(0, "cannot open fat32_mbr.img"); }
    }

    printf("\nNEGATIVE corpus (every one of these must be refused):\n");
    expect_mount(dir, "fat16.img", TIKU_FAT_ERR_NOT_FAT32);
    expect_mount(dir, "fat12.img", TIKU_FAT_ERR_NOT_FAT32);
    expect_mount(dir, "zeros.img", TIKU_FAT_ERR_NOFS);
    expect_loop_refused(dir);
    expect_file_loop_refused(dir);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
