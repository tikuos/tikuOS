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
#include "cpu1/tiku_cpu1_ipc.h"

#include <kernel/memory/tiku_mem.h>

/*
 * The payload, built for cortex-m33 by the sub-build in arch/ra8p1/cpu1/ and
 * wrapped into this image as bytes.  It is position-independent -- it derives
 * its own base from the PC -- so the two vector words are the only absolute
 * values, and they are patched at load time.
 */
extern const uint8_t _binary_tiku_cpu1_bin_start[];
extern const uint8_t _binary_tiku_cpu1_bin_end[];

#define CPU1_IMG_SIZE \
    ((uint32_t)(_binary_tiku_cpu1_bin_end - _binary_tiku_cpu1_bin_start))

/** @brief The image's home; 128-byte aligned because INITVTOR drops [6:0]. */
static uint8_t cpu1_area[TIKU_CPU1_AREA_SIZE] __attribute__((aligned(128)));

/** @brief The shared page inside it. */
#define CPU1_SH \
    ((volatile tiku_cpu1_shared_t *)(cpu1_area + TIKU_CPU1_SHARED_OFF))

/** @brief Sequence of the last message handed to the payload. */
static uint32_t cpu1_a2c_seq;

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

/** @brief Push this core's half of the page out to where CPU1 reads it. */
static void cpu1_push(void)
{
    tiku_ra8p1_dcache_clean((void *)CPU1_SH, TIKU_CPU1_C2A_OFF);
    __asm__ volatile ("dsb" ::: "memory");
}

/** @brief Pull CPU1's half back in; without this a stale line reads forever. */
static void cpu1_pull(void)
{
    tiku_ra8p1_dcache_invalidate((uint8_t *)CPU1_SH + TIKU_CPU1_C2A_OFF,
                                 sizeof(tiku_cpu1_shared_t) -
                                 TIKU_CPU1_C2A_OFF);
}

/** @brief Publish a halt request to the other core. */
static void cpu1_set_halt(uint32_t halt)
{
    CPU1_SH->halt = halt;
    cpu1_push();
}

int tiku_ra8p1_cpu1_active(void)
{
    return (TIKU_REG16(RA8P1_CPU1ACTCSR) & RA8P1_ACTCSR_ACT) ? 1 : 0;
}

int tiku_ra8p1_cpu1_running(void)
{
    return cpu1_running ? 1 : 0;
}

uint32_t tiku_ra8p1_cpu1_magic(void)
{
    cpu1_pull();
    return CPU1_SH->magic;
}

uint32_t tiku_ra8p1_cpu1_heartbeat(void)
{
    cpu1_pull();
    return CPU1_SH->heartbeat;
}

int tiku_ra8p1_cpu1_send(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;

    if (data == 0 || len == 0U || len > TIKU_CPU1_MSG_CAP) {
        return TIKU_RA8P1_CPU1_ERR_LEN;
    }
    if (!cpu1_running) {
        return TIKU_RA8P1_CPU1_ERR_ACT;
    }

    for (i = 0U; i < len; i++) {
        CPU1_SH->a2c_buf[i] = p[i];
    }
    CPU1_SH->a2c_len = len;

    /* Sequence last, and pushed on its own: the payload treats a changed
     * sequence as proof the buffer beside it is already complete. */
    cpu1_push();
    cpu1_a2c_seq++;
    CPU1_SH->a2c_seq = cpu1_a2c_seq;
    cpu1_push();
    return TIKU_RA8P1_CPU1_OK;
}

uint32_t tiku_ra8p1_cpu1_reply_seq(void)
{
    cpu1_pull();
    return CPU1_SH->c2a_seq;
}

uint32_t tiku_ra8p1_cpu1_reply(void *out, uint32_t cap)
{
    uint8_t *p = (uint8_t *)out;
    uint32_t len;
    uint32_t i;

    cpu1_pull();
    if (CPU1_SH->c2a_seq != cpu1_a2c_seq) {
        return 0U;                      /* nothing answered this send yet */
    }
    len = CPU1_SH->c2a_len;
    if (len > TIKU_CPU1_MSG_CAP) {
        len = TIKU_CPU1_MSG_CAP;
    }
    if (out == 0 || cap == 0U) {
        return len;
    }
    if (len > cap) {
        len = cap;
    }
    for (i = 0U; i < len; i++) {
        p[i] = CPU1_SH->c2a_buf[i];
    }
    return len;
}

uint32_t tiku_ra8p1_cpu1_image_size(void)
{
    return CPU1_IMG_SIZE;
}

int tiku_ra8p1_cpu1_alive(void)
{
    uint32_t a;
    uint32_t i;

    if (!cpu1_running || tiku_ra8p1_cpu1_magic() != TIKU_CPU1_MAGIC) {
        return 0;
    }
    /* A counter that is merely non-zero proves the payload ran once; only a
     * moving one proves it is still executing. */
    a = tiku_ra8p1_cpu1_heartbeat();
    for (i = 0U; i < 20000U; i++) {
        __asm__ volatile ("nop");
    }
    return (tiku_ra8p1_cpu1_heartbeat() != a) ? 1 : 0;
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

    if (CPU1_IMG_SIZE == 0U || CPU1_IMG_SIZE > sizeof(cpu1_area)) {
        return TIKU_RA8P1_CPU1_ERR_IMG;
    }

    /* Zero before copy, always: a shorter image laid over a longer one
     * otherwise leaves the previous tail live behind it. */
    for (i = 0U; i < sizeof(cpu1_area); i++) {
        cpu1_area[i] = 0U;
    }
    for (i = 0U; i < CPU1_IMG_SIZE; i++) {
        cpu1_area[i] = _binary_tiku_cpu1_bin_start[i];
    }
    vec[0] = base + TIKU_CPU1_STACK_OFF;
    vec[1] = (base + TIKU_CPU1_RESET_OFF) | 1U;
    cpu1_a2c_seq = 0U;

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
