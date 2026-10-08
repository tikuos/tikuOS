/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_fat.c - "fat" command: read a PC-written FAT32 volume.
 *
 * The FAT32 parser, kernel/fs/tiku_fat.c, reads blocks through a callback;
 * this file supplies the one that reads the eMMC and the fat subcommands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_fat.h"
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#include <kernel/fs/tiku_fat.h>
#include "tiku.h"

#if TIKU_SHELL_CMD_FAT

#include <arch/ambiq/tiku_emmc_arch.h>
#include <kernel/cpu/tiku_hang.h>
/* `fat hash` needs SHA-256, which a build links only with the crypto kit:
 * the whole kit (TIKU_KIT_CRYPTO_ENABLE), or the SHA-256, HMAC and Base64
 * files of it that TIKU_BASIC_CRYPTO_ENABLE adds for BASIC.  The Makefile
 * sets either flag only with sources that provide SHA-256; without them
 * `fat hash` prints that the build has no SHA-256. */
#if (TIKU_KIT_CRYPTO_ENABLE + 0) || (TIKU_BASIC_CRYPTO_ENABLE + 0)
#define FAT_HASH 1
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>
#else
#define FAT_HASH 0
#endif
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
 * The reader asks for absolute LBAs, which the card takes as they are; the
 * partition offset is applied inside tiku_fat.
 */
static int fat_blk_read(uint32_t lba, uint32_t n, void *buf, void *ctx)
{
    (void)ctx;
    tiku_hang_checkin();          /* a big hash walks thousands of these    */
    return (tiku_emmc_read_blocks(lba, n, buf) == TIKU_EMMC_OK) ? 0 : -1;
}

static tiku_fat_t s_fs;
static uint8_t    s_mounted;

/** @brief Refuse unless the card is identified and USB does not own it. */
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

/** @brief Refuse unless a volume is mounted and fat_ready() passes. */
static int fat_need_mount(void)
{
    if (!s_mounted) {
        SHELL_PRINTF("fat: not mounted (`fat mount`)\n");
        return 0;
    }
    return fat_ready();
}

/*---------------------------------------------------------------------------*/
/* MOUNT                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief `fat mount`: mount the card's FAT32 volume, print its geometry. */
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
     * The partition LBA is the volume's first sector, from the MBR or 0 for
     * a card without one; the other values are computed from the BPB.
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
/* LISTING                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief `fat ls [path]`: list a directory of the mounted volume. */
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
/* READ A FILE THROUGH THE CHAIN                                             */
/*---------------------------------------------------------------------------*/

#if FAT_HASH
/* `fat hash` reads through a 4 KB buffer of its own. */
static uint8_t s_hashbuf[4096];

/** @brief `fat hash <path>`: verify the file's chain, then SHA-256 it. */
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
     * The read path stops at the file's size, so a cluster chain that loops
     * back on itself returns the right number of wrong bytes.
     * tiku_fat_verify() rejects such a chain before any byte is hashed.
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
#else
/** @brief `fat hash` in a build without SHA-256: say so. */
static void cmd_hash(const char *path)
{
    SHELL_PRINTF("fat hash %s: this build has no SHA-256 (the crypto kit)\n",
                 path);
}
#endif

/*---------------------------------------------------------------------------*/
/* EXTENTS                                                                   */
/*---------------------------------------------------------------------------*/

static unsigned s_run_n;
static uint32_t s_run_sec;

/** @brief Extent callback for `fat runs`: print the first eight, count all. */
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

/** @brief `fat runs <path>`: list a file's extents and count them. */
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
    /* A file in one run is contiguous on the card; `fat stage` stages each
     * run with its own tiku_emmc_stage_chunk() call. */
    SHELL_PRINTF("  %u runs covering %lu sectors -- %s\n", s_run_n,
                 (unsigned long)s_run_sec,
                 (s_run_n == 1u) ? "contiguous"
                                 : "fragmented (correctness unaffected)");
}

/*---------------------------------------------------------------------------*/
/* STAGE A FILE BY NAME                                                      */
/*---------------------------------------------------------------------------*/
/*
 * A file is staged by name into the PSRAM tier: tiku_fat_runs() turns the
 * file's cluster chain into extents, and the eMMC staging pipeline
 * (tiku_emmc_stage_open(), _chunk() and _close()) appends them to the PSRAM
 * image in order, from offset 0.
 */
#if (TIKU_DRV_PSRAM_ENABLE + 0)
#include <arch/ambiq/tiku_psram_arch.h>

static uint32_t s_stage_sec;
static unsigned s_stage_runs;
static int      s_stage_bad;

/*
 * tiku_shell_fat_locate() and tiku_shell_fat_stage_prefix() serve commands
 * that address a file by LBA; both return -1 unless a volume is mounted.
 */

static uint32_t s_loc_first, s_loc_runs;

/** @brief Extent callback for tiku_shell_fat_locate(): first LBA, run count. */
static int locate_cb(uint32_t lba, uint32_t nsec, void *ctx)
{
    (void)nsec; (void)ctx;
    if (s_loc_runs == 0u) { s_loc_first = lba; }
    s_loc_runs++;
    return 0;
}

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

/** @brief Extent callback that stages runs until the prefix is covered. */
static int prefix_cb(uint32_t lba, uint32_t nsec, void *ctx)
{
    (void)ctx;
    if (nsec > s_pfx_left) { nsec = s_pfx_left; }
    if (tiku_emmc_stage_chunk(lba, nsec) != TIKU_EMMC_OK) {
        s_pfx_left = 0xFFFFFFFFu;        /* nonzero: stage_prefix fails  */
        return 1;
    }
    s_pfx_left -= nsec;
    return (s_pfx_left == 0u) ? 1 : 0;   /* covered the prefix: stop     */
}

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
    if (tiku_emmc_stage_open() != TIKU_EMMC_OK) { return -1; }
    (void)tiku_fat_runs(&s_fs, &f, prefix_cb, 0);
    if (s_pfx_left != 0u) {
        (void)tiku_emmc_stage_close(0u, &src, &dst, &rd_us, &wr_us);
        return -1;
    }
    if (tiku_emmc_stage_close(nsec * 512u, &src, &dst, &rd_us, &wr_us)
        != TIKU_EMMC_OK) {
        return -1;                       /* PSRAM read-back failed       */
    }
    return (src == dst) ? 0 : -1;
}
/** @brief Extent callback for `fat stage`: queue each run to the pipeline. */
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

/** @brief `fat stage <path>`: copy a file into the PSRAM tier and check it. */
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
    /* tiku_fat_runs() stops at the file's size, so a cluster chain that
     * loops back on itself yields the right number of sectors from the wrong
     * clusters; tiku_fat_verify() rejects such a chain before a byte moves. */
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
    if (tiku_emmc_stage_open() != TIKU_EMMC_OK) {
        SHELL_PRINTF("  FAILED: the SRAM tier cannot lend the 512 KB"
                     " bounce buffer\n");
        return;
    }
    rc = tiku_fat_runs(&s_fs, &f, stage_cb, (void *)0);
    if (rc != TIKU_FAT_OK || s_stage_bad) {
        SHELL_PRINTF("  FAILED during the walk (%s)\n",
                     tiku_fat_strerror(rc));
        (void)tiku_emmc_stage_close(0u, &src, &dst, &rd_us, &wr_us);
        return;
    }
    /*
     * The pipeline moves and hashes whole sectors, so the read-back hash
     * covers s_stage_sec * 512 bytes, the span the source hash covers.
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
