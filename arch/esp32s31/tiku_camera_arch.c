/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_camera_arch.c - SC101IOT set up over the shared I2C bus; whole DVP
 * frames or half-height pictures into PSRAM through AXI DMA RX 0, kept in
 * step by its end-of-frame interrupt.  Registers follow ESP-IDF 4d59230;
 * the sensor table is Espressif's esp_cam_sensor 1280x720 UYVY table
 * (Apache-2.0).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <hal/tiku_clock_hal.h>
#include <interfaces/bus/tiku_i2c_bus.h>
#include <interfaces/gpio/tiku_gpio_owner.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/threads/tiku_thread.h>
#include "tiku_camera_arch.h"
#include "tiku_lcdcam_arch.h"
#include "tiku_psram_arch.h"
#include "tiku_runtime_arch.h"

/* Register access; a host model defines its own before including this
 * file. */
#ifndef TIKU_S31_CAM_REG
#define TIKU_S31_CAM_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#endif
#define REG(a) TIKU_S31_CAM_REG(a)
#define LCD 0x20396000u
#define CAM_CTRL (LCD + 0x04u)      /* vs_eof_en 8, update 4 */
#define CAM_CTRL1 (LCD + 0x08u)     /* start 29, reset 30, FIFO reset 31 */
#define CAM_RGB_YUV (LCD + 0x0cu)
#define CAM_UPDATE (1u << 4)
#define CAM_START (1u << 29)
#define DMA 0x20348000u
#define IN_ST (DMA + 0x04u)
#define IN_ENA (DMA + 0x08u)
#define IN_CLR (DMA + 0x0cu)
#define IN_CONF0 (DMA + 0x10u)
#define IN_CONF1 (DMA + 0x14u)
#define IN_LINK1 (DMA + 0x20u)      /* stop 1, start 2 */
#define IN_LINK2 (DMA + 0x24u)
#define IN_EOF_AT (DMA + 0x2cu)     /* the descriptor that took the last EOF */
#define IN_PERI (DMA + 0x44u)
#define IN_RESET_AVAIL (DMA + 0x28cu)
#define RAW_EOF (1u << 1)
#define RAW_ERRORS ((1u << 2) | (1u << 3))
#define RAW_EMPTY (1u << 4)
#define CAM_CLOCK 0x20587158u       /* source 0 = crystal, enable 2, N at 3 */
#define MUX(n) (0x20582000u + 4u * (n))
#define CONFIG(n) (0x205830f4u + 4u * (n))
#define ROUTE(n) (0x20583af4u + 4u * (n))
#define INPUT(s) (0x205832f4u + 4u * (s))
#define ENABLE(n) ((n) < 32 ? 0x20583034u : 0x20583040u)
#define SET(n) ((n) < 32 ? 0x20583038u : 0x20583044u)
#define CLR(n) ((n) < 32 ? 0x2058303cu : 0x20583048u)
#define BIT(n) (1u << ((n) & 31u))
#define PSRAM 0x50000000u
#define CHUNK 4032u
#define COUNT ((TIKU_S31_CAM_FRAME_BYTES + CHUNK - 1u) / CHUNK)
#define OWNER (1u << 31)
#define SUC_EOF (1u << 30)
#define LINE_BYTES (TIKU_S31_CAM_W * 2u)
/* A stream descriptor a line: even lines into a half, odd lines into the
 * spare line; two halves, the second linked back to the first. */
#define STREAM_COUNT (2u * TIKU_S31_CAM_H)
#define IRQ_LINE 30u
#define IRQ_SOURCE 48u              /* AXI DMA RX 0: matrix map register 0xc0 */
#define IRQ_PRIORITY 1u
#define IRQ_EVENTS (RAW_EOF | RAW_ERRORS | RAW_EMPTY)
/* SC101IOT: 8-bit registers in 256-byte pages, the page in register 0xf0. */
#define PAGE 0xf0u
#define PINS 12u
#define INPUTS 11u
#define XCLK_SIGNAL 150u

