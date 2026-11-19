/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_card.c - Card discovery, bitmap ownership and native media mounting.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_card.h"
#include "tiku_media_vfs.h"
#if PLATFORM_ESP32S31
#include <arch/esp32s31/tiku_mfs_block_arch.h>
typedef tiku_s31_sdcard_t media_card_t;
typedef tiku_s31_mfs_block_t media_backend_t;
#define card_open tiku_s31_sdcard_open
#define card_read tiku_s31_sdcard_read
#define backend_init tiku_s31_mfs_block_init
#define backend_acquire tiku_s31_mfs_block_acquire
#define backend_release tiku_s31_mfs_block_release
#define CARD_BUSY TIKU_S31_SD_BUSY
#else
#include <arch/esp32p4/tiku_mfs_block_arch.h>
#include <boot/tiku_boot_c6.h>
typedef tiku_p4_sdcard_t media_card_t;
typedef tiku_p4_mfs_block_t media_backend_t;
#define card_open tiku_p4_sdcard_open
#define card_read tiku_p4_sdcard_read
#define backend_init tiku_p4_mfs_block_init
#define backend_acquire tiku_p4_mfs_block_acquire
#define backend_release tiku_p4_mfs_block_release
#define CARD_BUSY TIKU_IPC_BUSY
#endif
#include <kernel/memory/tiku_mem.h>
#include <string.h>
static tiku_mfs_t filesystem;
static tiku_media_store_t service;
static media_backend_t backend;
static tiku_arena_t bitmap_arena;
static size_t bitmap_capacity;
/** @brief Advance pending radio traffic through its owner before retrying storage. */
static void progress(void)
{
#if !PLATFORM_ESP32S31
    tiku_ipc_sdio_t *link=tiku_boot_c6_owner();
    if(link&&link->state==TIKU_SDIO_READY)tiku_ipc_sdio_step(link);
#endif
}
/** @brief Close the selected port's identification session. */
static int card_close(media_card_t *card)
{
#if PLATFORM_ESP32S31
    return tiku_s31_sdcard_close(card);
#else
    (void)card; return tiku_p4_sdcard_close();
#endif
}
/** @brief Normalize the card identity to SD register word order. */
static void card_identity(const media_card_t *card, uint32_t cid[4])
{
#if PLATFORM_ESP32S31
    tiku_s31_sdcard_identity(card,cid);
#else
    memcpy(cid,card->cid,16);
#endif
}
/** @brief Check live arena backing before any operation can access the bitmaps. */
static int acquire(void *ctx)
{
    tiku_mem_stats_t stats;
    if(tiku_arena_stats(&bitmap_arena,&stats)!=TIKU_MEM_OK||stats.used_bytes!=2*bitmap_capacity)
        return TIKU_MFS_IO;
    int rc=backend_acquire(ctx);
    for(unsigned n=0;rc==TIKU_MFS_BUSY&&n<128;n++){progress();rc=backend_acquire(ctx);}
    return rc;
}
/** @brief Read physical sectors while the identification lease is held read-only. */
static int discovery_read(void *ctx,uint64_t lba,uint32_t count,void *out)
{
    media_card_t *card=ctx;
    if(lba>=card->sectors||count>card->sectors-lba)return -1;
    for(uint32_t i=0;i<count;i++)if(card_read((uint32_t)(lba+i),(uint8_t *)out+512*i,card))return -1;
    return 0;
}
/** @brief Allocate reusable bitmap storage without a per-file allocation. */
static int buffers(size_t bytes)
{
    bytes=(bytes+7u)&~(size_t)7u;
    tiku_mem_stats_t stats;
    if(tiku_arena_stats(&bitmap_arena,&stats)==TIKU_MEM_OK){
        if(bitmap_capacity>=bytes&&stats.used_bytes==2*bitmap_capacity)return 0;
        if(tiku_arena_reset(&bitmap_arena)||tiku_arena_destroy(&bitmap_arena))return TIKU_MFS_BUSY;
    }
    memset(&bitmap_arena,0,sizeof bitmap_arena);bitmap_capacity=0;
    tiku_mem_request_t options={0};options.allocation_class=TIKU_MEM_TRANSIENT;
    #if PLATFORM_ESP32S31
    tiku_mem_tier_t tier=TIKU_MEM_PSRAM;
#else
    tiku_mem_tier_t tier=TIKU_MEM_SRAM;
#endif
    if(tiku_tier_arena_create_opts(&bitmap_arena,tier,2*bytes,91,&options))return TIKU_MFS_SPACE;
    if(!tiku_arena_alloc(&bitmap_arena,2*bytes))return TIKU_MFS_SPACE;
    bitmap_capacity=bytes;return 0;
}
int tiku_media_card_mount(const uint32_t *expected_cid,const uint8_t *format_uuid,uint32_t hz)
{
    if((!!expected_cid!=!!format_uuid)||hz<400000||hz>20000000)return TIKU_MFS_ARG;
    if(service.running||service.count||service.writer||service.collector||backend.active)return TIKU_MFS_BUSY;
    for(unsigned i=0;i<TIKU_MFS_READERS;i++)if(service.readers[i].token)return TIKU_MFS_BUSY;
    media_card_t card;int rc=card_open(&card);
    for(unsigned n=0;rc==CARD_BUSY&&n<128;n++){progress();rc=card_open(&card);}
    if(rc==CARD_BUSY)return TIKU_MFS_BUSY;
#if PLATFORM_ESP32S31
    if(rc)return TIKU_MFS_IO;
#endif
    if(rc)rc=TIKU_MFS_IO;
    uint32_t cid[4]; if(!rc)card_identity(&card,cid);
    uint8_t uuid[16];size_t bytes=rc?0:tiku_mfs_bitmap_bytes(card.sectors);
    if(!rc&&!bytes)rc=TIKU_MFS_ARG;
    if(!rc&&format_uuid){
        if(memcmp(expected_cid,cid,sizeof cid))rc=TIKU_MFS_STALE;
        else memcpy(uuid,format_uuid,16);
    }else if(!rc){
        tiku_block_t block={&card,card.sectors,discovery_read,NULL,NULL};
        rc=tiku_mfs_identify(&block,uuid);
    }
    int closed=card_close(&card);if(closed)return TIKU_MFS_IO;
    if(rc)return rc;
    if((rc=tiku_media_vfs_attach(NULL)))return rc;
    filesystem.mounted=0;
    if((rc=buffers(bytes)))return rc;
    if((rc=backend_init(&backend,0,card.sectors,cid,hz)))return rc;
    if((rc=tiku_media_store_init(&service,&filesystem,&backend.block,uuid,bitmap_arena.buf,
        bitmap_arena.buf+bitmap_capacity,bitmap_capacity,&backend,acquire,backend_release)))return rc;
    tiku_media_request_t request={0};request.op=format_uuid?TIKU_MEDIA_FORMAT:TIKU_MEDIA_MOUNT;
    request.client=1;request.permissions=TIKU_MEDIA_ADMIN;
    if((rc=tiku_media_card_request(&request)))return rc;
    return tiku_media_vfs_attach(&service);
}
tiku_media_store_t *tiku_media_card_store(void)
{ return filesystem.mounted&&!service.failed?&service:NULL; }
int tiku_media_card_request(tiku_media_request_t *request)
{
    if(service.count||service.running)return TIKU_MFS_BUSY;
    int rc=tiku_media_store_submit(&service,request);if(rc)return rc;
    rc=tiku_media_store_step(&service);
    if(rc==TIKU_MFS_BUSY){
        /* This wrapper owns its single queued request until completion or retry refusal. */
        if(service.count==1&&service.queue[0]==request)service.count=0;
        return rc;
    }
    return request->result;
}

