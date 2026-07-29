/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_cortexm.inl - generic Cortex-M worker-thread switcher.
 *
 * The one context-switch body shared by every Cortex-M part: threads run on PSP,
 * exceptions on MSP, and the kernel context migrates to PSP once at boot.  A
 * per-platform shim names its PendSV handler and includes this file.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#ifndef TIKU_THREAD_ARCH_PENDSV
#error "include a per-platform shim that #defines TIKU_THREAD_ARCH_PENDSV " \
       "to the PendSV handler symbol this build's vector table expects"
#endif

/*---------------------------------------------------------------------------*/
/* REGISTERS (architectural -- same on M4F / M33 / M55)                      */
/*---------------------------------------------------------------------------*/

#define SCB_ICSR    (*(volatile uint32_t *)0xE000ED04UL)
#define ICSR_PENDSVSET      (1UL << 28)

/** PendSV priority byte (SHPR3[23:16], byte-addressable). */
#define SHPR3_PENDSV_PRI (*(volatile uint8_t  *)0xE000ED22UL)

/** CPACR: full access to CP10/CP11 (the FPU). */
#define SCB_CPACR   (*(volatile uint32_t *)0xE000ED88UL)
#define CPACR_FPU_FULL      (0xFUL << 20)

#define SCB_FPCCR   (*(volatile uint32_t *)0xE000EF34UL)
#define FPCCR_ASPEN         (1UL << 31)
#define FPCCR_LSPEN         (1UL << 30)

/*---------------------------------------------------------------------------*/
/* CYCLE SOURCE (default: DWT CYCCNT)                                        */
/*---------------------------------------------------------------------------*/
/*
 * Per-thread accounting needs a free-running CPU-speed counter.  DWT
 * CYCCNT is the architectural default -- but on parts where the DWT
 * freezes without a debugger attached (nRF54L15), the shim #defines
 * TIKU_THREAD_ARCH_CUSTOM_CYCLES and provides, BEFORE including this
 * file, its own thread_cycles_init() (static) and the public
 * tiku_thread_arch_cycles() backed by a hardware timer.
 */
#ifndef TIKU_THREAD_ARCH_CUSTOM_CYCLES

#define SCB_DEMCR   (*(volatile uint32_t *)0xE000EDFCUL)
#define DEMCR_TRCENA        (1UL << 24)
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004UL)
#define DWT_CTRL_CYCCNTENA  (1UL << 0)

/** @brief Start the DWT cycle counter (default cycle source). */
static void thread_cycles_init(void)
{
    SCB_DEMCR |= DEMCR_TRCENA;
    DWT_CTRL  |= DWT_CTRL_CYCCNTENA;
}

/** @brief Free-running CPU cycle counter (per-thread accounting). */
uint32_t tiku_thread_arch_cycles(void)
{
    return DWT_CYCCNT;
}

#endif /* !TIKU_THREAD_ARCH_CUSTOM_CYCLES */

/*---------------------------------------------------------------------------*/
/* ISR STACK                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Dedicated exception stack (MSP) once threading starts.
 *
 * Before threading, MSP serves the kernel and every ISR from the boot stack.
 * Migration hands that region to the kernel thread as PSP and points MSP here.
 * 2 KB covers the lean ISR set at the NVIC preemption levels in use.
 */
static uint32_t s_isr_stack[512] __attribute__((aligned(8)));

/*---------------------------------------------------------------------------*/
/* BACKEND API (see externs in kernel/threads/tiku_thread.c)                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Migrate the calling (kernel) context from MSP to PSP in place.
 *
 * PSP := current MSP, CONTROL.SPSEL := 1 (FPCA preserved!), then MSP is
 * re-pointed at the dedicated ISR stack.  Returns normally -- the caller
 * continues on the same stack, now as a PSP thread.
 *
 * @param isr_stack_top  New MSP value (top of the ISR stack)
 */
__attribute__((naked))
static void thread_migrate_to_psp(uint32_t isr_stack_top
                                  __attribute__((unused)))
{
    __asm__ volatile (
        ".syntax unified            \n"
        "mrs    r1, msp             \n"
        "msr    psp, r1             \n"
        "mrs    r2, control         \n"
        "orrs   r2, r2, #2          \n"   /* SPSEL=1, keep FPCA/nPRIV */
        "msr    control, r2         \n"
        "isb                        \n"
        "msr    msp, r0             \n"
        "bx     lr                  \n");
}