/** @brief AXI DMA descriptor: size 0-11, length 12-23, EOF 30, owner 31. */
typedef struct { uint32_t control, buffer, next, reserved; } descriptor_t;
_Static_assert(sizeof(descriptor_t) == 16, "AXI descriptor size");
/* The frame's descriptors, the stream's and the spare line, each on whole
 * cache lines; the AXI DMA reaches PSRAM, not internal SRAM. */
#define FRAME_DESC_BYTES ((COUNT * sizeof(descriptor_t) + 63u) & ~63u)
#define STREAM_DESC_BYTES ((STREAM_COUNT * sizeof(descriptor_t) + 63u) & ~63u)
#define RING_BYTES (FRAME_DESC_BYTES + STREAM_DESC_BYTES + LINE_BYTES)

/* D0-D7, PCLK, VSYNC and HREF in, XCLK out last. */
static const uint8_t pins[PINS] = {46,47,48,49,50,51,52,53,54,56,57,55};
static const uint8_t signals[INPUTS] = {162,163,164,165,166,167,168,169,158,161,159};
static const uint8_t sensor_table[][2] = {
    {0xf0, 0x30}, {0x01, 0xff}, {0x02, 0xe0}, {0x30, 0x10}, {0x3f, 0x81}, {0xf0, 0x00},
    {0x70, 0x6b}, {0x72, 0x30}, {0x84, 0xb4}, {0x8b, 0x00}, {0x8c, 0x20}, {0x8d, 0x02},
    {0x8e, 0xec}, {0x9e, 0x10}, {0xb0, 0xc1}, {0xc8, 0x10}, {0xc9, 0x10}, {0xc6, 0x00},
    {0xe0, 0x0f}, {0xb5, 0xf0}, {0xde, 0x80}, {0xb5, 0xf0}, {0xde, 0x80}, {0xb2, 0x50},
    {0xb3, 0xfc}, {0xb4, 0x40}, {0xb5, 0xc0}, {0xb6, 0x50}, {0xb7, 0xfc}, {0xb8, 0x40},
    {0xb9, 0xc0}, {0xba, 0xff}, {0xbb, 0xcc}, {0xbc, 0xa9}, {0xbd, 0x7d}, {0xc1, 0x77},
    {0xf0, 0x01}, {0x70, 0x02}, {0x71, 0x02}, {0x72, 0x50}, {0x73, 0x02}, {0x74, 0xd2},
    {0x75, 0x20}, {0x76, 0x81}, {0x77, 0x8c}, {0x78, 0x81}, {0xf4, 0x01}, {0xf5, 0x00},
    {0xf6, 0x00}, {0xf0, 0x36}, {0x40, 0x03}, {0x41, 0x01}, {0xf0, 0x39}, {0x02, 0x70},
    {0xf0, 0x32}, {0x41, 0x00}, {0x43, 0x01}, {0x48, 0x02}, {0xf0, 0x45}, {0x09, 0x20},
    {0xf0, 0x33}, {0x33, 0x10}, {0xf0, 0x30}, {0x38, 0x44}, {0xf0, 0x39}, {0x07, 0x00},
    {0x08, 0x19}, {0x47, 0x00}, {0x48, 0x00}, {0xf0, 0x37}, {0x24, 0x31}, {0xf0, 0x34},
    {0x9f, 0x02}, {0xa6, 0x51}, {0xa7, 0x57}, {0xe8, 0x5f}, {0xa8, 0x50}, {0xa9, 0x50},
    {0xe9, 0x50}, {0xf0, 0x33}, {0xb3, 0x58}, {0xb2, 0x78}, {0xf0, 0x34}, {0x9f, 0x03},
    {0xa6, 0x51}, {0xa7, 0x57}, {0xaa, 0x01}, {0xab, 0x28}, {0xac, 0x01}, {0xad, 0x38},
    {0xf0, 0x33}, {0x0a, 0x01}, {0x0b, 0x28}, {0xf0, 0x33}, {0x64, 0x0f}, {0xec, 0x51},
    {0xed, 0x57}, {0x06, 0x58}, {0xe9, 0x58}, {0xeb, 0x68}, {0xf0, 0x33}, {0x64, 0x0f},
    {0xf0, 0x36}, {0x70, 0xdf}, {0xb6, 0x40}, {0xb7, 0x51}, {0xb8, 0x53}, {0xb9, 0x57},
    {0xba, 0x5f}, {0xb0, 0x84}, {0xb1, 0x82}, {0xb2, 0x84}, {0xb3, 0x88}, {0xb4, 0x90},
    {0xb5, 0x90}, {0xf0, 0x36}, {0x7e, 0x50}, {0x7f, 0x51}, {0x77, 0x81}, {0x78, 0x86},
    {0x79, 0x89}, {0xf0, 0x36}, {0x70, 0xdf}, {0x9c, 0x51}, {0x9d, 0x57}, {0x90, 0x54},
    {0x91, 0x54}, {0x92, 0x56}, {0xf0, 0x36}, {0xa0, 0x51}, {0xa1, 0x57}, {0x96, 0x33},
    {0x97, 0x43}, {0x98, 0x43}, {0xf0, 0x36}, {0x70, 0xdf}, {0x7c, 0x40}, {0x7d, 0x53},
    {0x74, 0xd0}, {0x75, 0xf0}, {0x76, 0xf0}, {0xf0, 0x37}, {0x0f, 0xd5}, {0x7a, 0x40},
    {0x7b, 0x57}, {0x71, 0x09}, {0x72, 0x09}, {0x73, 0x05}, {0xf0, 0x33}, {0x01, 0x44},
    {0xf0, 0x36}, {0x37, 0xfb}, {0xf0, 0x36}, {0x3c, 0x0d}, {0xf0, 0x33}, {0x14, 0x95},
    {0xf0, 0x33}, {0x8f, 0x80}, {0xf0, 0x37}, {0x27, 0x14}, {0x28, 0x03}, {0xf0, 0x36},
    {0x37, 0xf4}, {0xf0, 0x33}, {0x01, 0x44}, {0xf0, 0x36}, {0x79, 0x89}, {0xf0, 0x34},
    {0xac, 0x01}, {0xad, 0x40}, {0xf0, 0x33}, {0xeb, 0x70}, {0xf0, 0x34}, {0xa8, 0x50},
    {0xa9, 0x50}, {0xf0, 0x33}, {0xb3, 0x58}, {0xf0, 0x36}, {0x11, 0x80}, {0xf0, 0x36},
    {0x41, 0x51}, {0xf0, 0x3f}, {0x03, 0x09}, {0xf0, 0x32}, {0x0c, 0x06}, {0x0d, 0x82},
    {0x0e, 0x02}, {0x0f, 0xee}, {0xf0, 0x36}, {0xea, 0x09}, {0xeb, 0xf5}, {0xec, 0x11},
    {0xed, 0x27}, {0xe9, 0x20}, {0xf0, 0x01}, {0x79, 0x02}, {0xf0, 0x31}, {0x00, 0x00},
};
/* What the receiver does: IDLE; STREAMING pictures; SWITCHING from the
 * stream to a capture at the next frame's end; CAPTURING one frame; and
 * CAPTURED or FAILED until the poll reports it.  The interrupt moves
 * STREAMING, SWITCHING and CAPTURING on, and stops a stream after a DMA
 * error. */
