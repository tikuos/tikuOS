/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_display_arch.c - Korvo RGB565 scanout and paired frame handoff.
 * Register fields follow ESP-IDF 4d59230 S31 LCDCAM and AXI DMA definitions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <hal/tiku_clock_hal.h>
#include <interfaces/display/tiku_display_arch.h>
#include <interfaces/gpio/tiku_gpio_owner.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/threads/tiku_thread.h>
#include "tiku_display_arch.h"
#include "tiku_dma_arch.h"
#include "tiku_psram_arch.h"
#include "tiku_runtime_arch.h"

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define LCD 0x20396000u
#define DMA 0x20348000u
#define BYTES 768000u
#define CHUNK 4032u
#define COUNT ((BYTES + CHUNK - 1u) / CHUNK)
#define USER ((1u << 13) | (1u << 19) | (1u << 24))
#define MISC ((1u << 3) | (1u << 4) | (1u << 25) | (1u << 26))
#define MUX(n) (0x20582000u + 4u * (n))
#define CONFIG(n) (0x205830f4u + 4u * (n))
#define ROUTE(n) (0x20583af4u + 4u * (n))
#define ENABLE(n) ((n) < 32 ? 0x20583034u : 0x20583040u)
#define SET(n) ((n) < 32 ? 0x20583038u : 0x20583044u)
#define CLR(n) ((n) < 32 ? 0x2058303cu : 0x20583048u)
#define USB_PADS 0x20391018u
#define USB_PAD_MASK 0x5f00u
#define BIT(n) (1u << ((n) & 31u))

/** @brief AXI DMA descriptor with eight-byte alignment and a reserved tail. */
typedef struct { uint32_t control, buffer, next, reserved; } descriptor_t;
_Static_assert(sizeof(descriptor_t) == 16, "AXI descriptor size");
static volatile descriptor_t descriptors[2][COUNT] __attribute__((aligned(64)));
static const uint8_t pins[] = {8,9,10,11,12,13,14,15,16,17,18,19,33,34,35,36,40,43,44,45,38};
static const uint16_t signals[] = {154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,149,151,152,153,256};
static uint32_t saved[21][4], saved_output, saved_usb;
static unsigned usb_changed;
static tiku_arena_t scanout;
static tiku_display_t *active;
static tiku_s31_display_stats_t counters;
static unsigned claimed, hardware, shown_ring, paired, faulted;
static void *shown_buffer;
static uint32_t last_eof;
static int context(void);
static void halt(void);
static int start(void);
static const char owner[] = "lcd";
#ifdef TIKU_S31_DISPLAY_TEST
static unsigned fault;
void tiku_s31_display_test_fault(unsigned mode) { fault = mode; }
uint32_t tiku_s31_display_test_mismatches(void)
{
    if (!context() || !active) return UINT32_MAX;
    halt();
    const uint32_t *a = (const uint32_t *)active->fb;
    const uint32_t *b = (const uint32_t *)shown_buffer;
    uint32_t n = 0;
    for (unsigned i = 0; i < BYTES / 4; i++) n += a[i] != b[i];
    return start() ? UINT32_MAX : n;
}
#endif

/** @brief Restrict register and allocator operations to kernel foreground. */
static int context(void) { return !tiku_s31_in_trap && tiku_thread_in_kernel(); }

/** @brief Check the full 64-byte-aligned RGB565 surface in attached PSRAM. */
static int surface(const tiku_display_t *d)
{
    if (!d || !d->fb || d->w != 800 || d->h != 480 || d->stride != 1600 ||
        d->fmt != TIKU_DISPLAY_FMT_RGB565) return 0;
    uintptr_t a = (uintptr_t)d->fb;
    uint32_t size = tiku_s31_psram_size();
    return !(a & 63u) && a >= TIKU_S31_PSRAM_BASE && size >= BYTES &&
           a - TIKU_S31_PSRAM_BASE <= size - BYTES;
}

