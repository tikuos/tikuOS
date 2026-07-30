/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_fat.c - "fat" command: read a PC-written FAT32 volume.
 *
 * The binding is the only hardware here: the parser knows nothing about eMMC and
 * this file supplies the small callback that connects it to one, which is what let
 * the parser be developed and regression-tested on a host first.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_fat.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_io.h>
#include <kernel/fs/tiku_fat.h>
#include "tiku.h"

#if TIKU_SHELL_CMD_FAT

#include <arch/ambiq/tiku_emmc_arch.h>
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>
#include <kernel/cpu/tiku_hang.h>
#if (TIKU_DRV_USB_ENABLE + 0)
#include <arch/ambiq/tiku_usb_arch.h>
#endif
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* THE BINDING                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bridge tiku_fat's block callback to the eMMC driver.
 *
 * The reader asks in ABSOLUTE LBAs, which is exactly what the card wants, so
 * there is nothing to translate -- the partition offset lives inside the
 * mounted volume, not here.
 */
static int fat_blk_read(uint32_t lba, uint32_t n, void *buf, void *ctx)
{
    (void)ctx;
    tiku_hang_checkin();          /* a big hash walks thousands of these    */
    return (tiku_emmc_read_blocks(lba, n, buf) == TIKU_EMMC_OK) ? 0 : -1;
}

static tiku_fat_t s_fs;
static uint8_t    s_mounted;

/** @brief Shared refusal: the card must be ours to read. */
static int fat_ready(void)
{
#if (TIKU_DRV_USB_ENABLE + 0)
    if (tiku_usb_msc_owns_emmc()) {
        SHELL_PRINTF("fat: refused -- the USB host owns the card."
                     "  `power usb off` first.\n");
        return 0;
    }
#endif
    if (tiku_emmc_capacity_blocks() == 0u) {
        SHELL_PRINTF("fat: the card is not identified (`power emmc id`)\n");
        return 0;
    }
    return 1;
}

static int fat_need_mount(void)
{
    if (!s_mounted) {
        SHELL_PRINTF("fat: not mounted (`fat mount`)\n");
        return 0;
    }
    return fat_ready();
}

/*---------------------------------------------------------------------------*/
/* F1 -- MOUNT                                                               */
/*---------------------------------------------------------------------------*/

static void cmd_mount(void)
{
    tiku_fat_err_t rc;

    s_mounted = 0u;
    if (!fat_ready()) { return; }

    rc = tiku_fat_mount(&s_fs, fat_blk_read, (void *)0);
    if (rc != TIKU_FAT_OK) {
        SHELL_PRINTF("fat mount: %s\n", tiku_fat_strerror(rc));
        if (rc == TIKU_FAT_ERR_NOT_FAT32) {
            SHELL_PRINTF("  (a FAT volume of the wrong width -- this reader"
                         " is FAT32 only, by decision)\n");
        }
        return;
    }
    s_mounted = 1u;

    /*
     * Print what was DERIVED, not what was claimed.  Every one of these came
     * out of the BPB's own arithmetic and can be checked against what the PC
     * says about the same volume -- which is the F1 gate.
     */
    SHELL_PRINTF("fat mount: ok\n");
    SHELL_PRINTF("  partition LBA %lu  fat LBA %lu  data LBA %lu\n",
                 (unsigned long)s_fs.part_lba, (unsigned long)s_fs.fat_lba,
                 (unsigned long)s_fs.data_lba);
    SHELL_PRINTF("  %u sectors/cluster (%lu B)  %u FATs of %lu sectors\n",
                 s_fs.sec_per_clus,
                 (unsigned long)s_fs.sec_per_clus * s_fs.bytes_per_sec,
                 s_fs.num_fats, (unsigned long)s_fs.fat_sectors);
    SHELL_PRINTF("  %lu clusters, root at cluster %lu, %lu total sectors\n",
                 (unsigned long)s_fs.clusters, (unsigned long)s_fs.root_clus,
                 (unsigned long)s_fs.total_sec);
    SHELL_PRINTF("  FAT32 confirmed by CLUSTER COUNT (%lu >= 65525), not by"
                 " the boot sector's label\n", (unsigned long)s_fs.clusters);
}