enum { IDLE, STREAMING, SWITCHING, CAPTURING, CAPTURED, FAILED };
static uint32_t saved[PINS][4], saved_input[INPUTS];
static unsigned claimed, held, opened, irq;
static volatile unsigned mode;
/* The newest whole picture: its sequence number times two plus its half;
 * 0 before the first. */
static volatile uint32_t newest;
static void (*volatile notify)(void);
static tiku_arena_t ring;
static void *frame_buffer;
static uint64_t began;
static volatile tiku_s31_camera_stats_t counters;
static const char owner[] = "cam";

static int context(void) { return !tiku_s31_in_trap && tiku_thread_in_kernel(); }

static int sensor_write(uint8_t reg, uint8_t value)
{
    const uint8_t bytes[2] = {reg, value};
    return tiku_i2c_write(TIKU_S31_CAM_ADDR, bytes, 2) ? TIKU_S31_CAM_ERR_BUS : 0;
}

static int sensor_read(uint8_t reg, uint8_t *value)
{
    return tiku_i2c_write_read(TIKU_S31_CAM_ADDR, &reg, 1, value, 1) ? TIKU_S31_CAM_ERR_BUS : 0;
}

/** @brief Set bit 0 of register 0x31xx to @p on; 0x3100 bit 0 streams,
 *  0x3103 bit 0 resets the sensor. */
