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
 * Serves echo, a SHA-256 chain, a P-256 verify and two test messages that
 * fault or wedge the core; it is linked at the SRAM carve and runs only there.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "tiku_cpu1_ipc.h"
#include "tiku_cpu1_sha256.h"
#include "tiku_cpu1_cache.h"
#include <tikukits/crypto/p256/tiku_kits_crypto_p256.h>

/** @brief PC bits cleared to find the image base; INITVTOR ignores bits
 *         [6:0], so the base is 128-byte aligned. */
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
/* WDTCR: TOPS 16384 cycles of PCLKB / 8192, no window: about 2.1 s at PCLKB
 * 62.5 MHz, longer at a lower PCLKB. Hash batches and the parked loop
 * refresh WDT1; a stalled computation raises TIKU_CPU1_MAGIC_HANG. */
#define CPU1_WDT1_CR_VALUE  ((0x3U << 0) | (0x8U << 4) | (0x3U << 8) | (0x3U << 12))

/**
 * @brief Ring the M85's doorbell: a reply is waiting.
 *
 * @note Call after writing c2a_seq behind a barrier: tiku_coproc_poll() reads
 *       the sequence only when the doorbell rings.
 */
static void cpu1_ring(void)
{
    *(volatile uint32_t *)CPU1_IPC0ISET0 = CPU1_IPC_IRQ0;
}

/*
 * Sixteen zero words; the loader patches SP, reset, NMI and HardFault.  A
 * fault inside the fault handler locks the core up: it stops fetching and
 * the heartbeat freezes, with no reset and no NMI on the M85.
 */
__attribute__((section(".cpu1_vectors"), used))
const uint32_t cpu1_vectors[16] = { 0 };

#define CPU1_R32(a)  (*(volatile uint32_t *)(a))

/**
 * @brief Make the shared page non-cacheable, then turn the S-Cache on.
 *
 * The MPU goes first: the default map makes all of SRAM write-back, and a
 * cached shared page would spin forever on its own copy of the halt word.
 * HFNMIENA keeps the region in force in the HardFault and NMI handlers.
 *
 * @param base  Image base; the shared page is at base + TIKU_CPU1_SHARED_OFF
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

    /* Write-through with no write-allocate (the reset values), then flush
     * and enable. */
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

/** @brief Refresh WDT1: writing 0x00 then 0xFF reloads the down-counter. */
static inline void cpu1_wdt_kick(void)
{
    *(volatile uint8_t *)CPU1_WDT1_RR = 0x00U;
    *(volatile uint8_t *)CPU1_WDT1_RR = 0xFFU;
}

/**
 * @brief Arm WDT1 to supervise this payload (register-start mode).
 *
 * WDTRCR = 0 clears RSTIRQS, so an underflow raises an NMI on this core and
 * does not reset the system.  WDTCR is write-once; the first refresh starts
 * the count.
 */
static inline void cpu1_wdt_arm(void)
{
    *(volatile uint32_t *)CPU1_ICU1_NMIER |= CPU1_WDT_NMI_BIT;
    *(volatile uint8_t  *)CPU1_WDT1_RCR = 0x00U;
    *(volatile uint16_t *)CPU1_WDT1_CR  = (uint16_t)CPU1_WDT1_CR_VALUE;
    cpu1_wdt_kick();
}

