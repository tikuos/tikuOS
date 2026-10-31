/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mfs_block_arch.c - Extent translation and per-operation SD ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_mfs_block_arch.h"
#include <string.h>
#include <kernel/fs/tiku_mfs.h>
/** @brief Translate and transfer a checked extent through eight-sector DMA stages. */
static int transfer_blocks(tiku_p4_mfs_block_t *s,uint8_t write,uint64_t lba,
                           uint32_t n,void *data)
{
    if(!s||!s->active||!data||!n||lba>=s->block.sectors||n>s->block.sectors-lba)return -1;
    uint8_t *p=data;
    while(n){
        uint32_t take=n>8?8:n;
        int rc=tiku_p4_sdcard_blocks(&s->card,write,(uint32_t)(s->first+lba),take,p);
        if(rc)return rc;
        p+=take*512;lba+=take;n-=take;
    }
    return 0;
}
/** @brief Read volume-relative sectors. */
static int read_blocks(void *ctx,uint64_t lba,uint32_t n,void *p)
{ return transfer_blocks(ctx,0,lba,n,p); }
/** @brief Write volume-relative sectors inside the authorized extent. */
static int write_blocks(void *ctx,uint64_t lba,uint32_t n,const void *p)
{ return transfer_blocks(ctx,1,lba,n,(void *)p); }
/** @brief Complete card programming while its ownership lease remains held. */
static int sync_blocks(void *ctx)
{
    tiku_p4_mfs_block_t *s=ctx;
    return s&&s->active?tiku_p4_sdcard_sync(&s->card):-1;
}
int tiku_p4_mfs_block_init(tiku_p4_mfs_block_t *s,uint64_t first,uint64_t sectors,
                          const uint32_t cid[4],uint32_t hz)
{
    if(!s||!cid||!sectors||first>UINT32_MAX||sectors>(1ull<<32)-first||
       hz<400000||hz>20000000)return -1;
    memset(s,0,sizeof *s);s->first=first;s->hz=hz;memcpy(s->cid,cid,sizeof s->cid);
    s->block=(tiku_block_t){s,sectors,read_blocks,write_blocks,sync_blocks};return 0;
}
int tiku_p4_mfs_block_acquire(void *ctx)
{
    tiku_p4_mfs_block_t *s=ctx;if(!s||s->active)return TIKU_MFS_IO;
    int rc=tiku_p4_sdcard_open(&s->card);
    if(rc==TIKU_IPC_BUSY)return TIKU_MFS_BUSY;
    s->active=1;
    if(!rc&&(memcmp(s->cid,s->card.cid,sizeof s->cid)||s->first>=s->card.sectors||
       s->block.sectors>s->card.sectors-s->first))rc=TIKU_IPC_INVALID;
    if(!rc)rc=tiku_p4_sdcard_write_enable(&s->card,s->first,s->block.sectors);
    if(!rc)rc=tiku_p4_sdcard_clock(&s->card,s->hz);
    if(rc){int close=tiku_p4_sdcard_close();if(!close)s->active=0;if(close)return TIKU_MFS_IO;}
    return rc?TIKU_MFS_IO:0;
}
int tiku_p4_mfs_block_release(void *ctx)
{
    tiku_p4_mfs_block_t *s=ctx;if(!s||!s->active)return TIKU_IPC_INVALID;
    int rc=tiku_p4_sdcard_close();if(!rc)s->active=0;return rc;
}