/** @brief One-time bring-up (first tiku_thread_start, kernel context). */
void tiku_thread_arch_boot(void)
{
    /* Enable the FPU (CPACR full access to CP10/CP11) so FP-using
     * workers -- and the switcher's S16-S31 save/restore -- are valid.
     * Idempotent: the Ambiq CRTs already do this at reset; RP2350's does
     * not, so the threads backend owns it to stay self-contained. */
    SCB_CPACR |= CPACR_FPU_FULL;
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb");

    /* FP lazy stacking (reset default; assert it anyway). */
    SCB_FPCCR |= FPCCR_ASPEN | FPCCR_LSPEN;

    /* PendSV at the lowest priority: switches only in thread mode. */
    SHPR3_PENDSV_PRI = 0xFFu;

    /* Cycle counter for per-thread accounting (DWT, or the shim's own). */
    thread_cycles_init();

    thread_migrate_to_psp((uint32_t)&s_isr_stack[512]);
}

/** @brief Pend the context switch (idempotent, any context). */
void tiku_thread_arch_pend(void)
{
    SCB_ICSR = ICSR_PENDSVSET;
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb");
}

/**
 * @brief Build a worker's initial frames on its stack.
 *
 * Descending: the 8-word hardware frame (R0=arg, LR=exit trampoline, PC=entry,
 * xPSR=Thumb), then the 9-word software frame PendSV pops, EXC_RETURN set for
 * thread mode on PSP -- the FPU frame appears only on the first FP instruction.
 *
 * @return Initial sp (what the switcher's LDMIA expects)
 */
uint32_t *tiku_thread_arch_frame_init(uint32_t *stack_top,
                                      void (*entry)(void *),
                                      void *arg,
                                      void (*exit_fn)(void))
{
    uint32_t *sp = (uint32_t *)((uintptr_t)stack_top & ~(uintptr_t)7);
    uint8_t i;

    sp -= 8;                                   /* hardware frame        */
    sp[0] = (uint32_t)arg;                     /* R0                    */
    sp[1] = 0; sp[2] = 0; sp[3] = 0;           /* R1-R3                 */
    sp[4] = 0;                                 /* R12                   */
    sp[5] = ((uint32_t)exit_fn) | 1u;          /* LR: entry returns here*/
    sp[6] = ((uint32_t)entry) & ~1u;           /* PC (T from xPSR)      */
    sp[7] = 0x01000000u;                       /* xPSR: Thumb           */

    sp -= 9;                                   /* software frame        */
    for (i = 0; i < 8; i++) {
        sp[i] = 0;                             /* R4-R11                */
    }
    sp[8] = 0xFFFFFFFDu;                        /* EXC_RETURN            */

    return sp;
}

/*---------------------------------------------------------------------------*/
/* PENDSV -- THE SWITCH                                                       */
/*---------------------------------------------------------------------------*/

/** Policy hop: kernel/threads/tiku_thread.c picks the next context. */
extern uint32_t *tiku_thread_switch(uint32_t *old_sp);

/**
 * @brief PendSV handler (strong override of the vector's weak alias).
 *
 * Saves the outgoing software frame on its PSP stack, S16-S31 first when the FP
 * frame is live, asks the policy layer for the next sp, then unwinds the incoming
 * thread the same way.  EXC_RETURN travels in the frame, so the two kinds mix.
 */
__attribute__((naked))
void TIKU_THREAD_ARCH_PENDSV(void)
{
    __asm__ volatile (
        ".syntax unified                 \n"
        "mrs    r0, psp                  \n"
        "tst    lr, #0x10                \n"   /* FP frame live?        */
        "it     eq                       \n"
        "vstmdbeq r0!, {s16-s31}         \n"
        "stmdb  r0!, {r4-r11, lr}        \n"
        "bl     tiku_thread_switch       \n"
        "ldmia  r0!, {r4-r11, lr}        \n"
        "tst    lr, #0x10                \n"
        "it     eq                       \n"
        "vldmiaeq r0!, {s16-s31}         \n"
        "msr    psp, r0                  \n"
        "bx     lr                       \n");
}