static int sensor_bit(uint8_t reg, unsigned on)
{
    uint8_t value;
    if (sensor_write(PAGE, 0x31) || sensor_read(reg, &value)) return TIKU_S31_CAM_ERR_BUS;
    return sensor_write(reg, on ? (uint8_t)(value | 1u) : (uint8_t)(value & ~1u));
}

/** @brief Stop the receiver and the RX channel; leaves the counters. */
static void halt(void)
{
    REG(CAM_CTRL1) &= ~CAM_START;
    REG(CAM_CTRL) |= CAM_UPDATE;
    REG(IN_LINK1) = 1u << 1;
}

static volatile descriptor_t *frame_chain(void) { return (volatile descriptor_t *)ring.buf; }
static volatile descriptor_t *stream_chain(void)
{
    return (volatile descriptor_t *)((uint8_t *)ring.buf + FRAME_DESC_BYTES);
}

/** @brief Reset the RX channel and the receiver and start receiving into
 *  the chain at @p first; interrupt or kernel context. */
static int restart(volatile descriptor_t *first)
{
    halt();
    for (unsigned n = 0; !(REG(IN_RESET_AVAIL) & 1u); n++)
        if (n > 100000u) return TIKU_S31_CAM_ERR_FAULT;
    REG(IN_CONF0) |= 1u;
    REG(IN_CONF0) &= ~1u;
    REG(CAM_CTRL1) |= (1u << 30) | (1u << 31);
    REG(IN_CLR) = 0x7ffu;
    REG(IN_LINK2) = (uint32_t)(uintptr_t)first;
    REG(IN_LINK1) = 1u << 2;
    REG(CAM_CTRL) |= CAM_UPDATE;
    REG(CAM_CTRL1) |= CAM_START;
    return 0;
}

/** @brief Lay out the frame chain over @p buffer: CHUNK-byte descriptors,
 *  the last ending the chain, so a whole frame ends with its EOF on it. */
static void build_frame(uint8_t *buffer)
{
    volatile descriptor_t *d = frame_chain();
    for (unsigned i = 0, offset = 0; i < COUNT; i++) {
        unsigned n = TIKU_S31_CAM_FRAME_BYTES - offset;
        if (n > CHUNK) n = CHUNK;
        d[i] = (descriptor_t){n | OWNER, (uint32_t)(uintptr_t)buffer + offset,
                              i + 1u < COUNT ? (uint32_t)(uintptr_t)&d[i + 1] : 0u, 0};
        offset += n;
    }
}

/** @brief Lay out the stream ring over @p buffer: line 2k of a frame into
 *  line k of the half, the odd lines into the spare line, the second half
 *  linked back to the first; a whole picture ends with its EOF on its
 *  half's last descriptor. */
static void build_stream(uint8_t *buffer)
{
    volatile descriptor_t *d = stream_chain();
    uint32_t spare = (uint32_t)(uintptr_t)ring.buf + FRAME_DESC_BYTES + STREAM_DESC_BYTES;
    for (unsigned i = 0; i < STREAM_COUNT; i++) {
        unsigned half = i / TIKU_S31_CAM_H, line = i % TIKU_S31_CAM_H;
        uint32_t at = (line & 1u) ? spare :
            (uint32_t)(uintptr_t)buffer + half * TIKU_S31_CAM_PREVIEW_BYTES + (line / 2u) * LINE_BYTES;
        d[i] = (descriptor_t){LINE_BYTES | OWNER, at,
                              (uint32_t)(uintptr_t)&d[(i + 1u) % STREAM_COUNT], 0};
    }
}

/** @brief The end-of-frame interrupt: record a whole picture or frame, or
 *  re-arm at this frame's end when the last one started inside a frame;
 *  move a pending capture onto the frame chain. */