int tiku_s31_display_poll(tiku_s31_display_stats_t *stats)
{
    if (!context() || !active || !hardware) return TIKU_DISPLAY_ERR_STATE;
#ifdef TIKU_S31_DISPLAY_TEST
    if (fault == 2) { REG(DMA + 0x158) |= 1u; fault = 0; }
#endif
    uint32_t lcd = REG(LCD + 0x68), dma = REG(DMA + 0x138);
    counters.vsync += (lcd & 1u) != 0;
    counters.eof += (dma & 2u) != 0;
    if (dma & 2u) last_eof = REG(DMA + 0x164);
    counters.underruns += (lcd & 16u) != 0;
    counters.dma_errors |= dma & 0x3f4u;
    REG(LCD + 0x70) = lcd & 0x13u;
    REG(DMA + 0x144) = dma & 0x7ffu;
    if (stats) *stats = counters;
    return (faulted || counters.underruns || counters.dma_errors) ? TIKU_DISPLAY_ERR_STATE : 0;
}

/** @brief Stop LCD requests and park AXI TX before returning buffer ownership. */
static void halt(void)
{
    if (!hardware) return;
    (void)tiku_s31_display_poll(NULL);
    REG(LCD + 0x14) = USER | (1u << 21);
    REG(DMA + 0x158) |= 1u;
    for (unsigned n = 0; n < 100000u; n++) {
        if ((REG(DMA + 0x158) & 8u) && (REG(DMA + 0x298) & 1u)) {
            REG(DMA + 0x148) |= 1u;
            REG(DMA + 0x148) &= ~1u;
            REG(LCD + 0x18) = MISC | (1u << 27);
            __asm__ volatile ("fence iorw,iorw" ::: "memory");
            return;
        }
    }
    tiku_s31_fatal("LCD DMA stop");
}

int tiku_s31_display_close(tiku_display_t *d)
{
    if (!context() || d != active || !active) return TIKU_DISPLAY_ERR_STATE;
    halt();
    if (hardware) {
        REG(LCD + 0x64) = 0;
        REG(DMA + 0x140) = 0;
        REG(DMA + 0x17c) = 63u;
        REG(LCD + 0x3c) = 0;
        REG(0x20587150u) &= ~4u;
        REG(0x20587154u) &= ~4u;
        REG(0x2058714cu) = 10u;
    }
    for (unsigned i = 0; i < claimed; i++) {
        unsigned p = pins[i];
        REG(CLR(p)) = BIT(p);
        if (p == 38) REG(saved_output ? 0x20583014u : 0x20583018u) = BIT(p);
        REG(ROUTE(p)) = saved[i][1];
        REG(CONFIG(p)) = saved[i][2];
        REG(MUX(p)) = saved[i][0];
        if (saved[i][3]) REG(SET(p)) = BIT(p);
        (void)tiku_gpio_release(p / 8 + 1, p % 8, owner);
    }
    if (usb_changed) {
        REG(USB_PADS) = (REG(USB_PADS) & ~USB_PAD_MASK) | saved_usb;
        usb_changed = 0;
    }
    if (scanout.buf && tiku_arena_destroy(&scanout) != TIKU_MEM_OK)
        tiku_s31_fatal("LCD storage release");
    active = NULL;
    claimed = hardware = paired = faulted = shown_ring = 0;
    shown_buffer = NULL;
    return 0;
}

/** @brief Start a circular frame and require two scanout boundaries in 300 ms. */
static int start(void)
{
    REG(LCD + 0x70) = 0x13;
    REG(DMA + 0x144) = 0x7ff;
    __asm__ volatile ("fence iorw,iorw" ::: "memory");
    REG(DMA + 0x15c) = (uint32_t)(uintptr_t)descriptors[shown_ring];
    REG(DMA + 0x158) = 2u;
    tiku_clock_arch_delay(1);
    REG(LCD + 0x14) = USER | (1u << 21) | (1u << 27);
    uint32_t previous = counters.vsync;
    uint64_t begin = tiku_s31_time();
    while (tiku_s31_time() - begin < 4800000u) {
        if (tiku_s31_display_poll(NULL)) return TIKU_DISPLAY_ERR_STATE;
        if (counters.vsync - previous >= 2) return 0;
    }
    return TIKU_DISPLAY_ERR_STATE;
}

