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
extern uint8_t __uninit_start[], __uninit_end[];
#define LIVE __uninit_start
#define LIVE_BYTES ((size_t)(__uninit_end - __uninit_start))
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
    for (unsigned at = 0; at < TIKU_S31_MIRROR_BYTES; at += SECTOR)
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