/*---------------------------------------------------------------------------*/
/* F2 -- LISTING                                                             */
/*---------------------------------------------------------------------------*/

static void cmd_ls(const char *path)
{
    tiku_fat_dir_t d;
    tiku_fat_dirent_t e;
    tiku_fat_err_t rc;
    unsigned n = 0u;

    if (!fat_need_mount()) { return; }
    rc = tiku_fat_opendir(&s_fs, path, &d);
    if (rc != TIKU_FAT_OK) {
        SHELL_PRINTF("fat ls %s: %s\n", path, tiku_fat_strerror(rc));
        return;
    }
    SHELL_PRINTF("fat ls %s:\n", path);
    for (;;) {
        rc = tiku_fat_readdir(&s_fs, &d, &e);
        if (rc == TIKU_FAT_ERR_NOENT) { break; }
        if (rc != TIKU_FAT_OK) {
            SHELL_PRINTF("  (walk stopped: %s)\n", tiku_fat_strerror(rc));
            break;
        }
        if (e.is_dir) {
            SHELL_PRINTF("  %-32s <DIR>\n", e.name);
        } else {
            SHELL_PRINTF("  %-32s %lu\n", e.name, (unsigned long)e.size);
        }
        n++;
        tiku_hang_checkin();
    }
    SHELL_PRINTF("  %u entries\n", n);
}

/*---------------------------------------------------------------------------*/
/* F3 -- READ A FILE THROUGH THE CHAIN                                       */
/*---------------------------------------------------------------------------*/

/* One sector at a time keeps this off the big buffers the other subsystems
 * own; the read path is FAT-bound, not buffer-bound. */
static uint8_t s_hashbuf[4096];

static void cmd_hash(const char *path)
{
    tiku_fat_file_t f;
    tiku_kits_crypto_sha256_ctx_t ctx;
    uint8_t digest[TIKU_KITS_CRYPTO_SHA256_DIGEST_SIZE];
    tiku_fat_err_t rc;
    uint32_t total = 0u;
    unsigned i;

    if (!fat_need_mount()) { return; }
    rc = tiku_fat_open(&s_fs, path, &f);
    if (rc != TIKU_FAT_OK) {
        SHELL_PRINTF("fat hash %s: %s\n", path, tiku_fat_strerror(rc));
        return;
    }

    /*
     * Verify the chain BEFORE trusting a byte of it.  The read path is
     * bounded by the file's size, so a loop that preserves the length would
     * otherwise return exactly the right number of WRONG bytes -- see
     * tiku_fat.h.  One extra chain walk is cheap next to hashing 54 MB.
     */
    rc = tiku_fat_verify(&s_fs, &f);
    if (rc != TIKU_FAT_OK) {
        SHELL_PRINTF("fat hash %s: chain %s -- refusing to read it\n",
                     path, tiku_fat_strerror(rc));
        return;
    }

    tiku_kits_crypto_sha256_init(&ctx);
    for (;;) {
        int32_t got = tiku_fat_read(&s_fs, &f, s_hashbuf, sizeof s_hashbuf);
        if (got < 0) {
            SHELL_PRINTF("fat hash %s: read %s at %lu\n", path,
                         tiku_fat_strerror((tiku_fat_err_t)(-got)),
                         (unsigned long)total);
            return;
        }
        if (got == 0) { break; }
        tiku_kits_crypto_sha256_update(&ctx, s_hashbuf, (size_t)got);
        total += (uint32_t)got;
        tiku_hang_checkin();
    }
    tiku_kits_crypto_sha256_final(&ctx, digest);

    SHELL_PRINTF("fat hash %s: %lu bytes\n", path, (unsigned long)total);
    SHELL_PRINTF("  sha256 ");
    for (i = 0u; i < TIKU_KITS_CRYPTO_SHA256_DIGEST_SIZE; i++) {
        SHELL_PRINTF("%02x", digest[i]);
    }
    SHELL_PRINTF("\n");
}

