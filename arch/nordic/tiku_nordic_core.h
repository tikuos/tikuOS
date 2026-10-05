/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nordic_core.h - Cortex-M33 core registers and intrinsics.
 *
 * The core subset this port uses: system reset, vector relocation, SysTick,
 * NVIC enable and priority, barriers and PRIMASK.  All addresses are ARMv8-M
 * architectural, so nothing here is device-specific.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_CORE_H_
#define TIKU_NORDIC_CORE_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CORE REGISTER BLOCKS (ARMv8-M ARCHITECTURAL ADDRESSES)                    */
/*---------------------------------------------------------------------------*/

/** System Control Block (partial: what the port touches). */
typedef struct {
    volatile uint32_t CPUID;   /**< 0xE000ED00 CPU ID base register.        */
    volatile uint32_t ICSR;    /**< 0xE000ED04 Interrupt control/state.     */
    volatile uint32_t VTOR;    /**< 0xE000ED08 Vector table offset.         */
    volatile uint32_t AIRCR;   /**< 0xE000ED0C App interrupt/reset control. */
    volatile uint32_t SCR;     /**< 0xE000ED10 System control register.     */
    volatile uint32_t CCR;     /**< 0xE000ED14 Configuration/control.       */
    volatile uint8_t  SHPR[12];/**< 0xE000ED18 System handler priority.     */
    volatile uint32_t SHCSR;   /**< 0xE000ED24 System handler ctrl/state.   */
} tiku_nordic_scb_t;

/** SysTick timer. */
typedef struct {
    volatile uint32_t CTRL;    /**< 0xE000E010 Control and status.          */
    volatile uint32_t LOAD;    /**< 0xE000E014 Reload value.                */
    volatile uint32_t VAL;     /**< 0xE000E018 Current value.               */
    volatile uint32_t CALIB;   /**< 0xE000E01C Calibration.                 */
} tiku_nordic_systick_t;

/** Nested Vectored Interrupt Controller (enable/priority subset). */
typedef struct {
    volatile uint32_t ISER[16];/**< 0xE000E100 Set-enable.                  */
    uint32_t          _r0[16];
    volatile uint32_t ICER[16];/**< 0xE000E180 Clear-enable.                */
    uint32_t          _r1[16];
    volatile uint32_t ISPR[16];/**< 0xE000E200 Set-pending.                 */
    uint32_t          _r2[16];
    volatile uint32_t ICPR[16];/**< 0xE000E280 Clear-pending.               */
    uint32_t          _r3[16];
    volatile uint32_t IABR[16];/**< 0xE000E300 Active bit.                  */
    uint32_t          _r4[48];
    volatile uint8_t  IPR[496];/**< 0xE000E400 Interrupt priority (byte).   */
} tiku_nordic_nvic_t;

#define TIKU_SCB      ((tiku_nordic_scb_t *)0xE000ED00UL)     /**< SCB      */
#define TIKU_SYSTICK  ((tiku_nordic_systick_t *)0xE000E010UL) /**< SysTick  */
#define TIKU_NVIC     ((tiku_nordic_nvic_t *)0xE000E100UL)    /**< NVIC     */

/** nRF54L implements 3 NVIC priority bits (top bits of the 8-bit field). */
#define TIKU_NORDIC_NVIC_PRIO_BITS   3u

/* SysTick CTRL bits. */
#define TIKU_SYSTICK_CTRL_ENABLE     (1UL << 0)  /**< counter enable */
#define TIKU_SYSTICK_CTRL_TICKINT    (1UL << 1)  /**< exception at zero */
#define TIKU_SYSTICK_CTRL_CLKSOURCE  (1UL << 2)  /**< 1 = processor clock */
#define TIKU_SYSTICK_CTRL_COUNTFLAG  (1UL << 16) /**< counted to 0 since read */

/*---------------------------------------------------------------------------*/
/* BARRIER AND HINT INTRINSICS                                               */
/*---------------------------------------------------------------------------*/