#if PLATFORM_ESP32S31 && TIKU_MEDIA_ASYNC_ENABLE
#include "tiku_media_async.h"
/** @brief Bounded read query and card session occupy caller-provided storage. */
typedef struct {tiku_media_async_t queue;tiku_s31_mfs_async_t transport;} card_read_t;
/** @brief Validate bitmap backing before accepting a polled card session. */
static int read_acquire(void *ctx,void *backend_context)
{
    tiku_mem_stats_t stats;
    if(backend_context!=&backend||tiku_arena_stats(&bitmap_arena,&stats)!=TIKU_MEM_OK||
       stats.used_bytes!=2*bitmap_capacity)return TIKU_MFS_IO;
    return tiku_s31_mfs_async_acquire(ctx,backend_context);
}
/** @brief Begin one extent-relative sector read without waiting. */
static int read_sector(void *ctx,uint64_t lba,void *sector)
{ return tiku_s31_mfs_async_read(ctx,lba,1,sector); }
/** @brief Advance one card session phase. */
static int read_poll(void *ctx) { return tiku_s31_mfs_async_poll(ctx); }
/** @brief Release or cancel a retained read-only card session. */
static int read_close(void *ctx) { return tiku_s31_mfs_async_close(ctx); }
size_t tiku_media_card_read_size(void) { return sizeof(card_read_t); }
void tiku_media_card_read_init(void *memory)
{
    static const tiku_media_async_ops_t ops={read_acquire,read_sector,read_poll,read_close};
    card_read_t *r=memory;
    memset(r,0,sizeof *r);tiku_media_async_init(&r->queue,&ops,&r->transport);
}
int tiku_media_card_read_step(void *memory,tiku_media_store_t *s)
{
    if(!memory||s!=&service)return TIKU_MFS_ARG;
    return tiku_media_async_step(&((card_read_t *)memory)->queue,s);
}
int tiku_media_card_read_idle(const void *memory)
{ return memory&&!((const card_read_t *)memory)->queue.phase; }
int tiku_media_card_read_cancel(void *memory,tiku_media_store_t *s,tiku_media_request_t *request)
{
    if(!memory||s!=&service)return TIKU_MFS_ARG;
    return tiku_media_async_cancel(&((card_read_t *)memory)->queue,s,request);
}
#endif
