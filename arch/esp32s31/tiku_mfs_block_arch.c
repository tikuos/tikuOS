/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mfs_block_arch.c - Extent translation and per-operation SPI card ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_mfs_block_arch.h"
#include "tiku_watchdog_arch.h"
#include <kernel/fs/tiku_mfs.h>
#include <string.h>
void tiku_s31_sdcard_identity(const tiku_s31_sdcard_t *card, uint32_t cid[4])
{
    for (unsigned i = 0; i < 4; i++) {
        const uint8_t *p = card->cid + 4*i;
        cid[3-i] = (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
    }
}
/** @brief Transfer checked volume-relative sectors while the card lease is held. */
static int transfer(tiku_s31_mfs_block_t *s, unsigned write, uint64_t lba,
                    uint32_t count, void *data)
{
    if (!s || !s->active || !data || !count || count > UINT32_MAX/512 ||
        lba >= s->block.sectors || count > s->block.sectors-lba) return TIKU_MFS_ARG;
    uint8_t *p = data;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t physical = (uint32_t)(s->first+lba+i);
        int rc = write ? tiku_s31_sdcard_write(physical, p, &s->card) :
                         tiku_s31_sdcard_read(physical, p, &s->card);
        if (rc) return TIKU_MFS_IO;
        tiku_watchdog_arch_kick();
        p += 512;
    }
    return 0;
}
/** @brief Read sectors relative to the authorized extent. */
static int read_blocks(void *s, uint64_t lba, uint32_t n, void *p)
{ return transfer(s, 0, lba, n, p); }
/** @brief Write sectors relative to the authorized extent. */
static int write_blocks(void *s, uint64_t lba, uint32_t n, const void *p)
{ return transfer(s, 1, lba, n, (void *)p); }
/** @brief Complete programming and check status while the lease remains held. */
static int sync_blocks(void *ctx)
{
    tiku_s31_mfs_block_t *s = ctx;
    return s && s->active && !tiku_s31_sdcard_sync(&s->card) ? 0 : TIKU_MFS_IO;
}
int tiku_s31_mfs_block_init(tiku_s31_mfs_block_t *s, uint64_t first, uint64_t sectors,
                           const uint32_t cid[4], uint32_t hz)
{
    if (!s || !cid || !sectors || first > UINT32_MAX || sectors > (1ull<<32)-first ||
        hz < 400000 || hz > 20000000) return TIKU_MFS_ARG;
    memset(s, 0, sizeof *s); s->first = first; s->hz = hz;
    memcpy(s->cid, cid, sizeof s->cid);
    s->block = (tiku_block_t){s, sectors, read_blocks, write_blocks, sync_blocks};
    return 0;
}
int tiku_s31_mfs_block_acquire(void *ctx)
{
    tiku_s31_mfs_block_t *s = ctx;
    if (!s || s->active) return TIKU_MFS_IO;
    int rc = tiku_s31_sdcard_open(&s->card);
    if (rc == TIKU_S31_SD_BUSY) return TIKU_MFS_BUSY;
    if (rc) return TIKU_MFS_IO;
    s->active = 1;
    uint32_t cid[4]; tiku_s31_sdcard_identity(&s->card, cid);
    if (memcmp(cid, s->cid, sizeof cid) || s->first >= s->card.sectors ||
        s->block.sectors > s->card.sectors-s->first) rc = TIKU_MFS_STALE;
    if (!rc) rc = tiku_s31_sdcard_clock(&s->card, s->hz);
    if (!rc) rc = tiku_s31_sdcard_write_enable(&s->card, s->first, s->block.sectors);
    if (rc) { (void)tiku_s31_mfs_block_release(s); return TIKU_MFS_IO; }
    return 0;
}
int tiku_s31_mfs_block_release(void *ctx)
{
    tiku_s31_mfs_block_t *s = ctx;
    if (!s || !s->active) return TIKU_MFS_ARG;
    int rc = tiku_s31_sdcard_close(&s->card);
    if (!rc) s->active = 0;
    return rc ? TIKU_MFS_IO : 0;
}