/*---------------------------------------------------------------------------*/
/* EXTENTS -- what F4's staging path will hand to the block layer            */
/*---------------------------------------------------------------------------*/

static unsigned s_run_n;
static uint32_t s_run_sec;

static int run_cb(uint32_t lba, uint32_t nsec, void *ctx)
{
    (void)ctx;
    if (s_run_n < 8u) {
        SHELL_PRINTF("  run %u: LBA %lu x %lu sectors (%lu KB)\n",
                     s_run_n, (unsigned long)lba, (unsigned long)nsec,
                     (unsigned long)(nsec / 2u));
    } else if (s_run_n == 8u) {
        SHELL_PRINTF("  ...\n");
    }
    s_run_n++;
    s_run_sec += nsec;
    tiku_hang_checkin();
    return 0;
}

static void cmd_runs(const char *path)
{
    tiku_fat_file_t f;
    tiku_fat_err_t rc;

    if (!fat_need_mount()) { return; }
    rc = tiku_fat_open(&s_fs, path, &f);
    if (rc != TIKU_FAT_OK) {
        SHELL_PRINTF("fat runs %s: %s\n", path, tiku_fat_strerror(rc));
        return;
    }
    s_run_n = 0u; s_run_sec = 0u;
    SHELL_PRINTF("fat runs %s (%lu bytes):\n", path, (unsigned long)f.size);
    rc = tiku_fat_runs(&s_fs, &f, run_cb, (void *)0);
    if (rc != TIKU_FAT_OK) {
        SHELL_PRINTF("  walk: %s\n", tiku_fat_strerror(rc));
        return;
    }
    /* Fragmentation, stated as a number rather than a feeling: one run is
     * contiguous, and every extra run is a seam the staging path must chunk
     * around. */
    SHELL_PRINTF("  %u runs covering %lu sectors -- %s\n", s_run_n,
                 (unsigned long)s_run_sec,
                 (s_run_n == 1u) ? "contiguous"
                                 : "fragmented (correctness unaffected)");
}

/*---------------------------------------------------------------------------*/
/* F4 -- STAGE A MODEL BY NAME                                               */
/*---------------------------------------------------------------------------*/
/*
 * The flow this whole arc was for: a PC drags a model onto the card, and the
 * board loads it by FILENAME into the working tier.  Nothing here knows an
 * LBA -- the chain walker turns the name into extents and the eMMC pipeline
 * appends them to the PSRAM image in order.
 */
#if (TIKU_DRV_PSRAM_ENABLE + 0)
#include <arch/ambiq/tiku_psram_arch.h>

static uint32_t s_stage_sec;
static unsigned s_stage_runs;
static int      s_stage_bad;

/*
 * Helpers for other commands that address a file by LBA (the llm command
 * streams weights straight off the card).  Both require a mounted volume.
 */

static uint32_t s_loc_first, s_loc_runs;

static int locate_cb(uint32_t lba, uint32_t nsec, void *ctx)
{
    (void)nsec; (void)ctx;
    if (s_loc_runs == 0u) { s_loc_first = lba; }
    s_loc_runs++;
    return 0;
}

/** @brief First LBA, byte size and extent count of a file. */
int tiku_shell_fat_locate(const char *path, uint32_t *lba0, uint32_t *size,
                          uint32_t *nruns)
{
    tiku_fat_file_t f;
    if (!s_mounted) { return -1; }
    if (tiku_fat_open(&s_fs, path, &f) != TIKU_FAT_OK) { return -1; }
    if (tiku_fat_verify(&s_fs, &f) != TIKU_FAT_OK) { return -1; }
    s_loc_first = 0u; s_loc_runs = 0u;
    if (tiku_fat_runs(&s_fs, &f, locate_cb, 0) != TIKU_FAT_OK) { return -1; }
    *lba0 = s_loc_first; *size = f.size; *nruns = s_loc_runs;
    return 0;
}