/**
 * @brief Wait out a fault in Thread mode, then re-enter the reset path once
 *        the owner changes a2c_restart.
 *
 * Runs with the HardFault or NMI already deactivated, so a fault while
 * parked is taken as a new HardFault, not a lockup.
 *
 * @param base  Image base, arriving in the fabricated frame's R0
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
 * @brief HardFault: write TIKU_CPU1_MAGIC_FAULT, then exception-return into
 *        cpu1_park() to wait for the owner's restart.
 *
 * Finds the shared page through VTOR, which activation set to the image
 * base; the faulting PC can be anywhere.
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
     * An eight-word exception frame just below the initial SP from the vector
     * table, 8-aligned because the frame's realign bit (xPSR bit 9) is 0; an
     * unaligned frame would return to Thread mode with SP 4 bytes off.
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
     * Leave by exception return, which deactivates the HardFault; a branch
     * would keep execution priority at -1, where the next fault locks the
     * core up.  PRIMASK and BASEPRI do not unstack, so they are cleared here.
     * `mvn r0, #6` makes EXC_RETURN 0xFFFFFFF9 without a literal pool, which
     * this pinned section has no room for.  Nothing may touch the stack
     * between the MSR and the BX.
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
 * @brief WDT1 underflow (NMI): write TIKU_CPU1_MAGIC_HANG, then
 *        exception-return into cpu1_park() as cpu1_fault() does.
 *
 * An NMI left active would make the next fault a lockup.
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
    /* Clear the WDT NMI status, or the NMI fires again on return. */
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
 * @brief Answer one message: "FLT!" faults the core (UDF, then cpu1_fault()),
 *        "HANG" spins with interrupts masked until WDT1 fires, "HSH!" runs
 *        the hash chain, "ECV!" a P-256 verify, and anything else is echoed.
 *
 * The reply carries @p seq in c2a_seq, so the M85 matches it to its send.
 *
 * @param sh   The shared page
 * @param seq  The a2c_seq being answered
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
        /* Wedge: mask interrupts and spin without refreshing WDT1.  The
         * heartbeat freezes and no fault is raised; WDT1's NMI, which PRIMASK
         * does not mask, ends it. */
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
            /* Zero iterations: reply with the seed as this core read it. */
            for (i = 0U; i < 40U; i++) {
                sh->c2a_buf[i] = seed[i];
            }
            sh->c2a_len = 40U;
            __asm__ volatile ("dmb" ::: "memory");
            sh->c2a_seq = seq;
            cpu1_ring();
            return;
        }
        while (iters != 0U) {
            uint32_t batch = iters > 64U ? 64U : iters;
            if (sh->halt) {
                return;
            }
            tiku_cpu1_sha256_chain(seed, batch, digest);
            for (i = 0U; i < 32U; i++) {
                seed[i] = digest[i];
            }
            iters -= batch;
            cpu1_wdt_kick();
            sh->heartbeat++;
        }
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

    /* Sequence last, behind a barrier: the M85 reads the reply once the
     * sequence matches. */
    __asm__ volatile ("dmb" ::: "memory");
    sh->c2a_seq = seq;
    cpu1_ring();
}

/**
 * @brief Payload entry: turn the cache on, publish the magic word, arm WDT1
 *        and serve the mailbox, pausing while halt is set.
 *
 * Never returns.  It runs on the stack whose top is vector 0; the fault and
 * NMI handlers build their return frame there, over this function's frame.
 */
__attribute__((section(".cpu1_reset"), used, noreturn))
void cpu1_reset(void)
{
    volatile tiku_cpu1_shared_t *sh;
    uint32_t pc;
    uint32_t base;
    uint32_t served;

    /*
     * The image base is the PC with its low 7 bits cleared, exact while this
     * code sits in the first 128 bytes; tiku_cpu1.ld pins the entry at 0x40.
     */
    __asm__ volatile (".global cpu1_base_pc\n"
                      "cpu1_base_pc:\n"
                      "mov %0, pc" : "=r" (pc));
    base = pc & ~CPU1_BASE_MASK;
    sh = (volatile tiku_cpu1_shared_t *)(base + TIKU_CPU1_SHARED_OFF);

    cpu1_cache_on(base);

    /* A message already in the mailbox predates this start and is not
     * served: after a fault restart it is the message that caused the fault. */
    served = sh->a2c_seq;

    sh->parked = 0U;
    sh->heartbeat = 0U;
    sh->magic = TIKU_CPU1_MAGIC;

    /* Order the magic before the first heartbeat: the M85 reads a moving
     * heartbeat with no magic as a failed launch. */
    __asm__ volatile ("dmb" ::: "memory");

    cpu1_wdt_arm();

    for (;;) {
        uint32_t seq;

        /* The halt protocol: spin while halt is set, re-reading it every
         * pass. The acknowledgement is published after the job returns. */
        while (sh->halt != 0U) {
            sh->parked = 1U;
            __asm__ volatile ("dmb" ::: "memory");
            cpu1_wdt_kick();
        }
        sh->parked = 0U;
        __asm__ volatile ("dmb" ::: "memory");

        seq = sh->a2c_seq;
        if (seq != served) {
            cpu1_serve(sh, seq);
            served = seq;
        }

        cpu1_wdt_kick();
        sh->heartbeat++;
    }
}
