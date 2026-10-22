/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.c - AP x16 PSRAM on P4 v1.3, its bus at up to 200 MHz.
 *
 * LDO VO2, the MPLL, device setup at 20 MHz, strobe calibration at 200 MHz,
 * 32 MiB mapped; the bus follows the CPU.  ESP-IDF 4d59230d and 383dda6c.
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
#define TIKU_P4_PS_RESUME(s) ((void (*)(uint32_t))0x4fc0050cu)(s)
#define TIKU_P4_PS_ENABLE() ((void (*)(unsigned))0x4fc00504u)(0)
#define TIKU_P4_PS_DRAIN() drain_l1()
#define TIKU_P4_PS_INVALIDATE() \
    ((int (*)(unsigned, uint32_t, uint32_t))0x4fc003e4u) \
        (0x31, TIKU_P4_PSRAM_BASE, TIKU_P4_PSRAM_BYTES)
#define TIKU_P4_PS_FENCE() __asm__ volatile("fence iorw, iorw\nfence.i" ::: "memory")
#define TIKU_P4_PS_WORD(i) (((volatile const uint32_t *)TIKU_P4_PSRAM_BASE)[i])
/** @brief Write back every dirty L1 line; ECO2 needs the command twice. */
static void drain_l1(void)
{
    unsigned pass, tries;
    for (pass = 0; pass < 2; pass++) {
        TIKU_P4_REG_WRITE(0x3ff1009cu, 0x10u);
        TIKU_P4_REG_WRITE(0x3ff100a0u, 0);
        TIKU_P4_REG_WRITE(0x3ff100a4u, 0);
        TIKU_P4_REG_WRITE(0x3ff10098u, 4u);
        for (tries = 0; tries < 1000000u; tries++)
            if (TIKU_P4_REG_READ(0x3ff10098u) & 16u) break;
    }
}
#endif
/* The speed: the bus divider of the 400 MHz MPLL, the device's latency
 * codes and the controller's wait cycles to match (2 * (cycles - 1)). */
#define PS_MPLL_MHZ     400u
#define PS_DIV_FAST     2u      /* 200 MHz */
#define PS_DIV_SETUP    20u     /* 20 MHz: mode registers, the reference */
#define PS_MR0          0x30u   /* read latency code 4 (14 cycles), fixed */
#define PS_MR4          0x20u   /* write latency code 1 (7 cycles) */
#define PS_MR8          0x4bu   /* x16, 2 KB wrap, hybrid burst */
#define PS_RD_DUMMY     26u     /* 14 cycles */
#define PS_WR_DUMMY     12u     /* 7 cycles; register reads too */
#define PS_REF_ADDR     0x80u   /* where the calibration pattern lives */
#define PS_PADS_N       20u     /* pad registers from PS_PADS + 0x1c */
#define PS_PAD(n)       (PS_PADS + 0x1cu + 4u * (n))
#define PS_PAD_DQS(n)   ((n) == 8u || (n) == 19u)
#define PS_DELAYS       31u     /* {0,15}..{0,0}..{15,0} as {data, strobe} */
#define PS_DELAY_READS  100u

/* The calibration pattern (ESP-IDF mspi_timing_by_dqs.c). */
static const uint32_t reference[32] = {
    0x7f786655u, 0xa5ff005au, 0x3f3c33aau, 0xa5ff5a00u,
    0x1f1e9955u, 0xa5005affu, 0x0f0fccaau, 0xa55a00ffu,
    0x07876655u, 0xffa55a00u, 0x03c333aau, 0xff00a55au,
    0x01e19955u, 0xff005aa5u, 0x00f0ccaau, 0xff5a00a5u,
    0x80786655u, 0x00a5ff5au, 0xc03c33aau, 0x00a55affu,
    0xe01e9355u, 0x00ff5aa5u, 0xf00fccaau, 0x005affa5u,
    0xf8876655u, 0x5aa5ff00u, 0xfcc333aau, 0x5affa500u,
    0xfee19955u, 0x5a00a5ffu, 0x11f0ccaau, 0x5a00ffa5u,
};

static uint32_t capacity;
static unsigned stage, attempted;
static uint32_t phase_pass, delay_pass;
static unsigned phase_best, delay_best;

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

/** @brief The bus clock word for divider @p div (even): N, H and L. */
static uint32_t bus_clock(unsigned div)
{
    return ((div - 1u) << 16) | ((div / 2u - 1u) << 8) | (div - 1u);
}