static void received(void)
{
    uint32_t events = REG(IN_ST);
    uintptr_t at = REG(IN_EOF_AT);
    volatile descriptor_t *s = stream_chain(), *f = frame_chain();
    REG(IN_CLR) = events;
    if (events & RAW_ERRORS) {
        /* A stream stops; a capture fails at its poll. */
        counters.dma_errors++;
        halt();
        mode = mode == STREAMING ? IDLE : FAILED;
    } else if (events & RAW_EMPTY) {
        counters.long_frames++;
        if (mode == CAPTURING && restart(f)) mode = FAILED;
    } else if (!(events & RAW_EOF)) {
        return;
    } else if (mode == SWITCHING) {
        mode = restart(f) ? FAILED : CAPTURING;
        return;
    } else if (mode == STREAMING) {
        if (at == (uintptr_t)&s[TIKU_S31_CAM_H - 1u] || at == (uintptr_t)&s[STREAM_COUNT - 1u]) {
            newest = ((newest >> 1) + 1u) << 1 | (at == (uintptr_t)&s[STREAM_COUNT - 1u]);
            counters.frames++;
        } else {
            counters.short_frames++;
            if (restart(s)) mode = FAILED;
            return;
        }
    } else if (mode == CAPTURING) {
        if (at != (uintptr_t)&f[COUNT - 1u]) {
            counters.short_frames++;
            if (restart(f)) mode = FAILED;
            return;
        }
        halt();
        counters.frames++;
        mode = CAPTURED;
    } else {
        return;
    }
    if (notify) notify();
}

/** @brief Release what open took, in reverse order; also undoes a partial
 *  open. */
static void teardown(void)
{
    if (irq) {
        (void)tiku_s31_irq_enable(IRQ_LINE, 0);
        (void)tiku_s31_irq_release(IRQ_LINE, received);
        irq = 0;
    }
    if (held) {
        halt();
        REG(IN_PERI) = 63u;
        REG(CAM_CLOCK) &= ~4u;
        tiku_s31_lcdcam_release();
        held = 0;
    }
    for (unsigned i = 0; i < claimed; i++) {
        unsigned p = pins[i];
        REG(CLR(p)) = BIT(p);
        if (i < INPUTS) REG(INPUT(signals[i])) = saved_input[i];
        REG(ROUTE(p)) = saved[i][1];
        REG(CONFIG(p)) = saved[i][2];
        REG(MUX(p)) = saved[i][0];
        if (saved[i][3]) REG(SET(p)) = BIT(p);
        (void)tiku_gpio_release(p / 8 + 1, p % 8, owner);
    }
    claimed = 0;
    if (ring.buf && tiku_arena_destroy(&ring) != TIKU_MEM_OK)
        tiku_s31_fatal("camera storage release");
    ring.buf = NULL;
    opened = 0;
    mode = IDLE;
    newest = 0;
    notify = NULL;
}

