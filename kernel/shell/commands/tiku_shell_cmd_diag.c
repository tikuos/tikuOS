/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_diag.c - "diag" command (STM32N6).
 *
 * Exercises the parts of the port that only prove themselves by going wrong:
 * the fault handlers, the EXTI lines and the watchdog.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_diag.h"
#include <kernel/shell/tiku_shell.h>
#include <string.h>

#if defined(PLATFORM_STM32N6)

#include <arch/stm32n6/tiku_fault_arch.h>
#include <arch/stm32n6/tiku_stm32n6_regs.h>
#include <arch/stm32n6/tiku_cpu_watchdog_arch.h>
#include <interfaces/gpio/tiku_gpio.h>
#include <hal/tiku_gpio_irq_hal.h>
#include <arch/stm32n6/tiku_gpio_irq_arch.h>
#include <kernel/process/tiku_process.h>

/** @brief The board's USER button: PC13, active high behind a pull-down. */
#define DIAG_BTN_PORT   2U
#define DIAG_BTN_PIN    13U

/** @brief Report the stored fault record, or say there is none. */
static void diag_fault_show(void) {
    const tiku_stm32n6_fault_record_t *f = tiku_stm32n6_fault_last();

    if (f->magic != TIKU_STM32N6_FAULT_MAGIC) {
        SHELL_PRINTF("  no fault recorded since the last cold start\n");
        return;
    }
    SHELL_PRINTF("  last %s (#%lu)\n", tiku_stm32n6_fault_kind_name(f->kind),
                 (unsigned long)f->count);
    SHELL_PRINTF("    cfsr %08lx  hfsr %08lx  addr %08lx\n",
                 (unsigned long)f->cfsr, (unsigned long)f->hfsr,
                 (unsigned long)f->addr);
    SHELL_PRINTF("    pc   %08lx  lr   %08lx  psr  %08lx  sp %08lx\n",
                 (unsigned long)f->pc, (unsigned long)f->lr,
                 (unsigned long)f->psr, (unsigned long)f->sp);
}

/**
 * @brief Provoke one fault so the handler and its record can be seen working.
 *
 * @param which  "bus", "usage" or "stack"
 */
static void diag_fault_force(const char *which) {
    SHELL_PRINTF("  forcing a %s fault; the board resets and `diag fault`"
                 " then shows it\n", which);

    if (strcmp(which, "undef") == 0) {
        /* The one trigger with no ambiguity: an undefined instruction is a
         * precise UsageFault, taken at a known PC, with no bus involved. */
        __asm__ volatile ("udf #0");
    } else if (strcmp(which, "unalign") == 0) {
        TIKU_REG32(STM32N6_SCB_CCR) |= (1UL << 3);      /* UNALIGN_TRP */
        __asm__ volatile ("dsb\n\tisb" ::: "memory");
        volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)0x34181001UL;
        (void)*p;
    } else if (strcmp(which, "stack") == 0) {
        /* A branch to an even address clears EPSR.T: the same INVSTATE that
         * made every early image on this port lock up identically. */
        void (*bad)(void) = (void (*)(void))0x34180400UL;
        bad();
    } else {
        SHELL_PRINTF("  kinds: undef | unalign | stack\n");
        return;
    }
    SHELL_PRINTF("  (no fault taken -- unexpected)\n");
}

/** @brief Arm the USER button's EXTI line and report what it delivers. */
static void diag_exti(uint8_t argc, const char *argv[]) {
    int rc = tiku_gpio_irq_enable(DIAG_BTN_PORT, DIAG_BTN_PIN,
                                  TIKU_GPIO_EDGE_RISING);
    if (rc != TIKU_GPIO_IRQ_OK) {
        SHELL_PRINTF("  exti: arm failed (%d)\n", rc);
        return;
    }
    SHELL_PRINTF("  exti: line %u armed on port %u (USER button)\n",
                 (unsigned)DIAG_BTN_PIN, (unsigned)DIAG_BTN_PORT);

    if (argc >= 3 && strcmp(argv[2], "wait") == 0) {
        SHELL_PRINTF("  press the USER button...\n");
        return;                     /* the event lands as TIKU_EVENT_GPIO */
    }

    /* SWIER raises the line exactly as a pad edge does, so this proves the
     * whole path from line to vector without touching the board. */
    uint32_t hits = tiku_stm32n6_exti_hits(DIAG_BTN_PIN);
    TIKU_REG32(STM32N6_EXTI_SWIER1) = (1UL << DIAG_BTN_PIN);

    /* Poll for the handler's own counter rather than re-reading the pending
     * flag: an interrupt takes a few cycles to be taken, and reading the flag
     * straight after the trigger reports a miss that never happened. */
    unsigned long spins = 100000UL;
    while (tiku_stm32n6_exti_hits(DIAG_BTN_PIN) == hits && spins > 0UL) {
        spins--;
    }
    SHELL_PRINTF("  exti: software trigger -> handler %s (hits %lu)\n",
                 (spins > 0UL) ? "ran" : "NEVER RAN",
                 (unsigned long)tiku_stm32n6_exti_hits(DIAG_BTN_PIN));
}

