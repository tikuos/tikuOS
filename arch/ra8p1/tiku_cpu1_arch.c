/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1_arch.c - run a payload on the RA8P1's Cortex-M33.
 *
 * Loads a position-independent image into shared SRAM, points CPU1 at it and
 * releases it from power gating.  Liveness and halt travel through a shared
 * page, since the activation registers offer no way back.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "tiku_cpu1_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_common.h"
#include "tiku_cache_arch.h"

#include <kernel/memory/tiku_mem.h>

/*
 * The payload, assembled for cortex-m33 and pasted rather than built, so this
 * milestone needs no sub-build.  `adr` keeps the shared words PC-relative, so
 * the image runs wherever the loader puts it; the two vector words are the
 * only absolute values and are patched at load time.
 *
 *     .syntax unified
 *     .cpu cortex-m33
 *     .thumb
 *     .align 7                 @ INITVTOR discards bits [6:0]
 * vectors:
 *     .word 0                  @ SP    -- patched at load
 *     .word 0                  @ reset -- patched at load
 *     .rept 14 / .word 0 / .endr
 *     .align 2
 * reset:
 *     adr  r0, hbdata          @ magic + heartbeat
 *     adr  r1, haltw           @ halt request, a different cache line
 *     ldr  r2, =0x4D333350
 *     str  r2, [r0, #0]
 *     movs r2, #0
 * 1:  ldr  r3, [r1]
 *     cmp  r3, #0
 *     bne  2f
 *     adds r2, #1
 *     str  r2, [r0, #4]
 *     b    1b
 * 2:  ldr  r3, [r1]            @ halted: poll until the owner clears it
 *     cmp  r3, #0
 *     bne  2b
 *     b    1b                  @ and resume
 *     .align 5
 * hbdata: .word 0 / .word 0
 *     .align 5
 * haltw:  .word 0
 */
static const uint8_t cpu1_img[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x07, 0xA0, 0x0F, 0xA1, 0x0F, 0x4A, 0x02, 0x60,
    0x00, 0x22, 0x0B, 0x68, 0x00, 0x2B, 0x02, 0xD1, 0x01, 0x32, 0x42, 0x60,
    0xF9, 0xE7, 0x0B, 0x68, 0x00, 0x2B, 0xFC, 0xD1, 0xF5, 0xE7, 0x00, 0xBF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAF, 0xF3, 0x00, 0x80,
    0xAF, 0xF3, 0x00, 0x80, 0xAF, 0xF3, 0x00, 0x80, 0xAF, 0xF3, 0x00, 0x80,
    0xAF, 0xF3, 0x00, 0x80, 0xAF, 0xF3, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x50, 0x33, 0x33, 0x4D,
};

/*
 * Offsets into the image.  The halt word has a cache line to itself: the
 * owner writes it while invalidating the heartbeat line, and on a shared line
 * one operation discards the other.
 */
#define CPU1_RESET_OFF   0x40U
#define CPU1_HB_OFF      0x60U      /* magic at +0, heartbeat at +4 */
#define CPU1_HALT_OFF    0x80U
#define CPU1_STACK_OFF   0x400U

/** @brief The image's home; 128-byte aligned because INITVTOR drops [6:0]. */
static uint8_t cpu1_area[1024] __attribute__((aligned(128)));

/** @brief Whether a payload is counting, which no register reports. */
static uint8_t cpu1_running;

/** @brief Set once the warm-persist counter below has been seeded. */
static uint8_t cpu1_nmi_seeded;

/*
 * A CPU1 lockup arrives here: CPUnLCKUPCR.OAD resets to 0, routing it to a
 * non-maskable interrupt, and the NMI vector otherwise resolves to the fault
 * recorder, which resets the board.  A coprocessor that wedges must not take
 * the application core with it.  Strong here, so the weak default stands in
 * builds without this driver.
 */
TIKU_PERSIST_WARM volatile uint32_t tiku_ra8p1_cpu1_nmi_count;

void tiku_ra8p1_nmi_handler(void)
{
    tiku_ra8p1_cpu1_nmi_count++;
    cpu1_running = 0U;
}

/** @brief Unlock or relock the CPU-control registers CRPT guards. */
static void cpu1_protect(int unlock)
{
    TIKU_REG16(RA8P1_CPU1CRPT) = (uint16_t)(RA8P1_CPUCTRL_KEY |
                                            (unlock ? 0U : RA8P1_CRPT_PROTECT));
}

/** @brief Publish a halt request to the other core. */
static void cpu1_set_halt(uint32_t halt)
{
    *(volatile uint32_t *)(cpu1_area + CPU1_HALT_OFF) = halt;
    tiku_ra8p1_dcache_clean(cpu1_area + CPU1_HALT_OFF, 32U);
    __asm__ volatile ("dsb" ::: "memory");
}