/** @brief Build a self-looping frame in a ring that DMA no longer references. */
static void make_ring(unsigned ring, void *buffer)
{
    for (unsigned i = 0, offset = 0; i < COUNT; i++) {
        unsigned n = BYTES - offset;
        if (n > CHUNK) n = CHUNK;
        descriptors[ring][i] = (descriptor_t){n | (n << 12) | (1u << 31),
            (uint32_t)(uintptr_t)buffer + offset,
            (uint32_t)(uintptr_t)&descriptors[ring][(i + 1) % COUNT], 0};
        offset += n;
    }
    descriptors[ring][COUNT - 1].control |= 1u << 30;
}

/** @brief Publish non-scanning cache lines in 256-byte batches and poll scanout. */
static int publish(void *buffer, unsigned offset, unsigned bytes)
{
    uintptr_t a = (uintptr_t)buffer + offset;
    unsigned count = (bytes + (a & 63u) + 63u) & ~63u;
    a &= ~(uintptr_t)63u;
    while (count) {
        unsigned n = count > 256 ? 256 : count;
        if (tiku_s31_psram_sync((void *)a, n, 0) || tiku_s31_display_poll(NULL)) {
            halt(); faulted = 1;
            return TIKU_DISPLAY_ERR_STATE;
        }
        a += n; count -= n;
    }
    return 0;
}

/** @brief Refuse CPU writes to a paired framebuffer being read by DMA. */
static int writable(const tiku_display_t *d)
{
    if (!context() || d != active || faulted || !surface(d)) return 0;
    uintptr_t a = (uintptr_t)d->fb, b = (uintptr_t)shown_buffer;
    return !paired || (a >= b ? a - b >= BYTES : b - a >= BYTES);
}

