/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1_arch.c - run a payload on the RA8P1's Cortex-M33.
 *
 * Copies the payload to its fixed SRAM carve, points CPU1 at it and releases
 * it from power gating.  Liveness, halt and messages go through a shared
 * page; the activation registers cannot return CPU1 to power gating.
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
 * wrapped into this image as bytes.  It is linked at the SRAM carve and must
 * be copied there; SP, reset, NMI and HardFault are patched into its vector
 * table at load time.
 */
extern const uint8_t _binary_tiku_cpu1_bin_start[];
extern const uint8_t _binary_tiku_cpu1_bin_end[];

#define CPU1_IMG_SIZE \
    ((uint32_t)(_binary_tiku_cpu1_bin_end - _binary_tiku_cpu1_bin_start))

/** @brief The image's home: the fixed SRAM carve the payload links at. */
#define cpu1_area   ((uint8_t *)TIKU_CPU1_AREA_ADDR)

/** @brief The shared page inside it. */
#define CPU1_SH \
    ((volatile tiku_cpu1_shared_t *)(cpu1_area + TIKU_CPU1_SHARED_OFF))

/** @brief Sequence of the last message handed to the payload. */
static uint32_t cpu1_a2c_seq;

/** @brief Whether a payload is counting, which no register reports. */
static uint8_t cpu1_running;

/** @brief Validity gate for the retained diagnostic counters. */
static TIKU_RETAINED uint32_t cpu1_diag_magic;

/*
 * NMIs taken by the M85, counted by tiku_ra8p1_nmi_handler() below.  A CPU1
 * lockup raises none, so payload faults are reported through the shared
 * magic.  The handler is the strong definition; a build without this driver
 * keeps the weak default from tiku_crt_early.c, which records a fault and
 * resets.
 */
TIKU_RETAINED volatile uint32_t tiku_ra8p1_cpu1_nmi_count;

/** @brief Faults the payload has reported through the shared magic. */
TIKU_RETAINED volatile uint32_t tiku_ra8p1_cpu1_fault_count;

/** @brief Set once the current fault has been counted. */
static uint8_t cpu1_fault_noticed;

/** @brief Restart generation last written to a faulted core. */
static uint32_t cpu1_restart_gen;

/** @brief Set by the doorbell interrupt, cleared by
 *         tiku_ra8p1_cpu1_bell_take(). */
static volatile uint8_t cpu1_bell;

/** @brief Doorbells seen in total, for observability. */
volatile uint32_t tiku_ra8p1_cpu1_bell_count;

/**
 * @brief Reply doorbell interrupt.
 *
 * IPC0STA0 is read-only, so the acknowledge goes through IPC0CLR0; a write
 * to STA clears nothing, and the interrupt would re-enter forever.
 */
void tiku_ra8p1_ipc_handler(void)
{
    uint32_t sta = TIKU_REG32(RA8P1_IPC0STA0);

    TIKU_REG32(RA8P1_IPC0CLR0) = (sta & 0xFFUL) | RA8P1_IPC_CLR_RCLR |
                                 RA8P1_IPC_CLR_FCLR;
    TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_IPC)) &= ~RA8P1_ICU_IELSR_IR;
    (void)TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_IPC));
    TIKU_REG32(RA8P1_NVIC_ICPR(RA8P1_ICU_SLOT_IPC / 32U)) =
        (1UL << (RA8P1_ICU_SLOT_IPC % 32U));
    __asm__ volatile ("dsb" ::: "memory");

    cpu1_bell = 1U;
    tiku_ra8p1_cpu1_bell_count++;
}

/** @brief Link the doorbell event to the NVIC and unmask it. */
static void cpu1_bell_arm(void)
{
    TIKU_REG32(RA8P1_IPC0CLR0) = 0xFFUL | RA8P1_IPC_CLR_RCLR |
                                 RA8P1_IPC_CLR_FCLR;
    TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_IPC)) = RA8P1_EVENT_IPC_IRQ0;
    (void)TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_IPC));
    TIKU_REG32(RA8P1_NVIC_ICPR(RA8P1_ICU_SLOT_IPC / 32U)) =
        (1UL << (RA8P1_ICU_SLOT_IPC % 32U));
    TIKU_REG32(RA8P1_NVIC_ISER(RA8P1_ICU_SLOT_IPC / 32U)) =
        (1UL << (RA8P1_ICU_SLOT_IPC % 32U));
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}

int tiku_ra8p1_cpu1_bell_take(void)
{
    if (!cpu1_bell) {
        return 0;
    }
    cpu1_bell = 0U;
    return 1;
}

/** @brief M85 NMI: count it and mark the payload stopped. */
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

/** @brief Invalidate CPU1's half of the page; the next reads come from SRAM. */
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
    uint32_t m;

    cpu1_pull();
    m = CPU1_SH->magic;
    /* Every observation goes through here, so this counts a fault, once;
     * the flag clears when the magic reads TIKU_CPU1_MAGIC again. */
    if (m == TIKU_CPU1_MAGIC_FAULT || m == TIKU_CPU1_MAGIC_HANG) {
        /* A WDT1 hang counts as a fault: the payload is unusable until the
         * same restart. */
        if (!cpu1_fault_noticed) {
            cpu1_fault_noticed = 1U;
            tiku_ra8p1_cpu1_fault_count++;
            cpu1_running = 0U;
        }
    } else if (m == TIKU_CPU1_MAGIC) {
        cpu1_fault_noticed = 0U;
    }
    return m;
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

    /* Sequence last, pushed after the buffer: the payload serves a message
     * once its sequence changes. */
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
        return 0U;                      /* no reply to this send */
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

