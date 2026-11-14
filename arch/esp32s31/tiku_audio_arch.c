/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_audio_arch.c - I2S0 stereo rings with bounded DMA ownership.
 * Register layout follows ESP-IDF 4d59230 S31 I2S and AHB DMA definitions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_audio_arch.h"
#include "tiku_runtime_arch.h"
#include <kernel/threads/tiku_thread.h>
#include <interfaces/gpio/tiku_gpio_owner.h>
#include <stddef.h>
#include <string.h>
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define I2S 0x20387000u
#define DMA 0x20343000u
#define OWNER (1u << 31)
#define EOF_BIT (1u << 30)
#define BLOCK 1024u
#define RING 8u
#define PINS 0xf8u
#define INPUT 0x20583364u
#define MUX(p) (0x20582000u + 4u * (p))
#define ROUTE(p) (0x20583af4u + 4u * (p))
#define CONFIG(p) (0x205830f4u + 4u * (p))
/** @brief AHB descriptor: byte size/length, ownership, buffer and next pointer. */
typedef struct { uint32_t control, buffer, next; } descriptor_t;
static volatile descriptor_t rx[RING] __attribute__((aligned(64)));
static volatile descriptor_t tx[RING] __attribute__((aligned(64)));
static int16_t rx_data[RING][TIKU_S31_AUDIO_SAMPLES] __attribute__((aligned(64)));
static int16_t tx_data[RING][TIKU_S31_AUDIO_SAMPLES] __attribute__((aligned(64)));
static uint32_t pads[5][3], clocks[5], saved_enable, saved_input, saved_alive, saved_trust;
static unsigned opened, running, failed, claimed;
static uint32_t completed;
static uint64_t progress;
static tiku_s31_audio_status_t status;
static const char name[] = "audio";
/** @brief Restrict hardware calls to serialized kernel foreground execution. */
static int context(void) { return !tiku_s31_in_trap && tiku_thread_in_kernel(); }
/** @brief Stop I2S and reset pair one; refuse continued execution if DMA cannot park. */
static void halt(void)
{
    REG(I2S + 0x20) &= ~4u; REG(I2S + 0x24) &= ~4u;
    REG(DMA + 0x210) = 2u; REG(DMA + 0x290) = 1u;
    REG(DMA + 0x200) |= 1u; REG(DMA + 0x280) |= 1u;
    REG(DMA + 0x200) &= ~1u; REG(DMA + 0x280) &= ~1u;
    uint64_t start = tiku_s31_time();
    for (unsigned n = 0; n < 65536u; n++) {
        if ((REG(DMA + 0x210) & 16u) && (REG(DMA + 0x290) & 8u)) {
            __asm__ volatile ("fence iorw,iorw" ::: "memory");
            running = 0; return;
        }
        if (tiku_s31_time() - start >= 160000u) break;
    }
    tiku_s31_fatal("audio DMA stop");
}
/** @brief Apply configuration across clock domains with a 10 ms deadline. */
static int update(unsigned offset)
{
    REG(I2S + offset) |= 1u << 8;
    uint64_t start = tiku_s31_time();
    for (unsigned n = 0; n < 100000; n++) {
        if (!(REG(I2S + offset) & (1u << 8))) return 0;
        if (tiku_s31_time() - start >= 160000u) break;
    }
    return TIKU_S31_AUDIO_IO;
}
int tiku_s31_audio_status(tiku_s31_audio_status_t *out)
{
    if (!context() || !opened || !out) return TIKU_S31_AUDIO_INVALID;
    *out = status; out->blocks = completed; out->running = running; out->failed = failed;
    return 0;
}
int tiku_s31_audio_amplifier(unsigned enable)
{
    if (!context() || !opened || failed || enable > 1) return TIKU_S31_AUDIO_INVALID;
    REG(enable ? 0x20583008u : 0x2058300cu) = 1u << 7;
    uint64_t start = tiku_s31_time();
    for (unsigned n = 0; n < 10000; n++) {
        if (!!(REG(0x20583064u) & (1u << 7)) == enable) return 0;
        if (tiku_s31_time() - start >= 16000u) break;
    }
    REG(0x2058300cu) = 1u << 7; halt(); failed = 1;
    return TIKU_S31_AUDIO_IO;
}
void tiku_s31_audio_close(void)
{
    if (!context() || !opened) return;
    REG(0x2058300cu) = 1u << 7;
    halt();
    REG(0x205870e4u) |= 2u;
    REG(0x2058303cu) = PINS;
    for (unsigned i = 0; i < 5; i++) {
        REG(ROUTE(i + 3)) = pads[i][1]; REG(CONFIG(i + 3)) = pads[i][2];
        REG(MUX(i + 3)) = pads[i][0];
        (void)tiku_gpio_release(1, i + 3, name);
    }
    REG(INPUT) = saved_input;
    REG(0x20583038u) = saved_enable & PINS;
    for (unsigned i = 1; i < 5; i++) REG(0x205870e4u + i * 4) = clocks[i];
    REG(0x205870e4u) = clocks[0]; REG(0x20589008u) = saved_alive;
    REG(0x20504084u) = saved_trust;
    opened = running = failed = claimed = 0;
}
int tiku_s31_audio_open(void)
{
    if (!context() || opened) return TIKU_S31_AUDIO_INVALID;
    for (unsigned p = 0; p < 62; p++) {
        unsigned signal = REG(ROUTE(p)) & 511u;
        if (signal == 25 || signal == 27 || signal == 28) return TIKU_S31_AUDIO_INVALID;
    }
    saved_trust = REG(0x20504084u);
    if ((saved_trust & 4u) && (saved_trust & 3u)) return TIKU_S31_AUDIO_INVALID;
    for (claimed = 0; claimed < 5; claimed++) {
        if (tiku_gpio_claim(1, claimed + 3, name)) {
            while (claimed) (void)tiku_gpio_release(1, --claimed + 3, name);
            return TIKU_S31_AUDIO_INVALID;
        }
    }
    for (unsigned i = 0; i < 5; i++) {
        pads[i][0] = REG(MUX(i + 3)); pads[i][1] = REG(ROUTE(i + 3));
        pads[i][2] = REG(CONFIG(i + 3)); clocks[i] = REG(0x205870e4u + 4 * i);
    }
    saved_enable = REG(0x20583034u); saved_input = REG(INPUT); saved_alive = REG(0x20589008u);
    opened = 1; failed = 0; completed = 0; memset(&status,0,sizeof status);
    REG(0x2058303cu) = PINS; REG(0x2058300cu) = 1u << 7;
    for (unsigned p = 3; p <= 7; p++) {
        REG(MUX(p)) = (pads[p - 3][0] & ~((7u << 12) | (3u << 7))) | (1u << 12) | (1u << 9);
        REG(CONFIG(p)) = pads[p - 3][2] & ~4u;
        REG(ROUTE(p)) = 256u | (1u << 10);
    }
    REG(0x20583038u) = 1u << 7;
    REG(0x205870e4u) = 3; REG(0x205870e4u) = 1; REG(0x20589008u) |= 1u << 31;
    /* 40 MHz / (9 + 49/64) / 8 = 512 kHz; divider coefficients latch in order. */
    for (unsigned i = 0; i < 2; i++) {
        uint32_t ctrl = i ? 0x205870f0u : 0x205870e8u;
        uint32_t master = i ? 0 : 1u << 11;
        REG(ctrl) = 1u | (2u << 3) | master; REG(ctrl + 4) = 1u << 9;
        REG(ctrl + 4) |= 1u << 27;
        REG(ctrl + 4) |= 15u << 18;
        REG(ctrl + 4) = (REG(ctrl + 4) & ~(511u << 9)) | (4u << 9);
        REG(ctrl + 4) |= 3u;
        REG(ctrl) = 1u | (9u << 3) | master;
    }
    REG(I2S + 0x20) = (7u << 21) | (1u << 19) | (1u << 13) | (1u << 12) | 8u;
    REG(I2S + 0x24) = (1u << 30) | (7u << 21) | (1u << 19) | (1u << 13) | (1u << 12);
    REG(I2S + 0x28) = REG(I2S + 0x2c) = 15u | (15u << 14) | (15u << 19) | (15u << 27);
    REG(I2S + 0x50) = REG(I2S + 0x54) = 3u | (1u << 16);
    REG(I2S + 0x64) = BLOCK;
    REG(I2S + 0x14) = 0; REG(I2S + 0x18) = UINT32_MAX;
    REG(ROUTE(3)) = 25; REG(ROUTE(4)) = 27; REG(ROUTE(5)) = 28;
    REG(INPUT) = (1u << 9) | 6u;
    REG(0x20583038u) = (1u << 3) | (1u << 4) | (1u << 5);
    /* Peripheral DMA request 1 uses trusted bus master 33. Pair zero remains reserved for copies. */
    REG(0x20504084u) = saved_trust & ~3u;
    REG(0x2058707cu) = (REG(0x2058707cu) | 1u) & ~2u;
    REG(DMA + 0xa4) |= 8u; REG(DMA + 0x600) = 0x2f000000u; REG(DMA + 0x604) = 0x53ffffffu;
    halt();
    REG(DMA + 0x200) = 4u; REG(DMA + 0x280) = 28u;
    REG(DMA + 0x204) = REG(DMA + 0x284) = 1u << 12;
    REG(DMA + 0x238) = REG(DMA + 0x2b8) = 1;
    REG(DMA + 0x18) = REG(DMA + 0x68) = 0;
    if (update(0x20) || update(0x24)) { tiku_s31_audio_close(); return TIKU_S31_AUDIO_IO; }
    REG(I2S + 0x24) |= 4u;
    return 0;
}
int tiku_s31_audio_start(const int16_t *seed)
{
    if (!context() || !opened || running || failed) return TIKU_S31_AUDIO_INVALID;
    halt();
    for (unsigned i = 0; i < RING; i++) {
        if (seed) memcpy(tx_data[i], seed, BLOCK); else memset(tx_data[i], 0, BLOCK);
        rx[i] = (descriptor_t){BLOCK | OWNER, (uint32_t)(uintptr_t)rx_data[i],
            (uint32_t)(uintptr_t)&rx[(i + 1) % RING]};
        tx[i] = (descriptor_t){BLOCK | (BLOCK << 12) | OWNER | EOF_BIT,
            (uint32_t)(uintptr_t)tx_data[i], (uint32_t)(uintptr_t)&tx[(i + 1) % RING]};
    }
    REG(I2S + 0x20) |= 3u; REG(I2S + 0x24) |= 3u;
    REG(I2S + 0x20) &= ~3u; REG(I2S + 0x24) &= ~3u;
    REG(DMA + 0x1c) = 255u; REG(DMA + 0x6c) = 127u;
    REG(I2S + 0x18) = UINT32_MAX;
    REG(DMA + 0x214) = (uint32_t)(uintptr_t)rx; REG(DMA + 0x294) = (uint32_t)(uintptr_t)tx;
    __asm__ volatile ("fence iorw,iorw" ::: "memory");
    REG(DMA + 0x210) = 4u; REG(DMA + 0x290) = 2u;
    if (update(0x20) || update(0x24)) { halt(); failed = 1; return TIKU_S31_AUDIO_IO; }
    REG(I2S + 0x20) |= 4u; REG(I2S + 0x24) |= 4u;
    completed = 0; running = 1; progress = tiku_s31_time(); status.started_ticks = progress;
    return 0;
}
int tiku_s31_audio_poll(int16_t *capture, const int16_t *playback, uint32_t *sequence)
{
    if (!context() || !opened || !running || failed || !capture || !sequence)
        return TIKU_S31_AUDIO_INVALID;
    unsigned in = REG(DMA + 0x10) & 0xfcu, out = REG(DMA + 0x60) & 0x74u;
    unsigned hung = REG(I2S + 0x0c) & 12u;
    if (in || out || hung) {
        status.rx_faults += in != 0 || (hung & 4u) != 0;
        status.tx_faults += out != 0 || (hung & 8u) != 0;
        goto fault;
    }
    if (tiku_s31_time() - progress >= 3200000u) { status.timeouts++; goto fault; }
    unsigned i = completed % RING;
    if (rx[i].control & OWNER) return 0;
    __asm__ volatile ("fence iorw,iorw" ::: "memory");
    if (((rx[i].control >> 12) & 4095u) != BLOCK || (tx[i].control & OWNER)) {
        status.rx_faults++; goto fault;
    }
    memcpy(capture, rx_data[i], BLOCK);
    if (playback) memcpy(tx_data[i], playback, BLOCK); else memset(tx_data[i], 0, BLOCK);
    __asm__ volatile ("fence iorw,iorw" ::: "memory");
    tx[i].control = BLOCK | (BLOCK << 12) | OWNER | EOF_BIT;
    rx[i].control = BLOCK | OWNER;
    __asm__ volatile ("fence iorw,iorw" ::: "memory");
    *sequence = completed++; progress = tiku_s31_time();
    return 1;
fault:
    halt(); failed = 1; REG(0x2058300cu) = 1u << 7;
    return TIKU_S31_AUDIO_IO;
}
#ifdef TIKU_S31_AUDIO_TEST
void tiku_s31_audio_test_loopback(unsigned enable)
{ if (opened && !running) REG(INPUT) = (1u << 9) | (enable ? 5u : 6u); }
#endif