int tiku_ra8p1_cpu1_active(void)
{
    return (TIKU_REG16(RA8P1_CPU1ACTCSR) & RA8P1_ACTCSR_ACT) ? 1 : 0;
}

int tiku_ra8p1_cpu1_running(void)
{
    return cpu1_running ? 1 : 0;
}

/*
 * CPU1 writes the shared words behind this core's D-cache, so a read without
 * an invalidate serves a stale line indefinitely.
 */
uint32_t tiku_ra8p1_cpu1_magic(void)
{
    tiku_ra8p1_dcache_invalidate(cpu1_area + CPU1_HB_OFF, 32U);
    return *(volatile uint32_t *)(cpu1_area + CPU1_HB_OFF);
}

uint32_t tiku_ra8p1_cpu1_heartbeat(void)
{
    tiku_ra8p1_dcache_invalidate(cpu1_area + CPU1_HB_OFF, 32U);
    return *(volatile uint32_t *)(cpu1_area + CPU1_HB_OFF + 4U);
}

void tiku_ra8p1_cpu1_stop(void)
{
    uint32_t last, i, settle;

    cpu1_set_halt(1UL);

    /* Wait for the payload to reach its halt rather than assume it did: two
     * identical heartbeats mean it has stopped counting. */
    last = tiku_ra8p1_cpu1_heartbeat();
    for (settle = 0U; settle < 100U; settle++) {
        for (i = 0U; i < 20000U; i++) {
            __asm__ volatile ("nop");
        }
        if (tiku_ra8p1_cpu1_heartbeat() == last) {
            break;
        }
        last = tiku_ra8p1_cpu1_heartbeat();
    }
    cpu1_running = 0U;
}

int tiku_ra8p1_cpu1_start(void)
{
    volatile uint32_t *vec = (volatile uint32_t *)cpu1_area;
    uint32_t base = (uint32_t)(uintptr_t)cpu1_area;
    unsigned long spins;
    uint32_t i;

    if (!cpu1_nmi_seeded) {
        /* Warm-persist memory is not zeroed at a cold boot. */
        tiku_ra8p1_cpu1_nmi_count = 0U;
        cpu1_nmi_seeded = 1U;
    }

    /*
     * An already-active core cannot be launched twice: ACTREQ acts only "if
     * ACT is 0", and nothing returns CPU1 to power gating.  A second start
     * therefore RESUMES the payload, and must not rewrite an image the core
     * is executing at the time.
     */
    if (tiku_ra8p1_cpu1_active()) {
        cpu1_set_halt(0UL);
        cpu1_running = 1U;
        return TIKU_RA8P1_CPU1_OK;
    }

    for (i = 0U; i < sizeof(cpu1_area); i++) {
        cpu1_area[i] = 0U;
    }
    for (i = 0U; i < sizeof(cpu1_img); i++) {
        cpu1_area[i] = cpu1_img[i];
    }
    vec[0] = base + CPU1_STACK_OFF;
    vec[1] = (base + CPU1_RESET_OFF) | 1U;

    /* The image arrived through this core's write-back D-cache; CPU1 fetches
     * straight from SRAM and would see zeros. */
    tiku_ra8p1_dcache_clean(cpu1_area, sizeof(cpu1_area));
    __asm__ volatile ("dsb" ::: "memory");

    /*
     * VECTOR BASE BEFORE ACTIVATION, and the order is the whole of it.
     * INITVTOR is latched as the core leaves reset, which activation
     * triggers; set afterwards it changes nothing, and CPU1 boots from the
     * register's reset value 0x0200_0000 -- the M85's own vector table.  An
     * M33 running the M85's image shares its stack and paints it.
     */
    cpu1_protect(1);
    TIKU_REG32(RA8P1_CPU1INITVTOR) = base;
    TIKU_REG8(RA8P1_CPU1WAITCR) = 0U;
    TIKU_REG16(RA8P1_CPU1ACTCSR) = (uint16_t)(RA8P1_CPUCTRL_KEY |
                                              RA8P1_ACTCSR_ACTREQ);
    for (spins = 1000000UL; spins != 0UL; spins--) {
        if (tiku_ra8p1_cpu1_active()) {
            break;
        }
    }
    cpu1_protect(0);

    cpu1_running = (spins != 0UL) ? 1U : 0U;
    return (spins != 0UL) ? TIKU_RA8P1_CPU1_OK : TIKU_RA8P1_CPU1_ERR_ACT;
}
