/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1_payload.c - the RA8P1 Cortex-M33's mailbox server.
 *
 * Built standalone for cortex-m33 and embedded in the M85 image as bytes.
 * Serves echo, a SHA-256 chain and a P-256 verify; links at the fixed SRAM
 * carve and must be loaded there.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "tiku_cpu1_ipc.h"
#include "tiku_cpu1_sha256.h"
#include "tiku_cpu1_cache.h"
#include <tikukits/crypto/p256/tiku_kits_crypto_p256.h>

/** @brief INITVTOR discards bits [6:0], so the image base is 128-aligned. */
#define CPU1_BASE_MASK      0x7FUL

/** @brief Doorbell to the M85: bit 0 of the CPU1->CPU0 interrupt set. */
#define CPU1_IPC0ISET0      0x400200C4UL
#define CPU1_IPC_IRQ0       (1UL << 0)

/* WDT1 (0x40202700) and ICU1's NMI block (ICU base 0x4000C000; each CPU sees
 * its own).  UM 28 and 14; the transcription with reset values is in
 * arch/ra8p1/tiku_ra8p1_regs.h, which this minimal payload does not include. */
#define CPU1_WDT1_RR        0x40202700UL   /* 8-bit  refresh                 */
#define CPU1_WDT1_CR        0x40202702UL   /* 16-bit control                 */
#define CPU1_WDT1_RCR       0x40202706UL   /* 8-bit  RSTIRQS b7: 0=NMI,1=rst */
#define CPU1_ICU1_NMIER     0x4000C100UL   /* bit1 WDTEN                      */
#define CPU1_ICU1_NMICLR    0x4000C110UL   /* bit1 WDTCLR                     */
#define CPU1_ICU1_NMISR     0x4000C120UL   /* bit1 WDTST                      */
#define CPU1_WDT_NMI_BIT    (1UL << 1)
/* WDTCR: TOPS 16384 x CKS /8192, no window -> ~2.1 s at PCLKB <= 62.5 MHz,
 * which outlasts the longest single cpu1_serve so healthy work never trips it. */
#define CPU1_WDT1_CR_VALUE  ((0x3U << 0) | (0x8U << 4) | (0x3U << 8) | (0x3U << 12))

/**
 * @brief Tell the other core a reply is waiting.
 *
 * @note After the sequence word and its barrier, never before: the M85's
 *       handler treats the doorbell as proof the reply is complete.
 */
static void cpu1_ring(void)
{
    *(volatile uint32_t *)CPU1_IPC0ISET0 = CPU1_IPC_IRQ0;
}

/*
 * Sixteen zero words.  The loader patches SP, reset and HardFault; every
 * other vector stays zero, so a fault inside the fault handler ends in
 * LOCKUP -- which, measured, neither resets the board nor raises the M85's
 * NMI: the core simply stops fetching and the heartbeat freezes.
 */
__attribute__((section(".cpu1_vectors"), used))
const uint32_t cpu1_vectors[16] = { 0 };

#define CPU1_R32(a)  (*(volatile uint32_t *)(a))

/**
 * @brief Exempt the shared page, then turn the S-Cache on.
 *
 * @param base  Image base, and so the page's address
 * @note The MPU is programmed before the cache, because MAIR decides
 *       cacheability.  The default map makes all of SRAM write-back, and a
 *       cached shared page spins forever on its own copy of the halt word.
 * @note HFNMIENA is set, unlike the M85's map.  The fault handler polls this
 *       page, and an MPU bypassed during HardFault would make it cacheable
 *       again while the owner is trying to restart the core.
 */