/** @brief Show the watchdog, or arm it and stop feeding it. */
static void diag_wdt(uint8_t argc, const char *argv[]) {
    if (argc >= 3 && strcmp(argv[2], "bite") == 0) {
        /* ~1 s at 32 kHz. Nothing kicks it afterwards, so the reset that
         * follows is the proof; RCC_RSR then names the IWDG as the cause. */
        SHELL_PRINTF("  wdt: arming ~1 s and not feeding it; expect a reset\n");
        tiku_cpu_stm32n6_watchdog_on_arch(TIKU_WDT_SRC_ACLK, 32000U);
        for (;;) {
        }
    }
    SHELL_PRINTF("  wdt: RCC_RSR %08lx%s\n",
                 (unsigned long)TIKU_REG32(STM32N6_RCC_RSR),
                 (TIKU_REG32(STM32N6_RCC_RSR) & STM32N6_RCC_RSR_IWDGRSTF)
                     ? "  (last reset was the IWDG)" : "");
}

void tiku_shell_cmd_diag(uint8_t argc, const char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "fault") == 0) {
        if (argc >= 3) {
            diag_fault_force(argv[2]);
        } else {
            diag_fault_show();
        }
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        tiku_stm32n6_fault_clear();
        SHELL_PRINTF("  fault record cleared\n");
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "exti") == 0) {
        diag_exti(argc, argv);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "wdt") == 0) {
        diag_wdt(argc, argv);
        return;
    }
    SHELL_PRINTF("Usage: diag fault [undef|unalign|stack] | clear"
                 " | exti [wait] | wdt [bite]\n");
    diag_fault_show();
}

#endif /* PLATFORM_STM32N6 */

#if defined(PLATFORM_RA8P1)

#include <arch/ra8p1/tiku_fault_arch.h>
#include <arch/ra8p1/tiku_ra8p1_regs.h>
#include <kernel/memory/tiku_mem.h>

/** @brief Report the stored fault record, or say there is none. */
static void diag_fault_show(void) {
    const tiku_ra8p1_fault_record_t *f = tiku_ra8p1_fault_last();

    if (f->magic != TIKU_RA8P1_FAULT_MAGIC) {
        SHELL_PRINTF("  no fault recorded since the last cold start\n");
        return;
    }
    SHELL_PRINTF("  last %s (#%lu)\n", tiku_ra8p1_fault_kind_name(f->kind),
                 (unsigned long)f->count);
    SHELL_PRINTF("  cfsr=%lx hfsr=%lx addr=%lx\n",
                 (unsigned long)f->cfsr, (unsigned long)f->hfsr,
                 (unsigned long)f->addr);
    SHELL_PRINTF("  pc=%lx lr=%lx psr=%lx sp=%lx\n",
                 (unsigned long)f->pc, (unsigned long)f->lr,
                 (unsigned long)f->psr, (unsigned long)f->sp);
    SHELL_PRINTF("  exc=%lx msp=%lx psp=%lx\n",
                 (unsigned long)f->exc, (unsigned long)f->msp,
                 (unsigned long)f->psp);
    /* The frame verbatim: when a pop lands on stacked data, the eight
     * named fields above ARE the corruption, and only the raw words say
     * where the real frame sat. */
    SHELL_PRINTF("  frame %lx %lx %lx %lx\n",
                 (unsigned long)f->raw[0], (unsigned long)f->raw[1],
                 (unsigned long)f->raw[2], (unsigned long)f->raw[3]);
    SHELL_PRINTF("        %lx %lx %lx %lx\n",
                 (unsigned long)f->raw[4], (unsigned long)f->raw[5],
                 (unsigned long)f->raw[6], (unsigned long)f->raw[7]);
    SHELL_PRINTF("        %lx %lx %lx %lx\n",
                 (unsigned long)f->raw[8], (unsigned long)f->raw[9],
                 (unsigned long)f->raw[10], (unsigned long)f->raw[11]);
}

/** @brief Take one fault on purpose, so the handler can be seen working. */
static void diag_fault_force(const char *which) {
    SHELL_PRINTF("  forcing a %s fault; the board resets and `diag fault`"
                 " then shows it\n", which);

    if (strcmp(which, "undef") == 0) {
        /* No ambiguity: an undefined instruction is a precise UsageFault at a
         * known PC, with no bus or MPU involved. */
        __asm__ volatile ("udf #0");
    } else if (strcmp(which, "durable") == 0) {
        /* The violation this port exists to catch: a store into `.persistent`
         * that never opened the NVM window.  MPU says read-only, so it is a
         * DACCVIOL naming its own address in MMFAR -- and it must NOT be
         * silently dropped, which is the whole durable-write contract. */
        extern uint32_t __persistent_start;
        *(volatile uint32_t *)(uintptr_t)&__persistent_start = 0xDEADBEEFUL;
    } else {
        SHELL_PRINTF("  kinds: undef | durable\n");
        return;
    }
    SHELL_PRINTF("  (no fault taken -- UNEXPECTED, the guard is not working)\n");
}

void tiku_shell_cmd_diag(uint8_t argc, const char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "fault") == 0) {
        if (argc >= 3) {
            diag_fault_force(argv[2]);
        } else {
            diag_fault_show();
        }
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        tiku_ra8p1_fault_clear();
        SHELL_PRINTF("  fault record cleared\n");
        return;
    }
    SHELL_PRINTF("Usage: diag fault [undef|durable] | clear\n");
    diag_fault_show();
}

#endif /* PLATFORM_RA8P1 */