int tiku_display_arch_init(tiku_display_t *d)
{
    if (!context() || active) return TIKU_DISPLAY_ERR_STATE;
    if (!surface(d)) return TIKU_DISPLAY_ERR_INVALID;
    for (unsigned p = 0; p < 62; p++) {
        unsigned signal = REG(ROUTE(p)) & 511u;
        if (signal == 149 || (signal >= 151 && signal <= 169))
            return TIKU_DISPLAY_ERR_STATE;
    }
    tiku_mem_request_t request = TIKU_MEM_REQUEST_DEFAULT;
    request.flags = TIKU_MEM_ALLOW_EXTERNAL;
    request.alignment = 64;
    if (tiku_mem_arena_create(&scanout, BYTES, &request) != TIKU_MEM_OK)
        return TIKU_DISPLAY_ERR_STATE;
    active = d;
    counters = (tiku_s31_display_stats_t){0};
    for (unsigned i = 0; i < 21; i++) {
        unsigned p = pins[i];
        if (tiku_gpio_claim(p / 8 + 1, p % 8, owner)) goto fail;
        saved[i][0] = REG(MUX(p)); saved[i][1] = REG(ROUTE(p));
        saved[i][2] = REG(CONFIG(p)); saved[i][3] = REG(ENABLE(p)) & BIT(p);
        claimed++;
    }
    saved_output = REG(0x20583010u) & BIT(38);
    if (tiku_s31_dma_copy(scanout.buf, d->fb, BYTES)) goto fail;
    shown_ring = 0;
    shown_buffer = scanout.buf;
    paired = faulted = 0;
    make_ring(0, shown_buffer);
#ifdef TIKU_S31_DISPLAY_TEST
    if (fault == 1) { descriptors[0][0].control &= ~(1u << 31); fault = 0; }
#endif
    /* AXI DMA uses TEE entry 64; descriptor faults report bus ID 72. */
    uint32_t mode = REG(0x20504100u);
    if ((mode & 4u) && (mode & 3u)) goto fail;
    REG(0x20504100u) = mode & ~3u;
    if (REG(0x20504100u) & 3u) goto fail;
    REG(0x20587078u) = (REG(0x20587078u) | 1u) & ~2u;
    REG(DMA + 0x2a8) |= 16u;
    REG(DMA + 0x27c) = 0x2f000000u; REG(DMA + 0x280) = 0x2f00ffffu;
    REG(DMA + 0x284) = 0x50000000u; REG(DMA + 0x288) = 0x50ffffffu;
    REG(DMA + 0x140) = 0;
    REG(DMA + 0x148) = 1u; REG(DMA + 0x148) = (3u << 5) | (1u << 10);
    REG(DMA + 0x14c) = 1u << 12;
    REG(DMA + 0x17c) = 0;
    REG(0x20587154u) = 4u;
    /* 40 MHz crystal / 8 = 5 MHz PCLK; LCD core runs at 40 MHz. */
    REG(0x20587150u) = 4u | (7u << 3);
    REG(0x2058714cu) = 15u; REG(0x2058714cu) = 5u;
    hardware = 1;
    REG(0x20586264u) = 8u;
    REG(LCD + 0x64) = 0;
    REG(LCD + 0x14) = 1u << 28;
    REG(LCD + 0x18) = 1u << 27;
    REG(LCD + 0x00) = (1u << 8) | (1u << 6) | 1u;
    REG(LCD + 0x10) = 1u << 16;
    REG(LCD + 0x14) = USER;
    REG(LCD + 0x18) = MISC;
    REG(LCD + 0x1c) = 479u | (547u << 16);
    REG(LCD + 0x20) = 799u | (927u << 16);
    REG(LCD + 0x24) = 79u | (54u << 16);
    REG(LCD + 0x28) = 22u | (1u << 10) | (1u << 12) | (39u << 16) | (1u << 23);
    REG(LCD + 0x34) = REG(LCD + 0x38) = 0;
    REG(LCD + 0x3c) = 1u;
    /* GPIO33/34 carry LCD data12/13 and share the USB Serial/JTAG PHY. */
    saved_usb = REG(USB_PADS) & USB_PAD_MASK;
    REG(USB_PADS) = (REG(USB_PADS) & ~USB_PAD_MASK) | (1u << 8);
    usb_changed = 1;
    for (unsigned i = 0; i < 21; i++) {
        unsigned p = pins[i];
        REG(CLR(p)) = BIT(p);
        REG(MUX(p)) = (saved[i][0] & ~((7u << 12) | (3u << 7) | (1u << 9))) | (1u << 12);
        REG(CONFIG(p)) = saved[i][2] & ~4u;
        REG(ROUTE(p)) = signals[i];
        if (p == 38) {
            REG(0x20583014u) = BIT(p);
            REG(MUX(p)) |= 1u << 9;
            REG(ROUTE(p)) |= 1u << 10;
        }
#ifdef TIKU_S31_DISPLAY_TEST
        REG(MUX(p)) |= 1u << 9;
#endif
        REG(SET(p)) = BIT(p);
    }
    if (start() || !(REG(0x20583068u) & BIT(38))) goto fail;
    return 0;
fail:
    (void)tiku_s31_display_close(d);
    return TIKU_DISPLAY_ERR_STATE;
}