static void cpu1_cache_on(uint32_t base)
{
    uint32_t lo = base + TIKU_CPU1_SHARED_OFF;
    uint32_t hi = lo + (uint32_t)sizeof(tiku_cpu1_shared_t);

    CPU1_R32(CPU1_MPU_CTRL) = 0UL;
    __asm__ volatile ("dsb\n\tisb" ::: "memory");

    CPU1_R32(CPU1_MPU_MAIR0) = CPU1_MAIR_NORMAL_NC;
    CPU1_R32(CPU1_MPU_RNR)   = 0UL;
    CPU1_R32(CPU1_MPU_RBAR)  = (lo & ~0x1FUL) | CPU1_MPU_RBAR_AP_RW |
                               CPU1_MPU_RBAR_XN;
    CPU1_R32(CPU1_MPU_RLAR)  = ((hi - 1UL) & ~0x1FUL) | CPU1_MPU_RLAR_EN;
    CPU1_R32(CPU1_MPU_CTRL)  = CPU1_MPU_CTRL_ENABLE | CPU1_MPU_CTRL_PRIVDEF |
                               CPU1_MPU_CTRL_HFNMIENA;
    __asm__ volatile ("dsb\n\tisb" ::: "memory");

    /* Write-through, no write-allocate: the reset values, written out so
     * the cache policy is stated where it is chosen. */
    CPU1_R32(CPU1_SCAWTA) = CPU1_SCAWTA_WT;
    CPU1_R32(CPU1_SCAFCT) = CPU1_SCAFCT_FS;
    while ((CPU1_R32(CPU1_SCAFCT) & CPU1_SCAFCT_FS) != 0UL) {
    }
    CPU1_R32(CPU1_SCACTL) = CPU1_SCACTL_ENS;
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}

/** @brief EXC_RETURN: Thread mode, main stack, no floating-point frame. */
#define CPU1_EXC_RETURN_THREAD  0xFFFFFFF9UL

/** @brief Stacked xPSR: T set, no IT block, exception number 0. */
#define CPU1_RETPSR_THUMB       0x01000000UL

void cpu1_park(uint32_t base) __attribute__((used, noreturn));

/** @brief Refresh WDT1: the 0x00-then-0xFF sequence reloads the down-counter. */
static inline void cpu1_wdt_kick(void)
{
    *(volatile uint8_t *)CPU1_WDT1_RR = 0x00U;
    *(volatile uint8_t *)CPU1_WDT1_RR = 0xFFU;
}

/**
 * @brief Arm WDT1 to supervise this payload (register-start mode).
 *
 * @note WDTRCR clears RSTIRQS so an underflow is an NMI to THIS core, not a
 *       system reset.  WDTCR is write-once; the first refresh starts counting.
 */
static inline void cpu1_wdt_arm(void)
{
    *(volatile uint32_t *)CPU1_ICU1_NMIER |= CPU1_WDT_NMI_BIT;
    *(volatile uint8_t  *)CPU1_WDT1_RCR = 0x00U;
    *(volatile uint16_t *)CPU1_WDT1_CR  = (uint16_t)CPU1_WDT1_CR_VALUE;
    cpu1_wdt_kick();
}

/**
 * @brief Wait out the fault in Thread mode, then re-enter the reset path.
 *
 * @param base  Image base, arriving in the fabricated frame's R0
 * @note Runs at base priority with the HardFault already deactivated, so a
 *       fault while parked is handled rather than locking the core up.
 */
void cpu1_park(uint32_t base)
{
    volatile tiku_cpu1_shared_t *sh =
        (volatile tiku_cpu1_shared_t *)(base + TIKU_CPU1_SHARED_OFF);
    uint32_t gen = sh->a2c_restart;
    uint32_t entry = *(volatile uint32_t *)(base + 4U);
    uint32_t top = *(volatile uint32_t *)base;

    while (sh->a2c_restart == gen) {
    }
    __asm__ volatile ("msr msp, %0\n\tisb\n\tbx %1"
                      : : "r" (top), "r" (entry) : "memory");
    __builtin_unreachable();
}