static uint32_t s_pfx_left;

static int prefix_cb(uint32_t lba, uint32_t nsec, void *ctx)
{
    (void)ctx;
    if (nsec > s_pfx_left) { nsec = s_pfx_left; }
    if (tiku_emmc_stage_chunk(lba, nsec) != TIKU_EMMC_OK) {
        s_pfx_left = 0xFFFFFFFFu;        /* poison: caller sees failure */
        return 1;
    }
    s_pfx_left -= nsec;
    return (s_pfx_left == 0u) ? 1 : 0;   /* covered the prefix: stop     */
}

/**
 * @brief Stage the first @p bytes of a file into the PSRAM tier base.
 *
 * The same verified eMMC->SRAM->PSRAM pipeline as `fat stage`, walked only
 * until the prefix is covered.  Rounds up to whole sectors.
 */
int tiku_shell_fat_stage_prefix(const char *path, uint32_t bytes)
{
    tiku_fat_file_t f;
    uint32_t nsec = (bytes + 511u) / 512u;
    uint32_t src = 0u, dst = 0u, rd_us = 0u, wr_us = 0u;

    if (!s_mounted || bytes == 0u) { return -1; }
    if (tiku_fat_open(&s_fs, path, &f) != TIKU_FAT_OK) { return -1; }
    if (tiku_fat_verify(&s_fs, &f) != TIKU_FAT_OK) { return -1; }
    if ((uint64_t)nsec * 512u > f.size + 511u) { return -1; }
    s_pfx_left = nsec;
    tiku_emmc_stage_open();
    (void)tiku_fat_runs(&s_fs, &f, prefix_cb, 0);
    if (s_pfx_left != 0u) {
        (void)tiku_emmc_stage_close(0u, &src, &dst, &rd_us, &wr_us);
        return -1;
    }
    if (tiku_emmc_stage_close(nsec * 512u, &src, &dst, &rd_us, &wr_us)
        != TIKU_EMMC_OK) {
        return -1;                       /* readback hash mismatch       */
    }
    return (src == dst) ? 0 : -1;
}
static int stage_cb(uint32_t lba, uint32_t nsec, void *ctx)
{
    (void)ctx;
    if (tiku_emmc_stage_chunk(lba, nsec) != TIKU_EMMC_OK) {
        s_stage_bad = 1;
        return 1;                  /* stop the walk                        */
    }
    s_stage_sec += nsec;
    s_stage_runs++;
    return 0;
}

