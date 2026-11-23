/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_storage_arch.c - Durable SRAM mirrors and sector undo for the file store.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_storage_arch.h"
#include "tiku_storage_async.h"
#include <kernel/fs/tiku_tfs_async.h>
#include "tiku_psram_arch.h"
#include <kernel/memory/tiku_mem.h>
#include <kernel/memory/tiku_nvm_mirror.h>
#include <kernel/memory/tiku_nvm_region.h>
#include <string.h>
#define SECTOR 4096u
#define HEADER 16u
#define MAGIC 0x314e3354u
#define PENDING 0x31553354u
#ifndef TIKU_S31_STORAGE_VIEW
#define TIKU_S31_STORAGE_VIEW ((uint8_t *)TIKU_S31_PSRAM_BASE)
/* The durable bytes up to the last object; the page padding after it, which
 * the protection bounds include, is never read. */
extern uint8_t __uninit_start[], __uninit_used[];
#define LIVE __uninit_start
#define LIVE_BYTES ((size_t)(__uninit_used - __uninit_start))
#endif
/** @brief The fixed-offset undo record restored with durable SRAM. */
typedef struct { uint32_t pending, offset, crc, reserved; uint8_t data[SECTOR]; } undo_t;
#ifndef TIKU_S31_STORAGE_UNDO
static TIKU_DURABLE_ARCH undo_t undo_record;
#define TIKU_S31_STORAGE_UNDO undo_record
#endif
static uint32_t scratch[SECTOR / 4], sector[SECTOR / 4];
static int selected = -1;
static uint32_t generation;
static unsigned ready, attempted, failed, writing, flushing, provisioning;
static int restore_status = TIKU_NVM_RESTORE_CRC_FAIL;
static const uint32_t slots[] = {TIKU_S31_MIRROR_A, TIKU_S31_MIRROR_B};
/** @brief The sectors a mirror flush erases: those its header and the durable
 *         bytes reach.  The rest of the mirror is never read. */