/**
 * @brief Record the fault, then wait for the owner to order a restart.
 *
 * @note Reaches the shared page through VTOR, which the activation latched
 *       to the image base -- the stacked PC of a faulting instruction is no
 *       guide to anything.  The restart re-enters the reset path in Handler
 *       mode with the exception still active, so a SECOND fault cannot be
 *       handled and ends in LOCKUP; the owner sees that as a dead core.
 */
__attribute__((section(".cpu1_fault"), used, noreturn))
void cpu1_fault(void)
{
    uint32_t base = *(volatile uint32_t *)0xE000ED08UL;   /* VTOR */
    volatile tiku_cpu1_shared_t *sh =
        (volatile tiku_cpu1_shared_t *)(base + TIKU_CPU1_SHARED_OFF);
    uint32_t *frame;

    sh->magic = TIKU_CPU1_MAGIC_FAULT;
    __asm__ volatile ("dmb" ::: "memory");

    /*
     * Eight words at the vector table's stack top.  The base is 8-aligned
     * because the frame's realign bit is 0; an unstack onto an odd word
     * leaves Thread mode with the stack 4 bytes out.
     */
    frame = (uint32_t *)((*(volatile uint32_t *)base) & ~7UL) - 8;
    frame[0] = base;                              /* R0 -> cpu1_park arg  */
    frame[1] = 0UL;
    frame[2] = 0UL;
    frame[3] = 0UL;
    frame[4] = 0UL;                               /* R12                  */
    frame[5] = 0UL;                               /* LR                   */
    frame[6] = (uint32_t)&cpu1_park & ~1UL;       /* PC; T lives in xPSR  */
    frame[7] = CPU1_RETPSR_THUMB;

    /*
     * An exception return deactivates the HardFault; branching to the reset
     * entry instead leaves execution priority at -1, where the next fault
     * is a LOCKUP.  PRIMASK and BASEPRI are not in the frame and do not
     * unstack, so they are cleared here.  `mvn r0, #6` supplies the
     * EXC_RETURN without a literal pool, which this pinned section has no
     * room for.  The stack must not be touched between the MSR and the BX.
     */
    __asm__ volatile ("mov  r1, #0\n\t"
                      "msr  primask, r1\n\t"
                      "msr  basepri, r1\n\t"
                      "msr  msp, %0\n\t"
                      "mvn  r0, #6\n\t"
                      "dsb\n\tisb\n\t"
                      "bx   r0"
                      : : "r" (frame) : "r0", "r1", "memory");
    __builtin_unreachable();
}

/**
 * @brief WDT1 underflow (NMI): the payload wedged.  Record and hand back.
 *
 * @note Same exception-return discipline as cpu1_fault -- an NMI left active
 *       makes the next fault a LOCKUP, so this returns to cpu1_park in Thread
 *       mode rather than branching.  The owner sees MAGIC_HANG and restarts.
 */
__attribute__((section(".cpu1_nmi"), used, noreturn))
void cpu1_nmi(void)
{
    uint32_t base = *(volatile uint32_t *)0xE000ED08UL;   /* VTOR */
    volatile tiku_cpu1_shared_t *sh =
        (volatile tiku_cpu1_shared_t *)(base + TIKU_CPU1_SHARED_OFF);
    uint32_t *frame;

    sh->magic = TIKU_CPU1_MAGIC_HANG;
    __asm__ volatile ("dmb" ::: "memory");
    /* Clear the WDT NMI status, or it re-fires the instant the handler returns. */
    *(volatile uint32_t *)CPU1_ICU1_NMICLR = CPU1_WDT_NMI_BIT;

    frame = (uint32_t *)((*(volatile uint32_t *)base) & ~7UL) - 8;
    frame[0] = base;                              /* R0 -> cpu1_park arg  */
    frame[1] = 0UL;
    frame[2] = 0UL;
    frame[3] = 0UL;
    frame[4] = 0UL;                               /* R12                  */
    frame[5] = 0UL;                               /* LR                   */
    frame[6] = (uint32_t)&cpu1_park & ~1UL;       /* PC; T lives in xPSR  */
    frame[7] = CPU1_RETPSR_THUMB;

    __asm__ volatile ("mov  r1, #0\n\t"
                      "msr  primask, r1\n\t"
                      "msr  basepri, r1\n\t"
                      "msr  msp, %0\n\t"
                      "mvn  r0, #6\n\t"
                      "dsb\n\tisb\n\t"
                      "bx   r0"
                      : : "r" (frame) : "r0", "r1", "memory");
    __builtin_unreachable();
}