/** @brief Configure dedicated PSRAM pads and both SPI2/SPI3 clock dividers
 *         at the 20 MHz the setup runs at. */
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
    TIKU_P4_REG_WRITE(PS_SYS + 0x50, bus_clock(PS_DIV_SETUP));
    TIKU_P4_REG_WRITE(PS_PIO + 0x14, bus_clock(PS_DIV_SETUP));
    field(PS_SYS + 0x190, 1u << 5, 1u << 5);
    field(PS_SYS + 0x180, 1u << 5, 1u << 5);
    return (TIKU_P4_REG_READ(PS_CLOCK) & 0x00fff000u) == 0xd000u &&
           TIKU_P4_REG_READ(PS_PIO + 0x14) == bus_clock(PS_DIV_SETUP);
}

/** @brief Set the 200 MHz latencies (fixed) and x16 bursts, then require
 *         AP identity and a readback, at the setup clock. */
static int device(void)
{
    uint32_t word = PS_MR0, mr0, mr2, mr4, mr8;
    unsigned i;
    uint32_t written[16], received[16];
    if (!transfer(0xc0c0, 0, 0, &word, 2, 1)) return 0;
    word = PS_MR4;
    if (!transfer(0xc0c0, 4, 0, &word, 2, 1)) return 0;
    word = PS_MR8;
    if (!transfer(0xc0c0, 8, 0, &word, 2, 1) ||
        !transfer(0x4040, 0, PS_WR_DUMMY, &mr0, 2, 0) ||
        !transfer(0x4040, 2, PS_WR_DUMMY, &mr2, 2, 0) ||
        !transfer(0x4040, 4, PS_WR_DUMMY, &mr4, 2, 0) ||
        !transfer(0x4040, 8, PS_WR_DUMMY, &mr8, 2, 0)) return 0;
    if ((mr0 & 0x1f3fu) != (0x0d00u | PS_MR0) || (mr2 & 0xe7u) != 0xc7u ||
        (mr4 & 0xffu) != PS_MR4 || (mr8 & 0x4fu) != PS_MR8) return 0;
    stage = 3;
    for (i = 0; i < 16; i++) written[i] = 0x5a6b7c8du ^ (i * 0x1234567u);
    if (!transfer(0x8080, 0, PS_WR_DUMMY, written, 64, 1) ||
        !transfer(0, 0, PS_RD_DUMMY, received, 64, 0)) return 0;
    for (i = 0; i < 16; i++) if (received[i] != written[i]) return 0;
    return 1;
}

/** @brief Strobe phase @p p (0..3: 67.5 to 101.25 degrees) on both DQS pads. */
static void set_phase(unsigned p)
{
    field(PS_PAD(8), 3u << 1, p << 1);
    field(PS_PAD(19), 3u << 1, p << 1);
}

/** @brief Delay-line entry @p i: the strobe pads' 90 and 270 degree delays,
 *         every other pad's data delay. */
static void set_delay(unsigned i)
{
    unsigned n, data = i > 15u ? i - 15u : 0u, dqs = i < 15u ? 15u - i : 0u;

    for (n = 0; n < PS_PADS_N; n++) {
        if (PS_PAD_DQS(n)) {
            field(PS_PAD(n), (15u << 7) | (15u << 17), (dqs << 7) | (dqs << 17));
        } else {
            field(PS_PAD(n), 15u << 4, data << 4);
        }
    }
}

/** @brief Read the pattern back at the bus clock; @return 1 when it matches. */
static int pattern_reads(void)
{
    uint32_t got[32];
    unsigned i;

    if (!transfer(0, PS_REF_ADDR, PS_RD_DUMMY, got, 64, 0) ||
        !transfer(0, PS_REF_ADDR + 64u, PS_RD_DUMMY, got + 16, 64, 0)) return 0;
    for (i = 0; i < 32; i++) if (got[i] != reference[i]) return 0;
    return 1;
}

/** @brief The longest run of set bits in the low @p n of @p mask: its
 *         length, and its last bit in *@p end. */
static unsigned longest(uint32_t mask, unsigned n, unsigned *end)
{
    unsigned i, run = 0, best = 0;

    for (i = 0; i < n; i++) {
        run = (mask >> i) & 1u ? run + 1 : 0;
        if (run > best) {
            best = run;
            *end = i;
        }
    }
    return best;
}

/** @brief Write the pattern at the setup clock, move both controllers to
 *         200 MHz, then choose the strobe phase (the first of the longest
 *         passing run) and the delay entry (the middle of the longest run of
 *         entries that read it back 100 times). */