int tiku_s31_camera_open(unsigned xclk_div)
{
    if (!context() || opened || claimed || held) return TIKU_S31_CAM_ERR_STATE;
    if (xclk_div < TIKU_S31_CAM_XCLK_DIV_MIN || xclk_div > TIKU_S31_CAM_XCLK_DIV_MAX)
        return TIKU_S31_CAM_ERR_PARAM;
    counters = (tiku_s31_camera_stats_t){0};
    if (tiku_s31_irq_claim(IRQ_LINE, IRQ_SOURCE, IRQ_PRIORITY, received)) return TIKU_S31_CAM_ERR_STATE;
    irq = 1;
    if (tiku_tier_arena_create_span(&ring, TIKU_MEM_PSRAM, 0, RING_BYTES, 64, 0x43) != TIKU_MEM_OK) {
        ring.buf = NULL;
        teardown();
        return TIKU_S31_CAM_ERR_STATE;
    }
    for (unsigned i = 0; i < PINS; i++) {
        unsigned p = pins[i];
        if (tiku_gpio_claim(p / 8 + 1, p % 8, owner)) { teardown(); return TIKU_S31_CAM_ERR_STATE; }
        saved[i][0] = REG(MUX(p)); saved[i][1] = REG(ROUTE(p));
        saved[i][2] = REG(CONFIG(p)); saved[i][3] = REG(ENABLE(p)) & BIT(p);
        if (i < INPUTS) saved_input[i] = REG(INPUT(signals[i]));
        claimed++;
    }
    /* AXI DMA uses TEE entry 64 in trusted mode; PSRAM is its external window. */
    uint32_t mode = REG(0x20504100u);
    if ((mode & 4u) && (mode & 3u)) { teardown(); return TIKU_S31_CAM_ERR_STATE; }
    REG(0x20504100u) = mode & ~3u;
    if (REG(0x20504100u) & 3u) { teardown(); return TIKU_S31_CAM_ERR_STATE; }
    REG(0x20587078u) = (REG(0x20587078u) | 1u) & ~2u;
    REG(DMA + 0x2a8) |= 16u;
    REG(DMA + 0x284) = PSRAM; REG(DMA + 0x288) = PSRAM + 0x00ffffffu;
    tiku_s31_lcdcam_claim();
    held = 1;
    REG(CAM_CLOCK) = (xclk_div << 3) | 4u;
    for (unsigned i = 0; i < PINS; i++) {
        unsigned p = pins[i];
        REG(CLR(p)) = BIT(p);
        REG(CONFIG(p)) = saved[i][2] & ~4u;
        if (i < INPUTS) {
            REG(MUX(p)) = (saved[i][0] & ~((7u << 12) | (3u << 7))) | (1u << 12) | (1u << 9);
            /* VSYNC is active low on the sensor; the receiver counts it high. */
            REG(INPUT(signals[i])) = p | (signals[i] == 161 ? 1u << 8 : 0u) | (1u << 9);
        } else {
            REG(MUX(p)) = (saved[i][0] & ~((7u << 12) | (3u << 7) | (1u << 9))) | (1u << 12);
            REG(ROUTE(p)) = XCLK_SIGNAL;
            REG(SET(p)) = BIT(p);
        }
    }
    REG(CAM_CTRL) = 1u << 8;
    REG(CAM_CTRL1) = 0;
    REG(CAM_RGB_YUV) = 0;
    REG(CAM_CTRL) |= CAM_UPDATE;
    REG(IN_ENA) = 0;
    REG(IN_PERI) = 0;
    REG(IN_CONF0) = 1u;
    REG(IN_CONF0) = (3u << 4) | (1u << 9);
    REG(IN_CONF1) = 0u;            /* no owner check: the stream ring wraps */
    REG(IN_CLR) = 0x7ffu;
    tiku_clock_arch_delay(20000);
    uint8_t high = 0, low = 0;
    if (sensor_write(PAGE, 0x31) || sensor_read(0xf7, &high) || sensor_read(0xf8, &low)) {
        teardown();
        return TIKU_S31_CAM_ERR_ABSENT;
    }
    counters.model = (uint16_t)(high << 8 | low);
    if (counters.model != TIKU_S31_CAM_MODEL) {
        teardown();
        return TIKU_S31_CAM_ERR_ABSENT;
    }
    int rc = sensor_bit(0x03, 1);
    tiku_clock_arch_delay(5000);
    for (unsigned i = 0; !rc && i < sizeof sensor_table / sizeof sensor_table[0]; i++)
        rc = sensor_write(sensor_table[i][0], sensor_table[i][1]);
    if (!rc) rc = sensor_bit(0x00, 1);
    if (rc) { teardown(); return TIKU_S31_CAM_ERR_BUS; }
    opened = 1;
    return 0;
}

/** @brief Whether @p buffer of @p bytes is 64-byte aligned mapped PSRAM
 *  that holds a whole frame. */
static int frame_fits(const void *buffer, uint32_t bytes)
{
    uintptr_t a = (uintptr_t)buffer;
    return !(a & 63u) && a >= PSRAM && bytes >= TIKU_S31_CAM_FRAME_BYTES &&
           a - PSRAM + TIKU_S31_CAM_FRAME_BYTES <= tiku_s31_psram_size();
}

/** @brief Whether a capture is armed or waits to be polled. */
static int capturing(void)
{
    return mode == SWITCHING || mode == CAPTURING || mode == CAPTURED || mode == FAILED;
}