/**
 * @brief Answer one message: fault, hash chain, P-256 verify, or echo.
 *
 * @param sh  The shared page
 * @note Replies with the sequence it is answering, so the M85 can match a
 *       reply to its own send rather than to whatever arrived last.
 * @note TIKU_CPU1_FAULT_MSG faults this core on purpose: the undefined
 *       instruction escalates to HardFault and lands in cpu1_fault() above.
 *       The bench suite's fault leg is only evidence if the payload can
 *       really die.
 */
static void cpu1_serve(volatile tiku_cpu1_shared_t *sh, uint32_t seq)
{
    uint32_t len = sh->a2c_len;
    uint32_t i;

    if (len > TIKU_CPU1_MSG_CAP) {
        len = TIKU_CPU1_MSG_CAP;
    }
    if (len == 4U &&
        sh->a2c_buf[0] == (uint8_t)'F' && sh->a2c_buf[1] == (uint8_t)'L' &&
        sh->a2c_buf[2] == (uint8_t)'T' && sh->a2c_buf[3] == (uint8_t)'!') {
        __asm__ volatile ("udf #0");
    }
    if (len == 4U &&
        sh->a2c_buf[0] == (uint8_t)'H' && sh->a2c_buf[1] == (uint8_t)'A' &&
        sh->a2c_buf[2] == (uint8_t)'N' && sh->a2c_buf[3] == (uint8_t)'G') {
        /* Wedge: mask everything a payload can, then spin.  The heartbeat
         * freezes and no fault is raised -- the case only WDT1 -> NMI catches
         * (PRIMASK does not mask NMI).  Stops refreshing, so WDT1 underflows. */
        __asm__ volatile ("cpsid i" ::: "memory");
        for (;;) {
            __asm__ volatile ("nop");
        }
    }
    if (len == TIKU_CPU1_MSG_CAP &&
        sh->a2c_buf[0] == (uint8_t)TIKU_CPU1_WORK_MAGIC0 &&
        sh->a2c_buf[1] == (uint8_t)TIKU_CPU1_WORK_MAGIC1 &&
        sh->a2c_buf[2] == (uint8_t)TIKU_CPU1_WORK_MAGIC2 &&
        sh->a2c_buf[3] == (uint8_t)TIKU_CPU1_WORK_MAGIC3) {
        uint8_t seed[40];
        uint8_t digest[32];
        uint32_t iters = (uint32_t)sh->a2c_buf[4] |
                         ((uint32_t)sh->a2c_buf[5] << 8) |
                         ((uint32_t)sh->a2c_buf[6] << 16) |
                         ((uint32_t)sh->a2c_buf[7] << 24);

        if (iters > TIKU_CPU1_WORK_MAX_ITERS) {
            iters = TIKU_CPU1_WORK_MAX_ITERS;
        }
        for (i = 0U; i < 40U; i++) {
            seed[i] = sh->a2c_buf[8U + i];
        }
        if (iters == 0U) {
            /* Diagnostic: reply the seed as read, so the owner can see this
             * core's view of the mailbox rather than infer it. */
            for (i = 0U; i < 40U; i++) {
                sh->c2a_buf[i] = seed[i];
            }
            sh->c2a_len = 40U;
            __asm__ volatile ("dmb" ::: "memory");
            sh->c2a_seq = seq;
            cpu1_ring();
            return;
        }
        tiku_cpu1_sha256_chain(seed, iters, digest);
        for (i = 0U; i < 32U; i++) {
            sh->c2a_buf[i] = digest[i];
        }
        sh->c2a_len = 32U;
        __asm__ volatile ("dmb" ::: "memory");
        sh->c2a_seq = seq;
        cpu1_ring();
        return;
    }
    if (len == TIKU_CPU1_VERIFY_LEN &&
        sh->a2c_buf[0] == (uint8_t)TIKU_CPU1_VERIFY_MAGIC0 &&
        sh->a2c_buf[1] == (uint8_t)TIKU_CPU1_VERIFY_MAGIC1 &&
        sh->a2c_buf[2] == (uint8_t)TIKU_CPU1_VERIFY_MAGIC2 &&
        sh->a2c_buf[3] == (uint8_t)TIKU_CPU1_VERIFY_MAGIC3) {
        uint8_t op[5U][32U];
        uint32_t f, b;

        for (f = 0U; f < 5U; f++) {
            for (b = 0U; b < 32U; b++) {
                op[f][b] = sh->a2c_buf[4U + (f * 32U) + b];
            }
        }
        sh->c2a_buf[0] = (uint8_t)
            (tiku_kits_crypto_p256_ecdsa_verify(op[0], op[1], op[2], 32U,
                                                op[3], op[4]) == 0 ? 1U : 0U);
        sh->c2a_len = 1U;
        __asm__ volatile ("dmb" ::: "memory");
        sh->c2a_seq = seq;
        cpu1_ring();
        return;
    }
    for (i = 0U; i < len; i++) {
        sh->c2a_buf[i] = sh->a2c_buf[i];
    }
    sh->c2a_len = len;

    /* Sequence last, and behind a barrier: the M85 takes a matching seq as
     * proof the buffer beside it is already complete. */
    __asm__ volatile ("dmb" ::: "memory");
    sh->c2a_seq = seq;
    cpu1_ring();
}