static size_t mirror_span(void)
{ return (LIVE_BYTES + HEADER + SECTOR - 1u) & ~(size_t)(SECTOR - 1u); }
/** @brief Return one cached flash mirror, outside the allocatable PSRAM span. */
static uint8_t *mirror(unsigned slot)
{ return TIKU_S31_STORAGE_VIEW + TIKU_S31_REGION_BYTES + slot * TIKU_S31_MIRROR_BYTES; }
/** @brief Check erased bytes across the complete span. */
static int blank(const void *data, size_t bytes)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < bytes; i++) if (p[i] != 255u) return 0;
    return 1;
}
/** @brief Cover generation and length as well as the durable payload. */
static uint32_t checksum(const uint32_t *h)
{
    uint32_t crc = tiku_nvm_crc32_update(UINT32_MAX, h + 2, 8);
    return tiku_nvm_crc32_update(crc, (const uint8_t *)h + HEADER, h[2]) ^ UINT32_MAX;
}
/** @brief Validate geometry before reading or comparing generations. */
static int valid(unsigned slot)
{
    const uint32_t *h = (const uint32_t *)mirror(slot);
    return h[0] == MAGIC && h[2] <= TIKU_S31_MIRROR_BYTES - HEADER &&
           !(h[2] & 3u) && checksum(h) == h[1];
}
/** @brief Read flash through SRAM while the cache is suspended. */
static int load(uint32_t address, uint8_t *destination, size_t bytes)
{
    while (bytes) {
        size_t n = bytes < SECTOR ? bytes : SECTOR;
        if (tiku_s31_storage_io(0, address, scratch, n)) return -1;
        memcpy(destination, scratch, n);
        destination += n; address += n; bytes -= n;
    }
    return 0;
}
/** @brief Program aligned data through SRAM and verify it with ROM reads. */
static int program(uint32_t address, const void *source, size_t bytes)
{
    const uint8_t *src = source;
    while (bytes) {
        size_t n = bytes < 256u ? bytes : 256u;
        memcpy(scratch, src, n);
        if (tiku_s31_storage_io(2, address, scratch, n) ||
            tiku_s31_storage_io(0, address, scratch, n) || memcmp(scratch, src, n)) return -1;
        address += n; src += n; bytes -= n;
    }
    return 0;
}
int tiku_mem_arch_nvm_flush_status(void)
{
    if (!ready || failed || flushing || !tiku_s31_storage_context() ||
        LIVE_BYTES > TIKU_S31_MIRROR_BYTES - HEADER) return -1;
    if (selected >= 0) {
        const uint32_t *old = (const uint32_t *)mirror((unsigned)selected);
        if (!valid((unsigned)selected)) { failed = 1; return -1; }
        if (old[2] == LIVE_BYTES && !memcmp(old + 4, LIVE, LIVE_BYTES)) return 0;
    }
    flushing = 1;
    unsigned target = selected == 0 ? 1 : 0;
    uint32_t *h = (uint32_t *)mirror(target);
    memset(h, 255, TIKU_S31_MIRROR_BYTES);
    h[0] = MAGIC; h[2] = LIVE_BYTES; h[3] = generation + 1u;
    memcpy(h + 4, LIVE, LIVE_BYTES); h[1] = checksum(h);
    for (size_t at = 0; at < mirror_span(); at += SECTOR)
        if (tiku_s31_storage_io(1, slots[target] + at, NULL, 0)) goto fail;
    if (program(slots[target] + 4, h + 1, LIVE_BYTES + HEADER - 4) ||
        program(slots[target], h, 4)) goto fail;
    selected = (int)target; generation = h[3]; flushing = 0;
    return 0;
fail:
    flushing = 0; failed = 1;
    return -1;
}
void tiku_mem_arch_nvm_flush(void) { (void)tiku_mem_arch_nvm_flush_status(); }
int tiku_mem_arch_nvm_restore_status(void) { return restore_status; }
const uint8_t *tiku_mem_arch_durable(size_t *bytes)
{
    if (bytes) *bytes = 0;
    if (!ready || failed || selected < 0 || !valid((unsigned)selected)) return NULL;
    const uint32_t *h = (const uint32_t *)mirror((unsigned)selected);
    if (bytes) *bytes = h[2];
    return (const uint8_t *)(h + 4);
}
uint8_t *tiku_mem_arch_durable_live(size_t *bytes)
{
    if (bytes) *bytes = ready && !failed ? LIVE_BYTES : 0;
    return ready && !failed ? LIVE : NULL;
}
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src, tiku_mem_arch_size_t n)
{ memcpy(dst, src, n); }
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src, tiku_mem_arch_size_t n)
{ memmove(dst, src, n); }
void tiku_mem_arch_init(void) { }
/** @brief Preserve the old sector before an update that can tear adjacent data. */
static int journal_begin(size_t at)
{
    TIKU_S31_STORAGE_UNDO.offset = at;
    TIKU_S31_STORAGE_UNDO.reserved = 0;
    memcpy(TIKU_S31_STORAGE_UNDO.data, TIKU_S31_STORAGE_VIEW + at, SECTOR);
    TIKU_S31_STORAGE_UNDO.crc = tiku_nvm_crc32(TIKU_S31_STORAGE_UNDO.data, SECTOR);
    TIKU_S31_STORAGE_UNDO.pending = PENDING;
    return tiku_mem_arch_nvm_flush_status();
}
/** @brief Clear the recovery record before acknowledging the region update. */
static int journal_end(void)
{
    TIKU_S31_STORAGE_UNDO.pending = 0;
    return tiku_mem_arch_nvm_flush_status();
}
/** @brief Program whole affected sectors, preserving bytes outside each request. */
static int region_write(tiku_nvm_backend_t *be, size_t off, const void *data, size_t len)
{
    const uint8_t *src = data;
    uintptr_t address = (uintptr_t)data, base = (uintptr_t)TIKU_S31_STORAGE_VIEW;
    if (!be || be->base != TIKU_S31_STORAGE_VIEW || be->size != TIKU_S31_REGION_BYTES ||
        !ready || failed || writing || !tiku_s31_storage_context() || !data ||
        off > be->size || len > be->size - off || len > UINTPTR_MAX - address ||
        TIKU_S31_STORAGE_UNDO.pending) return -1;
    if (!len) return 0;
    size_t first = off & ~(size_t)(SECTOR - 1u);
    size_t end = (off + len + SECTOR - 1u) & ~(size_t)(SECTOR - 1u);
    if (address < base + end && address + len > base + first) return -1;
    writing = 1;
    while (len) {
        size_t at = off & ~(size_t)(SECTOR - 1u), within = off - at;
        size_t n = len < SECTOR - within ? len : SECTOR - within;
        memcpy(sector, TIKU_S31_STORAGE_VIEW + at, SECTOR);
        if (memcmp((uint8_t *)sector + within, src, n)) {
            memcpy((uint8_t *)sector + within, src, n);
            if (!provisioning && journal_begin(at)) goto fail;
            if (tiku_s31_storage_io(1, TIKU_S31_STORAGE_START + at, NULL, 0) ||
                program(TIKU_S31_STORAGE_START + at, sector, SECTOR)) goto fail;
            if (!provisioning && journal_end()) goto fail;
            memcpy(TIKU_S31_STORAGE_VIEW + at, sector, SECTOR);
        }
        off += n; src += n; len -= n;
    }
    writing = 0;
    return 0;
fail:
    writing = 0; failed = 1;
    return -1;
}
const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    static tiku_nvm_backend_t backend = {TIKU_S31_STORAGE_VIEW, TIKU_S31_REGION_BYTES, region_write, NULL, NULL};
    return ready && !failed ? &backend : NULL;
}
/** @brief Require the storage reservation to follow every factory partition. */
static int reservation(void)
{
    if (tiku_s31_storage_io(0, 0x8000, scratch, SECTOR)) return -1;
    unsigned count = 0;
    for (unsigned i = 0; i < 0xc00 / 4; i += 8) {
        unsigned magic = scratch[i] & 0xffffu;
        if (magic == 0xffffu || magic == 0xebebu) return count ? 0 : -1;
        if (magic != 0x50aau) return -1;
        uint32_t start = scratch[i + 1], bytes = scratch[i + 2];
        if (!bytes || start > TIKU_S31_STORAGE_START || bytes > TIKU_S31_STORAGE_START - start) return -1;
        count++;
    }
    return -1;
}
int tiku_s31_storage_init(void)
{
    if (!tiku_s31_storage_context()) return -1;
    if (attempted) return ready && !failed ? 0 : -1;
    attempted = 1;
    if (LIVE_BYTES > TIKU_S31_MIRROR_BYTES - HEADER || reservation() ||
        load(TIKU_S31_STORAGE_START, TIKU_S31_STORAGE_VIEW, TIKU_S31_STORAGE_SHADOW)) return -1;
    memset(LIVE, 0, LIVE_BYTES);
    for (unsigned i = 0; i < 2; i++) {
        const uint32_t *h = (const uint32_t *)mirror(i);
        if (!valid(i)) continue;
        if (selected >= 0) {
            uint32_t delta = h[3] - generation;
            const uint32_t *prior = (const uint32_t *)mirror((unsigned)selected);
            if (delta == 0x80000000u || (!delta &&
                (h[2] != prior[2] || memcmp(h + 4, prior + 4, h[2])))) return -1;
            if (!delta || delta >= 0x80000000u) continue;
        }
        selected = (int)i; generation = h[3];
    }
    if (selected < 0) {
        if (!blank(mirror(0), 2u * TIKU_S31_MIRROR_BYTES)) return -1;
        restore_status = TIKU_NVM_RESTORE_VIRGIN;
    } else {
        const uint32_t *h = (const uint32_t *)mirror((unsigned)selected);
        size_t n = h[2] < LIVE_BYTES ? h[2] : LIVE_BYTES;
        memcpy(LIVE, h + 4, n);
        restore_status = TIKU_NVM_RESTORE_V2_OK;
    }
    ready = 1;
    if (TIKU_S31_STORAGE_UNDO.pending) {
        uint32_t at = TIKU_S31_STORAGE_UNDO.offset;
        if (TIKU_S31_STORAGE_UNDO.pending != PENDING || TIKU_S31_STORAGE_UNDO.reserved ||
            (at & (SECTOR - 1u)) || at > TIKU_S31_REGION_BYTES - SECTOR ||
            tiku_nvm_crc32(TIKU_S31_STORAGE_UNDO.data, SECTOR) != TIKU_S31_STORAGE_UNDO.crc ||
            tiku_s31_storage_io(1, TIKU_S31_STORAGE_START + at, NULL, 0) ||
            program(TIKU_S31_STORAGE_START + at, TIKU_S31_STORAGE_UNDO.data, SECTOR) || journal_end()) {
            failed = 1; return -1;
        }
        memcpy(TIKU_S31_STORAGE_VIEW + at, TIKU_S31_STORAGE_UNDO.data, SECTOR);
    }
    provisioning = blank(TIKU_S31_STORAGE_VIEW, TIKU_S31_REGION_BYTES);
    return 0;
}
void tiku_s31_storage_seal(void) { provisioning = 0; }