uint32_t tiku_display_arch_caps(void) { return TIKU_DISPLAY_CAP_FLIP; }
tiku_display_fmt_t tiku_display_arch_format(void) { return TIKU_DISPLAY_FMT_RGB565; }
void tiku_display_arch_geometry(uint16_t *w, uint16_t *h)
{ if (w) *w = 800; if (h) *h = 480; }
int tiku_display_arch_fill_rect(tiku_display_t *d, uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h, uint32_t colour)
{
    if (!writable(d)) return TIKU_DISPLAY_ERR_STATE;
    if (x > 800 || y > 480 || w > 800 - x || h > 480 - y) return TIKU_DISPLAY_ERR_INVALID;
    uint16_t c = ((colour >> 8) & 0xf800u) | ((colour >> 5) & 0x7e0u) | ((colour >> 3) & 31u);
    for (unsigned row = y; row < y + h; row++) {
        uint16_t *p = (uint16_t *)d->fb + row * 800u + x;
        for (unsigned col = 0; col < w; col++) p[col] = c;
    }
    return 0;
}
int tiku_display_arch_present(tiku_display_t *d, uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h)
{
    if (!writable(d)) return TIKU_DISPLAY_ERR_STATE;
    if (x > 800 || y > 480 || w > 800 - x || h > 480 - y) return TIKU_DISPLAY_ERR_INVALID;
    /* The paired back buffer is published by set_scanout at flip time. */
    if (paired || !w || !h) return 0;
    unsigned begin = (y * 1600u + x * 2u) & ~63u;
    unsigned end = ((y + h - 1u) * 1600u + (x + w) * 2u + 63u) & ~63u;
    halt();
    if (tiku_s31_dma_copy((uint8_t *)scanout.buf + begin,
                         (uint8_t *)d->fb + begin, end - begin)) {
        faulted = 1;
        return TIKU_DISPLAY_ERR_STATE;
    }
    int result = start();
    if (result) { halt(); faulted = 1; }
    return result;
}
int tiku_display_arch_set_scanout(tiku_display_t *d, void *fb)
{
    if (!context() || !active || d != active || faulted) return TIKU_DISPLAY_ERR_STATE;
    tiku_display_t candidate = *d;
    candidate.fb = fb;
    if (!surface(&candidate)) return TIKU_DISPLAY_ERR_INVALID;
    uintptr_t a = (uintptr_t)fb, b = (uintptr_t)shown_buffer;
    if (a == b) return paired ? 0 : TIKU_DISPLAY_ERR_INVALID;
    if (a >= b ? a - b < BYTES : b - a < BYTES) return TIKU_DISPLAY_ERR_INVALID;
    if (publish(fb, 0, BYTES)) return TIKU_DISPLAY_ERR_STATE;
    unsigned next = shown_ring ^ 1u;
    make_ring(next, fb);
    (void)tiku_s31_display_poll(NULL);
    last_eof = 0;
    REG(DMA + 0x144) = 2u;
    __asm__ volatile ("fence iorw,iorw" ::: "memory");
#ifdef TIKU_S31_DISPLAY_TEST
    unsigned skip = fault == 3;
    fault = 0;
    if (!skip)
#endif
        descriptors[shown_ring][COUNT - 1].next = (uint32_t)(uintptr_t)descriptors[next];
    __asm__ volatile ("fence iorw,iorw" ::: "memory");
    uint64_t begin = tiku_s31_time();
    uint32_t expected = (uint32_t)(uintptr_t)&descriptors[next][COUNT - 1];
    while (tiku_s31_time() - begin < 8000000u) {
        if (tiku_s31_display_poll(NULL)) break;
        if (last_eof == expected) {
            shown_ring = next; shown_buffer = fb; paired = 1;
            return 0;
        }
    }
    halt(); faulted = 1;
    return TIKU_DISPLAY_ERR_STATE;
}
int tiku_display_arch_fill_circle(tiku_display_t *d, int16_t x, int16_t y, uint16_t r, uint32_t c)
{ (void)d; (void)x; (void)y; (void)r; (void)c; return TIKU_DISPLAY_ERR_UNSUPPORTED; }
int tiku_display_arch_fill_rounded_rect(tiku_display_t *d, int16_t x, int16_t y,
                                      uint16_t w, uint16_t h, uint16_t r, uint32_t c)
{ (void)d; (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; return TIKU_DISPLAY_ERR_UNSUPPORTED; }