/**
 * @brief Publish the magic word, then serve the mailbox until asked to halt.
 *
 * @note Never returns; the vector table's stack pointer covers the fault
 *       path and this function's own frame.
 */
__attribute__((section(".cpu1_reset"), used, noreturn))
void cpu1_reset(void)
{
    volatile tiku_cpu1_shared_t *sh;
    uint32_t pc;
    uint32_t base;
    uint32_t beats = 0U;
    uint32_t served;

    /*
     * Base masked out of the PC, so the reset path needs no relocation of
     * its own.  Exact while the entry stays inside the first 128 bytes,
     * which the .ld asserts.
     */
    __asm__ volatile ("mov %0, pc" : "=r" (pc));
    base = pc & ~CPU1_BASE_MASK;
    sh = (volatile tiku_cpu1_shared_t *)(base + TIKU_CPU1_SHARED_OFF);

    cpu1_cache_on(base);

    /* Whatever is in the mailbox predates this boot; a fault restart would
     * otherwise re-serve the very message that killed the last life. */
    served = sh->a2c_seq;

    sh->magic = TIKU_CPU1_MAGIC;

    /* The magic must land before the first heartbeat.  Normal memory is
     * weakly ordered to the other core, and a moving counter with the magic
     * still zero reads exactly like a launch that failed. */
    __asm__ volatile ("dmb" ::: "memory");

    cpu1_wdt_arm();

    for (;;) {
        uint32_t seq;

        /* Re-read every pass; this is the halt protocol.  The counters
         * survive it, so a resumed payload continues where it stopped. */
        while (sh->halt != 0U) {
        }

        seq = sh->a2c_seq;
        if (seq != served) {
            cpu1_serve(sh, seq);
            served = seq;
        }

        cpu1_wdt_kick();
        beats++;
        sh->heartbeat = beats;
    }
}
