/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.c - AP x16 PSRAM at 80 MHz on P4 silicon v1.3.
 *
 * Powers LDO VO2, starts the MPLL, pads and device, maps 32 MiB at 0x48000000.
 * Register sequence: ESP-IDF 4d59230ddff16327812782151ef0afef202dc6d7.
 *
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_psram_arch.h"
#include "tiku_pmu_arch.h"
#include "tiku_clock_arch.h"
#include "tiku_cache_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32p4_regs.h"

#define PS_SYS 0x5008e000u
#define PS_PIO 0x5008f000u
#define PS_PADS 0x500e1200u
#define PS_CLOCK 0x500e6030u
#define PS_CLOSE (1u << 31)

/** @brief ECO2 ROM command configuration; pointers refer to internal SRAM. */
typedef struct {
    uint16_t command, command_bits;
    uint32_t *address;
    uint32_t address_bits;
    uint32_t *tx;
    uint32_t tx_bits;
    uint32_t *rx;
    uint32_t rx_bits, dummy_bits;
} ps_command_t;

#ifndef TIKU_P4_PS_MODE
#define TIKU_P4_PS_MODE() ((void (*)(int, int))0x4fc00110u)(3, 7)
#define TIKU_P4_PS_CONFIG(c) ((void (*)(int, ps_command_t *))0x4fc00108u)(3, c)
#define TIKU_P4_PS_SUSPEND() ((uint32_t (*)(void))0x4fc00508u)()
#define TIKU_P4_PS_ENABLE() ((void (*)(unsigned))0x4fc00504u)(0)
#define TIKU_P4_PS_INVALIDATE() \
    ((int (*)(unsigned, uint32_t, uint32_t))0x4fc003e4u) \
        (0x31, TIKU_P4_PSRAM_BASE, TIKU_P4_PSRAM_BYTES)
#define TIKU_P4_PS_FENCE() __asm__ volatile("fence iorw, iorw\nfence.i" ::: "memory")
#define TIKU_P4_PS_WORD(i) (((volatile const uint32_t *)TIKU_P4_PSRAM_BASE)[i])
#endif
static uint32_t capacity;
static unsigned stage, attempted;

/** @brief Change one register field without modifying unrelated bits. */
static void field(uintptr_t address, uint32_t mask, uint32_t value)
{
    TIKU_P4_REG_WRITE(address, (TIKU_P4_REG_READ(address) & ~mask) | value);
}

/** @brief Bound a SPI3 command to 65536 polls. */
static int idle(void)
{
    unsigned tries;
    for (tries = 0; tries < 65536; tries++)
        if (!(TIKU_P4_REG_READ(PS_PIO) & 0xfffe0000u)) return 1;
    return 0;
}

/** @brief Transfer up to 64 bytes using ROM configuration and bounded start.
 * @note Interrupts are masked, SPI2 AXI is closed, buffers are word-aligned SRAM.
 */
static int transfer(uint16_t command, uint32_t address, unsigned dummy,
                    uint32_t *data, unsigned bytes, int write)
{
    ps_command_t config = {command, 16, &address, 32,
        write ? data : 0, write ? bytes * 8 : 0,
        write ? 0 : data, write ? 0 : bytes * 8, dummy};
    unsigned i;
    if (!data || !bytes || bytes > 64 || !idle()) return 0;
    TIKU_P4_PS_MODE();
    TIKU_P4_PS_CONFIG(&config);
    field(PS_PIO + 0x34, 3, 1);
    TIKU_P4_REG_WRITE(PS_PIO, 1u << 18);
    if (!idle()) return 0;
    if (!write) for (i = 0; i < (bytes + 3) / 4; i++)
        data[i] = TIKU_P4_REG_READ(PS_PIO + 0x58 + i * 4);
    return 1;
}

/** @brief Configure dedicated PSRAM pads and both SPI2/SPI3 clock dividers. */
static int controller(void)
{
    unsigned offset;
    field(TIKU_P4_SOC_CLK0, 1u << 31, 1u << 31);
    field(PS_CLOCK, 0x00fff000u, 0x0000d000u);
    field(TIKU_P4_HP_RESET0, (1u << 23) | (1u << 25), (1u << 23) | (1u << 25));
    field(TIKU_P4_HP_RESET0, 1u << 25, 0);
    field(TIKU_P4_HP_RESET0, 1u << 23, 0);
    field(PS_SYS + 0x3c, PS_CLOSE | 1, PS_CLOSE);
    for (offset = 0x1c; offset <= 0x68; offset += 4) {
        unsigned shift = offset == 0x3c || offset == 0x68 ? 15 : 12;
        field(PS_PADS + offset, 3u << shift, 2u << shift);
    }
    field(PS_PADS + 0x3c, 1, 1);
    field(PS_PADS + 0x68, 1, 1);
    field(PS_SYS + 0x1a0, 0xfe01ffffu, 0x8400018fu);
    field(PS_SYS + 0x174, 0x001c0000u, 3u << 18);
    TIKU_P4_REG_WRITE(PS_SYS + 0x50, 0x00030103u);
    TIKU_P4_REG_WRITE(PS_PIO + 0x14, 0x00030103u);
    field(PS_SYS + 0x190, 1u << 5, 1u << 5);
    field(PS_SYS + 0x180, 1u << 5, 1u << 5);
    return (TIKU_P4_REG_READ(PS_CLOCK) & 0x00fff000u) == 0xd000u &&
           TIKU_P4_REG_READ(PS_PIO + 0x14) == 0x00030103u;
}