static void cmd_stage(const char *path)
{
    tiku_fat_file_t f;
    tiku_fat_err_t rc;
    uint32_t src = 0u, dst = 0u, rd_us = 0u, wr_us = 0u, bytes;

    if (!fat_need_mount()) { return; }
    if (!tiku_psram_powered() || tiku_psram_asleep()) {
        SHELL_PRINTF("fat stage: psram not up (`power psram up`)\n");
        return;
    }
    rc = tiku_fat_open(&s_fs, path, &f);
    if (rc != TIKU_FAT_OK) {
        SHELL_PRINTF("fat stage %s: %s\n", path, tiku_fat_strerror(rc));
        return;
    }
    /* Verify the chain before moving a byte, for the reason in tiku_fat.h. */
    rc = tiku_fat_verify(&s_fs, &f);
    if (rc != TIKU_FAT_OK) {
        SHELL_PRINTF("fat stage %s: chain %s -- refusing\n", path,
                     tiku_fat_strerror(rc));
        return;
    }
    if (f.size == 0u) { SHELL_PRINTF("fat stage: empty file\n"); return; }
    if (f.size > TIKU_PSRAM_SIZE_BYTES) {
        SHELL_PRINTF("fat stage %s: %lu bytes does not fit the %lu MB tier\n",
                     path, (unsigned long)f.size,
                     (unsigned long)(TIKU_PSRAM_SIZE_BYTES / (1024u*1024u)));
        return;
    }

    SHELL_PRINTF("fat stage %s: %lu bytes\n", path, (unsigned long)f.size);
    s_stage_sec = 0u; s_stage_runs = 0u; s_stage_bad = 0;
    tiku_emmc_stage_open();
    rc = tiku_fat_runs(&s_fs, &f, stage_cb, (void *)0);
    if (rc != TIKU_FAT_OK || s_stage_bad) {
        SHELL_PRINTF("  FAILED during the walk (%s)\n",
                     tiku_fat_strerror(rc));
        (void)tiku_emmc_stage_close(0u, &src, &dst, &rd_us, &wr_us);
        return;
    }
    /*
     * Whole sectors, NOT the file size. The staging pipeline hashes every
     * byte it moves, and it moves whole sectors; clamping to the file size
     * here made the two hashes cover regions 224 bytes apart and report a
     * MISMATCH on a perfectly good transfer. Only a file whose length is an
     * exact sector multiple hid it.
     */
    bytes = s_stage_sec * 512u;

    if (tiku_emmc_stage_close(bytes, &src, &dst, &rd_us, &wr_us)
        != TIKU_EMMC_OK) {
        SHELL_PRINTF("  FAILED reading the staged image back\n");
        return;
    }

    {
        unsigned long total = rd_us + wr_us;
        SHELL_PRINTF("  %u extent%s, %lu sectors\n", s_stage_runs,
                     (s_stage_runs == 1u) ? "" : "s",
                     (unsigned long)s_stage_sec);
        SHELL_PRINTF("  card->sram %8lu us   sram->psram %8lu us\n",
                     (unsigned long)rd_us, (unsigned long)wr_us);
        SHELL_PRINTF("  end-to-end %8lu us = %lu.%02lu MB/s\n", total,
                     total ? (unsigned long)(((uint64_t)bytes * 100u /
                              total) / 100u) : 0ul,
                     total ? (unsigned long)(((uint64_t)bytes * 100u /
                              total) % 100u) : 0ul);
        SHELL_PRINTF("  fnv src %08lx dst %08lx -- %s\n",
                     (unsigned long)src, (unsigned long)dst,
                     (src == dst) ? "bit-exact in the tier"
                                  : "MISMATCH");
        SHELL_PRINTF("  staged at psram offset 0; `power psram up` mapped it"
                     " as the TIKU_MEM_PSRAM tier\n");
    }
}
#endif /* TIKU_DRV_PSRAM_ENABLE */

/*---------------------------------------------------------------------------*/

void tiku_shell_cmd_fat(uint8_t argc, const char *argv[])
{
    if (argc < 2) {
        SHELL_PRINTF("Usage: fat mount | ls [path] | hash <path>"
                     " | runs <path> | stage <path>\n");
        return;
    }
    if (argv[1][0] == 'm') { cmd_mount(); return; }
    if (argv[1][0] == 'l') { cmd_ls((argc >= 3) ? argv[2] : "/"); return; }
    if (argv[1][0] == 'h') {
        if (argc < 3) { SHELL_PRINTF("Usage: fat hash <path>\n"); return; }
        cmd_hash(argv[2]);
        return;
    }
    if (argv[1][0] == 'r') {
        if (argc < 3) { SHELL_PRINTF("Usage: fat runs <path>\n"); return; }
        cmd_runs(argv[2]);
        return;
    }
#if (TIKU_DRV_PSRAM_ENABLE + 0)
    if (argv[1][0] == 's') {
        if (argc < 3) { SHELL_PRINTF("Usage: fat stage <path>\n"); return; }
        cmd_stage(argv[2]);
        return;
    }
#endif
    SHELL_PRINTF("Usage: fat mount | ls | hash | runs | stage <path>\n");
}

#endif /* TIKU_SHELL_CMD_FAT */