#if TIKU_S31_STORAGE_ASYNC_ENABLE
/** @brief Region publication follows a committed mirror with a cleared undo record. */
enum {
    WRITE_VALIDATE=1, WRITE_VALIDATE_CRC, WRITE_PREPARE, WRITE_UNDO,
    WRITE_PENDING, WRITE_MIRROR, WRITE_MIRROR_CRC, WRITE_MIRROR_ERASE,
    WRITE_MIRROR_PROGRAM, WRITE_MIRROR_GATE, WRITE_ERASE, WRITE_PROGRAM,
    WRITE_CLEAR, WRITE_PUBLISH
};
static tiku_s31_storage_write_t *active_write;
/** @brief Retire the transaction; failed writes require recovery from flash at boot. */
static int write_retire(tiku_s31_storage_write_t *op, int result)
{
    if (result) failed=1;
    op->phase=0; op->data=NULL; active_write=NULL; writing=flushing=0;
    return result;
}
/** @brief Relock staged durable SRAM; the transaction owns its asynchronous mirror flush. */
static void write_relock(uint16_t saved) { tiku_mpu_arch_lock_nvm(saved); }
/** @brief Program and verify one page-bounded slice, retaining ownership across suspend. */
static int write_program(tiku_s31_storage_write_t *op, uint32_t address,
                          const uint8_t *source, size_t bytes)
{
    if (!op->pending && !op->verify && blank(source,bytes)) op->verify=1;
    if (op->verify) {
        if (tiku_s31_storage_io(0,address,scratch,bytes) || memcmp(scratch,source,bytes)) return -1;
        op->verify=0; return 0;
    }
    memcpy(scratch,source,bytes);
    int rc=tiku_s31_storage_slice(&op->flash,2,address,scratch,bytes,op->pending);
    if (rc<0) return -1;
    op->pending=rc!=0;
    if (!rc) op->verify=1;
    return 1;
}
/** @brief Choose a word-aligned transfer within the controller buffer and flash page. */
static size_t write_chunk(uint32_t address, size_t bytes)
{
    size_t n=bytes<64u ? bytes : 64u;
    return n<256u-(address&255u) ? n : 256u-(address&255u);
}
int tiku_s31_storage_write_begin(tiku_s31_storage_write_t *op, tiku_nvm_backend_t *be,
                                  size_t off, const void *data, size_t len)
{
    uintptr_t view=(uintptr_t)TIKU_S31_STORAGE_VIEW, base=be ? (uintptr_t)be->base : 0;
    uintptr_t input=(uintptr_t)data;
    if (!op || !be || !data || !len || !tiku_s31_storage_context() ||
        !ready || failed || selected<0 || provisioning || base<view || base-view>TIKU_S31_REGION_BYTES ||
        be->size>TIKU_S31_REGION_BYTES-(base-view) || off>be->size || len>be->size-off ||
        len>UINTPTR_MAX-input) return -1;
    if (writing || flushing || active_write) return -2;
    if (TIKU_S31_STORAGE_UNDO.pending) return -1;
    off += base-view;
    size_t first=off&~(size_t)(SECTOR-1u), end=(off+len+SECTOR-1u)&~(size_t)(SECTOR-1u);
    if (input<view+end && input+len>view+first) return -1;
    *op=(tiku_s31_storage_write_t){.data=data,.offset=off,.left=len,.phase=WRITE_VALIDATE};
    active_write=op; writing=flushing=1;
    return 1;
}
int tiku_s31_storage_write_poll(tiku_s31_storage_write_t *op)
{
    if (!op || op!=active_write || !tiku_s31_storage_context()) return -1;
    uint32_t *h=(uint32_t *)mirror(op->target);
    size_t n;
    int rc;
    uint16_t saved;
    switch (op->phase) {
    case WRITE_VALIDATE:
        if (selected<0) {op->phase=WRITE_PREPARE;return 1;}
        h=(uint32_t *)mirror((unsigned)selected);
        if (h[0]!=MAGIC || h[2]>TIKU_S31_MIRROR_BYTES-HEADER || (h[2]&3u)) break;
        op->cursor=0;op->crc=tiku_nvm_crc32_update(UINT32_MAX,h+2,8);
        op->phase=WRITE_VALIDATE_CRC;return 1;
    case WRITE_VALIDATE_CRC:
        h=(uint32_t *)mirror((unsigned)selected);
        n=h[2]-op->cursor;if(n>256)n=256;
        op->crc=tiku_nvm_crc32_update(op->crc,(uint8_t *)(h+4)+op->cursor,n);op->cursor+=n;
        if(op->cursor==h[2]) {
            if((op->crc^UINT32_MAX)!=h[1])break;
            op->phase=WRITE_PREPARE;
        }
        return 1;
    case WRITE_PREPARE:
        op->sector_offset=(uint32_t)(op->offset&~(size_t)(SECTOR-1u));
        op->within=op->offset-op->sector_offset;
        op->count=op->left<SECTOR-op->within ? op->left : SECTOR-op->within;
        memcpy(sector,TIKU_S31_STORAGE_VIEW+op->sector_offset,SECTOR);
        if (!memcmp((uint8_t *)sector+op->within,op->data,op->count)) {
            op->phase=WRITE_PUBLISH;return 1;
        }
        memcpy((uint8_t *)sector+op->within,op->data,op->count);
        op->cursor=0;op->crc=UINT32_MAX;op->phase=WRITE_UNDO;return 1;
    case WRITE_UNDO:
        n=SECTOR-op->cursor;if(n>256)n=256;
        saved=tiku_mpu_unlock_nvm();
        memcpy(TIKU_S31_STORAGE_UNDO.data+op->cursor,TIKU_S31_STORAGE_VIEW+op->sector_offset+op->cursor,n);
        write_relock(saved);
        op->crc=tiku_nvm_crc32_update(op->crc,TIKU_S31_STORAGE_UNDO.data+op->cursor,n);op->cursor+=n;
        if(op->cursor==SECTOR)op->phase=WRITE_PENDING;
        return 1;
    case WRITE_PENDING:
        saved=tiku_mpu_unlock_nvm();
        TIKU_S31_STORAGE_UNDO.offset=op->sector_offset;TIKU_S31_STORAGE_UNDO.reserved=0;
        TIKU_S31_STORAGE_UNDO.crc=op->crc^UINT32_MAX;TIKU_S31_STORAGE_UNDO.pending=PENDING;
        write_relock(saved);
        op->after_mirror=WRITE_ERASE;op->phase=WRITE_MIRROR;return 1;
    case WRITE_MIRROR:
        op->target=selected==0 ? 1u : 0u;h=(uint32_t *)mirror(op->target);
        h[0]=MAGIC;h[2]=LIVE_BYTES;h[3]=generation+1u;
        memcpy(h+4,LIVE,LIVE_BYTES);
        memset((uint8_t *)(h+4)+LIVE_BYTES,255,TIKU_S31_MIRROR_BYTES-HEADER-LIVE_BYTES);
        op->cursor=0;op->crc=tiku_nvm_crc32_update(UINT32_MAX,h+2,8);
        op->phase=WRITE_MIRROR_CRC;return 1;
    case WRITE_MIRROR_CRC:
        n=LIVE_BYTES-op->cursor;if(n>256)n=256;
        op->crc=tiku_nvm_crc32_update(op->crc,(uint8_t *)(h+4)+op->cursor,n);op->cursor+=n;
        if(op->cursor==LIVE_BYTES) {
            h[1]=op->crc^UINT32_MAX;op->cursor=0;op->phase=WRITE_MIRROR_ERASE;
        }
        return 1;
    case WRITE_MIRROR_ERASE:
        rc=tiku_s31_storage_slice(&op->flash,1,slots[op->target]+op->cursor,NULL,0,op->pending);
        if(rc<0)break;
        op->pending=rc!=0;
        if(!rc) {
            op->cursor+=SECTOR;
            if(op->cursor>=mirror_span()) {op->cursor=4;op->phase=WRITE_MIRROR_PROGRAM;}
        }
        return 1;
    case WRITE_MIRROR_PROGRAM:
        n=write_chunk(slots[op->target]+op->cursor,LIVE_BYTES+HEADER-op->cursor);
        rc=write_program(op,slots[op->target]+op->cursor,mirror(op->target)+op->cursor,n);
        if(rc<0)break;
        if(!rc) {
            op->cursor+=n;
            if(op->cursor==LIVE_BYTES+HEADER)op->phase=WRITE_MIRROR_GATE;
        }
        return 1;
    case WRITE_MIRROR_GATE:
        rc=write_program(op,slots[op->target],mirror(op->target),4);
        if(rc<0)break;
        if(!rc) {selected=(int)op->target;generation=h[3];op->cursor=0;op->phase=op->after_mirror;}
        return 1;
    case WRITE_ERASE:
        rc=tiku_s31_storage_slice(&op->flash,1,TIKU_S31_STORAGE_START+op->sector_offset,NULL,0,op->pending);
        if(rc<0)break;
        op->pending=rc!=0;
        if(!rc) {op->cursor=0;op->phase=WRITE_PROGRAM;}
        return 1;
    case WRITE_PROGRAM:
        n=write_chunk(TIKU_S31_STORAGE_START+op->sector_offset+op->cursor,SECTOR-op->cursor);
        rc=write_program(op,TIKU_S31_STORAGE_START+op->sector_offset+op->cursor,(uint8_t *)sector+op->cursor,n);
        if(rc<0)break;
        if(!rc) {op->cursor+=n;if(op->cursor==SECTOR)op->phase=WRITE_CLEAR;}
        return 1;
    case WRITE_CLEAR:
        saved=tiku_mpu_unlock_nvm();TIKU_S31_STORAGE_UNDO.pending=0;write_relock(saved);
        op->after_mirror=WRITE_PUBLISH;op->phase=WRITE_MIRROR;return 1;
    case WRITE_PUBLISH:
        memcpy(TIKU_S31_STORAGE_VIEW+op->sector_offset,sector,SECTOR);
        op->offset+=op->count;op->data+=op->count;op->left-=op->count;
        if(!op->left)return write_retire(op,0);
        op->phase=WRITE_PREPARE;return 1;
    default: break;
    }
    return write_retire(op,-1);
}

static tiku_s31_storage_write_t port_write;
/** @brief One sector-journalled write as a store request; a busy region is not accepted. */
static int port_begin(void *context, tiku_nvm_backend_t *be, size_t off, const void *data, size_t n)
{
    int rc = tiku_s31_storage_write_begin(context, be, off, data, n);
    return rc == -2 ? TFS_ERR_BUSY : rc < 0 ? TFS_ERR_IO : rc;
}
/** @brief Durable once the transaction has retired its undo record. */
static int port_poll(void *context)
{
    int rc = tiku_s31_storage_write_poll(context);
    return rc < 0 ? TFS_ERR_IO : rc;
}
const tiku_tfs_async_ops_t *tiku_tfs_async_port(void **context)
{
    static const tiku_tfs_async_ops_t ops = {port_begin, port_poll};
    if (context) *context = &port_write;
    return &ops;
}
#endif