/** @brief Set fixed latency and x16 bursts, then require AP identity/readback. */
static int device(void)
{
    uint32_t word = 0x28, mr0, mr2, mr4, mr8;
    unsigned i;
    uint32_t written[16], received[16];
    if (!transfer(0xc0c0, 0, 0, &word, 2, 1)) return 0;
    word = 0x40;
    if (!transfer(0xc0c0, 4, 0, &word, 2, 1)) return 0;
    word = 0x4b;
    if (!transfer(0xc0c0, 8, 0, &word, 2, 1) ||
        !transfer(0x4040, 0, 8, &mr0, 2, 0) ||
        !transfer(0x4040, 2, 8, &mr2, 2, 0) ||
        !transfer(0x4040, 4, 8, &mr4, 2, 0) ||
        !transfer(0x4040, 8, 8, &mr8, 2, 0)) return 0;
    if ((mr0 & 0x1f3fu) != 0x0d28u || (mr2 & 0xe7u) != 0xc7u ||
        (mr4 & 0xffu) != 0x40u || (mr8 & 0x4fu) != 0x4bu) return 0;
    stage = 3;
    for (i = 0; i < 16; i++) written[i] = 0x5a6b7c8du ^ (i * 0x1234567u);
    if (!transfer(0x8080, 0, 8, written, 64, 1) ||
        !transfer(0, 0, 18, received, 64, 0)) return 0;
    for (i = 0; i < 16; i++) if (received[i] != written[i]) return 0;
    return 1;
}

/** @brief Map exactly 512 64-KiB pages, checking the whole 1024-entry table. */
static int mapping(void)
{
    unsigned page;
    TIKU_P4_PS_SUSPEND();
    TIKU_P4_REG_WRITE(PS_SYS + 0x48, 0xf0000000u);
    TIKU_P4_REG_WRITE(PS_SYS + 0x4c, 0xf0008080u);
    field(PS_SYS + 0x40, 0x0fffffff, 1u | 0x38u | (17u << 6) |
          (31u << 14) | (3u << 20) | (7u << 22));
    field(PS_SYS + 0x44, 0x0ffffc00, (0xfu << 18) | (1u << 23) | (3u << 26));
    field(PS_SYS + 0xd8, 15, 3);
    field(PS_PIO + 0xd4, 2, 2);
    field(PS_SYS + 0x0c, 3u << 25, 3u << 25);
    for (page = 0; page < 1024; page++) {
        uint32_t value = page < 512 ? 0x0c00u | page : 0;
        TIKU_P4_REG_WRITE(PS_SYS + 0x380, page);
        TIKU_P4_REG_WRITE(PS_SYS + 0x37c, value);
        if (TIKU_P4_REG_READ(PS_SYS + 0x37c) != value) goto failed;
    }
    if (TIKU_P4_PS_INVALIDATE()) goto failed;
    field(PS_SYS + 0x3c, PS_CLOSE | 1, 1);
    TIKU_P4_PS_ENABLE();
    TIKU_P4_PS_FENCE();
    /* SPI3 sends eight data bits per transfer; SPI2 maps both byte lanes.
     * The 64 PIO bytes occupy the low byte of 64 consecutive halfwords. */
    for (page = 0; page < 32; page++) {
        uint32_t expected = 0x5a6b7c8du ^ ((page / 2) * 0x1234567u);
        uint32_t got = TIKU_P4_PS_WORD(page);
        expected = (expected >> (16 * (page % 2))) & 0xffff;
        got = (got & 255u) | ((got >> 8) & 0xff00u);
        if (got != expected) return 0;
    }
    return 1;
failed:
    for (page = 0; page < 1024; page++) {
        TIKU_P4_REG_WRITE(PS_SYS + 0x380, page);
        TIKU_P4_REG_WRITE(PS_SYS + 0x37c, 0);
    }
    TIKU_P4_PS_ENABLE();
    TIKU_P4_PS_FENCE();
    return 0;
}

int tiku_p4_psram_init(void)
{
    uint32_t irq;
    unsigned mhz;
    int ok = 0;
    if (tiku_p4_in_isr()) return -1;
    if (capacity) return 0;
    if (attempted || !tiku_p4_cache_layout_ok()) return -1;
    attempted = 1;
    mhz = TIKU_P4_REG_READ(TIKU_P4_ROOT_SELECT) & 3u;
    mhz = mhz == 0 ? 40 : mhz == 1 ?
        360u / (1u + ((TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV0) >> 5) & 255u)) : 0;
    if (mhz != 40 && mhz != 90 && mhz != 180 && mhz != 360) return -1;
    if (tiku_p4_clock_set_mhz(40)) return -1;
    irq = TIKU_P4_IRQ_SAVE();
    if (!tiku_p4_pmu_psram_ldo_init() && !tiku_p4_pmu_mpll_init()) {
        stage = 1;
        if (controller()) {
            stage = 2;
            if (device()) {
                stage = 4;
                ok = mapping();
            }
        }
    }
    if (ok) { capacity = TIKU_P4_PSRAM_BYTES; stage = 5; }
    else if (stage) field(PS_SYS + 0x3c, PS_CLOSE | 1, PS_CLOSE);
    TIKU_P4_IRQ_RESTORE(irq);
    if (tiku_p4_clock_set_mhz(mhz)) { capacity = 0; return -1; }
    return ok ? 0 : -1;
}

uint32_t tiku_p4_psram_size(void) { return capacity; }
unsigned tiku_p4_psram_stage(void) { return stage; }