/** @brief Data synchronization barrier (DSB SY). */
static inline void tiku_nordic_dsb(void) { __asm volatile ("dsb 0xF" ::: "memory"); }
/** @brief Instruction synchronization barrier (ISB SY). */
static inline void tiku_nordic_isb(void) { __asm volatile ("isb 0xF" ::: "memory"); }
/** @brief One NOP. */
static inline void tiku_nordic_nop(void) { __asm volatile ("nop"); }
/** @brief Wait for interrupt. */
static inline void tiku_nordic_wfi(void) { __asm volatile ("wfi" ::: "memory"); }
/** @brief Wait for event. */
static inline void tiku_nordic_wfe(void) { __asm volatile ("wfe" ::: "memory"); }

/*---------------------------------------------------------------------------*/
/* INTERRUPT MASK (PRIMASK)                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Clear PRIMASK: interrupts are taken. */
static inline void tiku_nordic_enable_irq(void)
{
    __asm volatile ("cpsie i" ::: "memory");
}

/** @brief Set PRIMASK: interrupts are masked. */
static inline void tiku_nordic_disable_irq(void)
{
    __asm volatile ("cpsid i" ::: "memory");
}

/** @brief Return PRIMASK: 1 while interrupts are masked, else 0. */
static inline uint32_t tiku_nordic_get_primask(void)
{
    uint32_t r;
    __asm volatile ("mrs %0, primask" : "=r" (r));
    return r;
}

/** @brief Write PRIMASK, e.g. a value saved by tiku_nordic_get_primask(). */
static inline void tiku_nordic_set_primask(uint32_t v)
{
    __asm volatile ("msr primask, %0" :: "r" (v) : "memory");
}

/*---------------------------------------------------------------------------*/
/* NVIC HELPERS                                                              */
/*---------------------------------------------------------------------------*/

/** Enable an external interrupt (IRQn >= 0). */
static inline void tiku_nordic_nvic_enable(int32_t irqn)
{
    TIKU_NVIC->ISER[((uint32_t)irqn) >> 5] = (1UL << (((uint32_t)irqn) & 0x1Fu));
}

/** Disable an external interrupt; DSB and ISB complete it before return. */
static inline void tiku_nordic_nvic_disable(int32_t irqn)
{
    TIKU_NVIC->ICER[((uint32_t)irqn) >> 5] = (1UL << (((uint32_t)irqn) & 0x1Fu));
    tiku_nordic_dsb();
    tiku_nordic_isb();
}

/** Clear a pending external interrupt. */
static inline void tiku_nordic_nvic_clear_pending(int32_t irqn)
{
    TIKU_NVIC->ICPR[((uint32_t)irqn) >> 5] = (1UL << (((uint32_t)irqn) & 0x1Fu));
}

/** Set an external interrupt's priority, 0 (highest) to 7 (lowest). */
static inline void tiku_nordic_nvic_set_priority(int32_t irqn, uint32_t prio)
{
    TIKU_NVIC->IPR[(uint32_t)irqn] =
        (uint8_t)((prio << (8u - TIKU_NORDIC_NVIC_PRIO_BITS)) & 0xFFu);
}

/*---------------------------------------------------------------------------*/
/* SYSTEM RESET                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Request a system reset via SCB->AIRCR (SYSRESETREQ).
 *
 * Writes the VECTKEY (0x05FA) with SYSRESETREQ set, then spins.  On nRF54L
 * this asserts the RESET.SREQ reset source (decoded by the reset-reason
 * arch layer as a software reboot).
 */
static inline void tiku_nordic_system_reset(void)
{
    tiku_nordic_dsb();
    TIKU_SCB->AIRCR = (0x05FAUL << 16) | (1UL << 2); /* VECTKEY | SYSRESETREQ */
    tiku_nordic_dsb();
    for (;;) {
        /* wait for reset */
    }
}

/*---------------------------------------------------------------------------*/
/* VECTOR TABLE RELOCATION                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Point VTOR at a relocated vector table.
 *
 * @note The table must be aligned to its size rounded up to a power of two,
 *       and to at least 128 bytes.
 */
static inline void tiku_nordic_set_vtor(uint32_t table_addr)
{
    TIKU_SCB->VTOR = table_addr;
    tiku_nordic_dsb();
    tiku_nordic_isb();
}

#endif /* TIKU_NORDIC_CORE_H_ */