static int calibrate(void)
{
    unsigned p, i, k, end = 0, len;
    uint32_t pattern[32];

    for (i = 0; i < 32; i++) pattern[i] = reference[i];
    if (!transfer(0x8080, PS_REF_ADDR, PS_WR_DUMMY, pattern, 64, 1) ||
        !transfer(0x8080, PS_REF_ADDR + 64u, PS_WR_DUMMY, pattern + 16, 64, 1))
        return 0;
    TIKU_P4_REG_WRITE(PS_SYS + 0x50, bus_clock(PS_DIV_FAST));
    TIKU_P4_REG_WRITE(PS_PIO + 0x14, bus_clock(PS_DIV_FAST));
    field(PS_PIO + 0xd4, 2, 0);
    set_delay(15u);
    phase_pass = 0;
    for (p = 0; p < 4; p++) {
        set_phase(p);
        if (pattern_reads()) phase_pass |= 1u << p;
    }
    len = longest(phase_pass, 4, &end);
    if (len == 0) return 0;
    phase_best = end - len + 1;
    set_phase(phase_best);
    delay_pass = 0;
    for (i = 0; i < PS_DELAYS; i++) {
        set_delay(i);
        for (k = 0; k < PS_DELAY_READS && pattern_reads(); k++) {
        }
        if (k == PS_DELAY_READS) delay_pass |= 1u << i;
    }
    len = longest(delay_pass, PS_DELAYS, &end);
    if (len <= 1) return 0;
    delay_best = end - len / 2;
    set_delay(delay_best);
    return 1;
}

/** @brief Map exactly 512 64-KiB pages, checking the whole 1024-entry table. */
static int mapping(void)
{
    unsigned page;
    TIKU_P4_PS_SUSPEND();
    TIKU_P4_REG_WRITE(PS_SYS + 0x48, 0xf0000000u);
    TIKU_P4_REG_WRITE(PS_SYS + 0x4c, 0xf0008080u);
    field(PS_SYS + 0x40, 0x0fffffff, 1u | 0x38u | ((PS_RD_DUMMY - 1u) << 6) |
          (31u << 14) | (3u << 20) | ((PS_WR_DUMMY - 1u) << 22));
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
            if (device() && calibrate()) {
                stage = 4;
                ok = mapping();
            }
        }
    }
    if (ok) { capacity = TIKU_P4_PSRAM_BYTES; stage = 5; }
    else if (stage) field(PS_SYS + 0x3c, PS_CLOSE | 1, PS_CLOSE);
    TIKU_P4_IRQ_RESTORE(irq);
    if (tiku_p4_clock_set_mhz(mhz)) { capacity = 0; return -1; }
    if (ok) {
        tiku_p4_clock_follow(tiku_p4_psram_follow_cpu);
        tiku_p4_psram_follow_cpu(mhz);
    }
    return ok ? 0 : -1;
}

uint32_t tiku_p4_psram_size(void) { return capacity; }

/** @brief The bus divider under a CPU at @p cpu_mhz: the fastest of 200,
 *         100, 66 and 40 MHz not above the CPU clock.  A bus faster than the
 *         CPU loses DMA writes (a copy within PSRAM at 200 MHz under a 40 MHz
 *         CPU comes back wrong). */
static unsigned divider_for(unsigned cpu_mhz)
{
    unsigned div = PS_DIV_FAST;
    while (div < 10u && PS_MPLL_MHZ / div > cpu_mhz) div += 2u;
    return div;
}

void tiku_p4_psram_follow_cpu(unsigned cpu_mhz)
{
    uint32_t word, state, autoload;
    unsigned tries;

    if (!capacity) return;
    word = bus_clock(divider_for(cpu_mhz));
    if (TIKU_P4_REG_READ(PS_SYS + 0x50) == word) return;
    state = TIKU_P4_IRQ_SAVE();
    TIKU_P4_PS_FENCE();
    TIKU_P4_PS_DRAIN();
    autoload = TIKU_P4_PS_SUSPEND();
    field(PS_SYS + 0x3c, PS_CLOSE, PS_CLOSE);
    for (tries = 0; tries < 65536u && (TIKU_P4_REG_READ(PS_SYS) & 255u); tries++) {
    }
    TIKU_P4_REG_WRITE(PS_SYS + 0x50, word);
    TIKU_P4_REG_WRITE(PS_PIO + 0x14, word);
    field(PS_SYS + 0x3c, PS_CLOSE, 0);
    TIKU_P4_PS_RESUME(autoload);
    TIKU_P4_PS_FENCE();
    TIKU_P4_IRQ_RESTORE(state);
}

uint32_t tiku_p4_psram_hz(void)
{
    uint32_t div = ((TIKU_P4_REG_READ(PS_SYS + 0x50) >> 16) & 255u) + 1u;
    return capacity ? PS_MPLL_MHZ * 1000000u / div : 0;
}

void tiku_p4_psram_calibration(uint32_t *phases, uint32_t *delays,
                               unsigned *phase, unsigned *delay)
{
    *phases = phase_pass;
    *delays = delay_pass;
    *phase = phase_best;
    *delay = delay_best;
}
unsigned tiku_p4_psram_stage(void) { return stage; }