int tiku_s31_camera_stream(void *buffer, uint32_t bytes, void (*fn)(void))
{
    if (!context() || !opened || capturing()) return TIKU_S31_CAM_ERR_STATE;
    (void)tiku_s31_irq_enable(IRQ_LINE, 0);
    halt();
    REG(IN_ENA) = 0;
    mode = IDLE;
    newest = 0;
    notify = NULL;
    if (!buffer) return 0;
    if (!frame_fits(buffer, bytes)) return TIKU_S31_CAM_ERR_PARAM;
    build_stream(buffer);
    if (tiku_s31_psram_sync(ring.buf, RING_BYTES, 1) ||
        tiku_s31_psram_sync(buffer, TIKU_S31_CAM_FRAME_BYTES, 1))
        return TIKU_S31_CAM_ERR_FAULT;
    notify = fn;
    mode = STREAMING;
    if (restart(stream_chain())) { halt(); mode = IDLE; return TIKU_S31_CAM_ERR_FAULT; }
    REG(IN_ENA) = IRQ_EVENTS;
    (void)tiku_s31_irq_enable(IRQ_LINE, 1);
    return 0;
}

int tiku_s31_camera_latest(uint32_t *sequence)
{
    uint32_t n = newest;
    if (sequence) *sequence = n >> 1;
    return mode == STREAMING && n ? (int)(n & 1u) : -1;
}

int tiku_s31_camera_capture(void *frame, uint32_t bytes)
{
    if (!context() || !opened || capturing()) return TIKU_S31_CAM_ERR_STATE;
    if (!frame_fits(frame, bytes)) return TIKU_S31_CAM_ERR_PARAM;
    (void)tiku_s31_irq_enable(IRQ_LINE, 0);
    frame_buffer = frame;
    build_frame(frame);
    if (tiku_s31_psram_sync(ring.buf, FRAME_DESC_BYTES, 1) ||
        tiku_s31_psram_sync(frame, TIKU_S31_CAM_FRAME_BYTES, 1)) {
        if (mode == STREAMING) (void)tiku_s31_irq_enable(IRQ_LINE, 1);
        return TIKU_S31_CAM_ERR_FAULT;
    }
    began = tiku_s31_time();
    if (mode == STREAMING) {
        mode = SWITCHING;
    } else {
        mode = CAPTURING;
        if (restart(frame_chain())) { halt(); mode = IDLE; return TIKU_S31_CAM_ERR_FAULT; }
    }
    REG(IN_ENA) = IRQ_EVENTS;
    (void)tiku_s31_irq_enable(IRQ_LINE, 1);
    return TIKU_S31_CAM_BUSY;
}

int tiku_s31_camera_poll(tiku_s31_camera_stats_t *stats)
{
    int rc = TIKU_S31_CAM_BUSY;
    if (!context() || !opened || !capturing()) {
        rc = TIKU_S31_CAM_ERR_STATE;
    } else if (mode == CAPTURED) {
        (void)tiku_s31_irq_enable(IRQ_LINE, 0);
        REG(IN_ENA) = 0;
        mode = IDLE;
        counters.frame_us = (uint32_t)((tiku_s31_time() - began) / 16u);
        rc = tiku_s31_psram_invalidate(frame_buffer, TIKU_S31_CAM_FRAME_BYTES) ?
             TIKU_S31_CAM_ERR_FAULT : 0;
    } else if (mode == FAILED ||
               tiku_s31_time() - began > (uint64_t)TIKU_S31_CAM_WAIT_US * 16u) {
        (void)tiku_s31_irq_enable(IRQ_LINE, 0);
        halt();
        REG(IN_ENA) = 0;
        rc = mode == FAILED ? TIKU_S31_CAM_ERR_FAULT : TIKU_S31_CAM_ERR_TIMEOUT;
        mode = IDLE;
    }
    if (stats) {
        (void)tiku_s31_irq_enable(IRQ_LINE, 0);
        *stats = counters;
        if (mode == STREAMING || capturing()) (void)tiku_s31_irq_enable(IRQ_LINE, 1);
    }
    return rc;
}

int tiku_s31_camera_close(void)
{
    if (!context() || !opened) return TIKU_S31_CAM_ERR_STATE;
    (void)tiku_s31_irq_enable(IRQ_LINE, 0);
    halt();
    REG(IN_ENA) = 0;
    (void)sensor_bit(0x00, 0);
    teardown();
    return 0;
}