void tiku_ra8p1_cpu1_raw(uint32_t out[5])
{
    /* Invalidate both halves first, so the reads come from SRAM. */
    tiku_ra8p1_dcache_invalidate((void *)CPU1_SH, sizeof(tiku_cpu1_shared_t));
    out[0] = CPU1_SH->halt;
    out[1] = CPU1_SH->a2c_restart;
    out[2] = CPU1_SH->a2c_seq;
    out[3] = CPU1_SH->magic;
    out[4] = CPU1_SH->heartbeat;
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
    /* A non-zero counter shows the payload ran; a moving one shows it still
     * runs. */
    a = tiku_ra8p1_cpu1_heartbeat();
    for (i = 0U; i < 20000U; i++) {
        __asm__ volatile ("nop");
    }
    return (tiku_ra8p1_cpu1_heartbeat() != a) ? 1 : 0;
}

int tiku_ra8p1_cpu1_stop(void)
{
    uint32_t i;

    if (!tiku_ra8p1_cpu1_active()) {
        cpu1_running = 0U;
        return TIKU_RA8P1_CPU1_OK;
    }
    cpu1_set_halt(1UL);
    for (i = 0U; i < 2000000U; i++) {
        cpu1_pull();
        if (CPU1_SH->parked) {
            cpu1_running = 0U;
            return TIKU_RA8P1_CPU1_OK;
        }
    }
    return TIKU_RA8P1_CPU1_ERR_DEAD;
}

void tiku_ra8p1_cpu1_diag_init(void)
{
    if (cpu1_diag_magic != 0x43314447UL) {
        tiku_ra8p1_cpu1_nmi_count = 0U;
        tiku_ra8p1_cpu1_fault_count = 0U;
        cpu1_diag_magic = 0x43314447UL;
    }
}

int tiku_ra8p1_cpu1_start(void)
{
    volatile uint32_t *vec = (volatile uint32_t *)cpu1_area;
    uint32_t base = (uint32_t)(uintptr_t)cpu1_area;
    unsigned long spins;
    uint32_t i;

    tiku_ra8p1_cpu1_diag_init();

    /*
     * An active core cannot be launched again: ACTREQ acts only while ACT is
     * 0, and nothing returns CPU1 to power gating.  A second start resumes
     * the payload and leaves the image the core is executing untouched.
     */
    if (tiku_ra8p1_cpu1_active()) {
        /* A faulted payload waits in cpu1_park() for the restart word to
         * change; changing it re-enters the reset path. */
        uint32_t rm = tiku_ra8p1_cpu1_magic();
        if (rm == TIKU_CPU1_MAGIC_FAULT || rm == TIKU_CPU1_MAGIC_HANG) {
            uint32_t settle;

            cpu1_restart_gen++;
            CPU1_SH->a2c_restart = cpu1_restart_gen;
            cpu1_push();
            /* Wait, bounded, for the restarted payload to publish its
             * magic. */
            for (settle = 0U; settle < 100U; settle++) {
                for (i = 0U; i < 20000U; i++) {
                    __asm__ volatile ("nop");
                }
                if (tiku_ra8p1_cpu1_magic() == TIKU_CPU1_MAGIC) {
                    break;
                }
            }
        } else {
            cpu1_set_halt(0UL);
        }
        cpu1_running = 1U;
        /* A locked-up core ignores both words, so the resume is checked
         * against the heartbeat. */
        if (!tiku_ra8p1_cpu1_alive()) {
            cpu1_running = 0U;
            return TIKU_RA8P1_CPU1_ERR_DEAD;
        }
        return TIKU_RA8P1_CPU1_OK;
    }

    if (CPU1_IMG_SIZE == 0U || CPU1_IMG_SIZE > TIKU_CPU1_AREA_SIZE) {
        return TIKU_RA8P1_CPU1_ERR_IMG;
    }

    /* Zero the whole area first: no tail of an earlier, longer image
     * remains, and the payload's .bss and the shared page start at zero. */
    for (i = 0U; i < TIKU_CPU1_AREA_SIZE; i++) {
        cpu1_area[i] = 0U;
    }
    for (i = 0U; i < CPU1_IMG_SIZE; i++) {
        cpu1_area[i] = _binary_tiku_cpu1_bin_start[i];
    }
    vec[0] = base + TIKU_CPU1_STACK_OFF;
    vec[1] = (base + TIKU_CPU1_RESET_OFF) | 1U;
    /* NMI (WDT1) and HardFault.  A fault taken while HardFault is active
     * locks the core up whatever the other vectors hold, so both handlers
     * exception-return before anything else can fault. */
    vec[2] = (base + TIKU_CPU1_NMI_OFF) | 1U;
    vec[3] = (base + TIKU_CPU1_FAULT_OFF) | 1U;
    cpu1_a2c_seq = 0U;
    cpu1_restart_gen = 0U;
    cpu1_bell = 0U;
    cpu1_bell_arm();

    /* The image arrived through this core's write-back D-cache; CPU1 fetches
     * straight from SRAM and would see zeros. */
    tiku_ra8p1_dcache_clean(cpu1_area, TIKU_CPU1_AREA_SIZE);
    __asm__ volatile ("dsb" ::: "memory");

    /*
     * The vector base is set before activation.  INITVTOR is latched as the
     * core leaves reset, which activation triggers; set afterwards it
     * changes nothing and CPU1 boots from the register's reset value
     * 0x0200_0000, the M85's own vector table.  An M33 running the M85's
     * image would use and overwrite the M85's stack.
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
