/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_diag.c - "diag" command (STM32N6, RA8P1, ESP32-C61, C5).
 *
 * Forces and reports faults, and where the port has them exercises the EXTI
 * lines, the watchdog, the sleep settings and the PSRAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_diag.h"
#include <shell/tiku_shell.h>
#include <string.h>
#include <stdlib.h>

#if defined(PLATFORM_STM32N6)

#include <arch/stm32n6/tiku_fault_arch.h>
#include <arch/stm32n6/tiku_stm32n6_regs.h>
#include <arch/stm32n6/tiku_cpu_watchdog_arch.h>
#include <interfaces/gpio/tiku_gpio.h>
#include <hal/tiku_gpio_irq_hal.h>
#include <arch/stm32n6/tiku_gpio_irq_arch.h>
#include <kernel/process/tiku_process.h>
#include <kernel/cpu/tiku_common.h>   /* tiku_common_reset_reason */

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
 * @brief Take one fault of kind @p which; the handler records it and the
 *        board resets.
 *
 * @param which  "undef", "unalign" or "stack" (PSPLIM overflow).
 */
static void diag_fault_force(const char *which) {
    SHELL_PRINTF("  forcing a %s fault; the board resets and `diag fault`"
                 " then shows it\n", which);

    if (strcmp(which, "undef") == 0) {
        /* An undefined instruction is a precise UsageFault, taken at a
         * known PC, with no bus involved. */
        __asm__ volatile ("udf #0");
    } else if (strcmp(which, "unalign") == 0) {
        TIKU_REG32(STM32N6_SCB_CCR) |= (1UL << 3);      /* UNALIGN_TRP */
        __asm__ volatile ("dsb\n\tisb" ::: "memory");
        volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)0x34181001UL;
        (void)*p;
    } else if (strcmp(which, "stack") == 0) {
        static uint32_t fault_stack[64] __attribute__((aligned(8)));
        uintptr_t top = (uintptr_t)(fault_stack + 64);
        /* Handler mode uses MSP; the failing push uses a separate PSP. */
        __asm__ volatile (
            "msr psp, %0\n\t"
            "msr psplim, %0\n\t"
            "mrs r1, control\n\t"
            "orr r1, r1, #2\n\t"
            "msr control, r1\n\t"
            "isb\n\t"
            "push {r0}\n\t"
            "b ."
            :: "r"(top) : "r1", "memory");
        __builtin_unreachable();
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
        return;                     /* a press broadcasts TIKU_EVENT_GPIO */
    }

    /* A write to SWIER raises the line as a pad edge does, so the path from
     * line to vector runs with no button press. */
    uint32_t hits = tiku_stm32n6_exti_hits(DIAG_BTN_PIN);
    TIKU_REG32(STM32N6_EXTI_SWIER1) = (1UL << DIAG_BTN_PIN);

    /* The handler's own counter is polled for up to 100000 spins: the
     * interrupt is taken a few cycles after the trigger. */
    unsigned long spins = 100000UL;
    while (tiku_stm32n6_exti_hits(DIAG_BTN_PIN) == hits && spins > 0UL) {
        spins--;
    }
    SHELL_PRINTF("  exti: software trigger -> handler %s (hits %lu)\n",
                 (spins > 0UL) ? "ran" : "NEVER RAN",
                 (unsigned long)tiku_stm32n6_exti_hits(DIAG_BTN_PIN));
}

/**
 * @brief Print the last reset code, or with `bite` arm the watchdog and stop
 *        feeding it.
 */
static void diag_wdt(uint8_t argc, const char *argv[]) {
    if (argc >= 3 && strcmp(argv[2], "bite") == 0) {
        /* About 1 s at 32 kHz.  Nothing feeds it, so the board resets, and
         * the reset reason after the reboot names the watchdog. */
        SHELL_PRINTF("  wdt: arming ~1 s and not feeding it; expect a reset\n");
        tiku_cpu_stm32n6_watchdog_on_arch(TIKU_WDT_SRC_ACLK, 32000U);
        for (;;) {
        }
    }
    /* tiku_common_reset_reason() returns the SYSRSTIV-style code that
     * /sys/boot decodes; 0x0016 is a watchdog timeout.  Its first call
     * latches the code and clears RCC_RSR. */
    SHELL_PRINTF("  wdt: last reset code 0x%04x%s\n",
                 (unsigned)tiku_common_reset_reason(),
                 (tiku_common_reset_reason() == 0x0016U)
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
    /* The 12 words of the frame area as found.  When the frame is shifted,
     * the stacked pc, lr and psr printed above are the wrong words; the raw
     * words show where the real frame sits. */
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

/** @brief Take one fault of kind @p which; the board resets after it. */
static void diag_fault_force(const char *which) {
    SHELL_PRINTF("  forcing a %s fault; the board resets and `diag fault`"
                 " then shows it\n", which);

    if (strcmp(which, "undef") == 0) {
        /* An undefined instruction is a precise UsageFault at a known PC,
         * with no bus or MPU involved. */
        __asm__ volatile ("udf #0");
    } else if (strcmp(which, "durable") == 0) {
        /* A store into `.persistent` with the NVM window closed: the MPU maps
         * it read-only, so the store takes a DACCVIOL with its address in
         * MMFAR. */
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

#if defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_fault_arch.h>
#include <arch/esp32c61/tiku_cpu_common.h>
#include <arch/esp32c61/tiku_cpu_watchdog_arch.h>
#include <arch/esp32c61/tiku_sleep_arch.h>
#include <arch/esp32c61/tiku_psram_arch.h>
#include <arch/esp32c61/tiku_cpu_freq_boot_arch.h>
#include <arch/esp32c61/tiku_esp32c61_regs.h>

/** @brief Report the stored fault record, or say there is none. */
static void diag_fault_show(void) {
    const tiku_esp32c61_fault_record_t *f = tiku_esp32c61_fault_last();

    if (f->magic != TIKU_ESP32C61_FAULT_MAGIC) {
        SHELL_PRINTF("  no fault recorded\n");
        return;
    }
    SHELL_PRINTF("  last %s (#%lu)\n",
                 tiku_esp32c61_fault_kind_name(f->mcause & 0xFFFUL),
                 (unsigned long)f->count);
    SHELL_PRINTF("    mcause %08lx  mtval %08lx\n",
                 (unsigned long)f->mcause, (unsigned long)f->mtval);
    SHELL_PRINTF("    pc     %08lx  ra    %08lx  sp %08lx\n",
                 (unsigned long)f->pc, (unsigned long)f->ra,
                 (unsigned long)f->sp);
}

/* An address inside the PMP NULL guard.  It is volatile so the compiler emits
 * the access as written. */
static volatile uintptr_t diag_unmapped = 0x10UL;

/** @brief Take one exception of kind @p which; the board resets after it. */
static void diag_fault_force(const char *which) {
    SHELL_PRINTF("  forcing a %s fault; the board resets and `diag fault`"
                 " then shows it\n", which);
    if (strcmp(which, "illegal") == 0) {
        __asm__ volatile ("unimp");
    } else if (strcmp(which, "load") == 0) {
        (void)*(volatile uint32_t *)diag_unmapped;
    } else if (strcmp(which, "store") == 0) {
        *(volatile uint32_t *)diag_unmapped = 0UL;
    } else {
        SHELL_PRINTF("  kinds: illegal | load | store\n");
        return;
    }
    SHELL_PRINTF("  (no fault taken -- unexpected)\n");
}

/**
 * @brief Print the last reset's ROM code, or with `bite` arm the watchdog and
 *        stop feeding it.
 */
static void diag_wdt(uint8_t argc, const char *argv[]) {
    if (argc >= 3 && strcmp(argv[2], "bite") == 0) {
        /* About 1 s and never fed: the board resets, and the ROM reports the
         * TIMG0 watchdog as the cause. */
        SHELL_PRINTF("  wdt: arming ~1 s and not feeding it; expect a reset\n");
        tiku_cpu_esp32c61_watchdog_on_arch(TIKU_WDT_SRC_ACLK, 32768U);
        for (;;) {
        }
    }
    SHELL_PRINTF("  wdt: last reset rom code %lu%s\n",
                 (unsigned long)tiku_cpu_esp32c61_reset_code(),
                 (tiku_cpu_esp32c61_reset_reason() == 0x0016U)
                     ? "  (a watchdog)" : "");
}

/** @brief Show how the last boot began and the PMU's sleep settings; deep
 *         sleep is `power off` and light sleep `power nap`. */
static void diag_sleep(void) {
    static const struct {
        const char *name;
        uint32_t    reg;
    } regs[] = {
        { "pd top",     ESP32C61_PMU_PD_TOP_CNTL },
        { "pd hp-aon",  ESP32C61_PMU_PD_HPAON_CNTL },
        { "pd hp-cpu",  ESP32C61_PMU_PD_HPCPU_CNTL },
        { "pd wifi",    ESP32C61_PMU_PD_HPWIFI_CNTL },
        { "pd lp-peri", ESP32C61_PMU_PD_LPPERI_CNTL },
        { "pd mem",     ESP32C61_PMU_PD_MEM_CNTL },
        { "hp slp dig", ESP32C61_PMU_HP_SLP_DIG_POWER },
        { "lp slp dig", ESP32C61_PMU_LP_SLP_DIG_POWER },
        { "lp slp reg", ESP32C61_PMU_LP_SLP_REGULATOR0 },
        { "wake ena",   ESP32C61_PMU_SLP_CNTL2 },
    };

    SHELL_PRINTF("  boot: rom code %lu  wake cause %08lx%s\n",
                 (unsigned long)tiku_cpu_esp32c61_reset_code(),
                 (unsigned long)tiku_esp32c61_wake_cause(),
                 tiku_esp32c61_sleep_missed() ? "  (last sleep MISSED)" : "");
    SHELL_PRINTF("  lp timer: %lu Hz, at %lu\n",
                 (unsigned long)tiku_esp32c61_lp_hz(),
                 (unsigned long)tiku_esp32c61_lp_ticks());
    for (unsigned i = 0U; i < sizeof regs / sizeof regs[0]; i++) {
        SHELL_PRINTF("  %-10s %08lx\n", regs[i].name,
                     (unsigned long)TIKU_REG32(regs[i].reg));
    }
}

/**
 * @brief Bring the PSRAM up and check every word of it, or with `attach` hand
 *        it to the PSRAM tier.
 */
static void diag_psram(uint8_t argc, const char *argv[]) {
    volatile uint32_t *w = (volatile uint32_t *)TIKU_ESP32C61_PSRAM_BASE;
    tiku_esp32c61_psram_err_t rc;
    uint32_t words, bad = 0UL;
    uint64_t t0, t1, t2;

    if (argc >= 3 && strcmp(argv[2], "attach") == 0) {
        rc = tiku_esp32c61_psram_attach();
        SHELL_PRINTF("  psram: attach %d\n", (int)rc);
        return;
    }
    rc = tiku_esp32c61_psram_init();
    SHELL_PRINTF("  psram: init %d  id %06lx  %lu KB at %08lx\n", (int)rc,
                 (unsigned long)tiku_esp32c61_psram_id(),
                 (unsigned long)(tiku_esp32c61_psram_size() / 1024UL),
                 (unsigned long)TIKU_ESP32C61_PSRAM_BASE);
    if (rc != TIKU_ESP32C61_PSRAM_OK) {
        return;
    }
    /* Destructive: an address-unique word everywhere, written back past
     * the cache, then read and checked. */
    words = tiku_esp32c61_psram_size() / 4UL;
    t0 = tiku_cpu_esp32c61_systimer();
    for (uint32_t i = 0UL; i < words; i++) {
        w[i] = (i * 2654435761UL) ^ 0x55AA55AAUL;
    }
    (void)ESP32C61_ROM_CACHE_WB_INVAL(TIKU_ESP32C61_PSRAM_BASE,
                                      tiku_esp32c61_psram_size());
    t1 = tiku_cpu_esp32c61_systimer();
    for (uint32_t i = 0UL; i < words; i++) {
        if (w[i] != ((i * 2654435761UL) ^ 0x55AA55AAUL)) {
            bad++;
        }
    }
    t2 = tiku_cpu_esp32c61_systimer();
    SHELL_PRINTF("  psram: %lu words, %lu bad; write %lu KB/s, read %lu KB/s\n",
                 (unsigned long)words, (unsigned long)bad,
                 (unsigned long)((uint64_t)words * 4ULL * 16000ULL / 1024ULL /
                                 ((t1 - t0) / 1000ULL + 1ULL)),
                 (unsigned long)((uint64_t)words * 4ULL * 16000ULL / 1024ULL /
                                 ((t2 - t1) / 1000ULL + 1ULL)));
}

void tiku_shell_cmd_diag(uint8_t argc, const char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "psram") == 0) {
        diag_psram(argc, argv);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "fault") == 0) {
        if (argc >= 3) {
            diag_fault_force(argv[2]);
        } else {
            diag_fault_show();
        }
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        tiku_esp32c61_fault_clear();
        SHELL_PRINTF("  fault record cleared\n");
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "wdt") == 0) {
        diag_wdt(argc, argv);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "sleep") == 0) {
        diag_sleep();
        return;
    }
    SHELL_PRINTF("Usage: diag fault [illegal|load|store] | clear | wdt [bite]"
                 " | sleep | psram [attach]\n");
    diag_fault_show();
}
#endif /* PLATFORM_ESP32C61 */

#if defined(PLATFORM_ESP32C5)
#include <arch/esp32c5/tiku_fault_arch.h>
#include <arch/esp32c5/tiku_timer_arch.h>
#include <arch/esp32c5/tiku_pmu_arch.h>
#include <arch/esp32c5/tiku_clock_arch.h>
#include <arch/esp32c5/tiku_esp32c5_regs.h>

/** @brief Report the retained fault record, or say there is none. */
static void diag_fault_show(void)
{
    const tiku_c5_fault_record_t *f = tiku_c5_fault_last();

    if (f->magic != TIKU_C5_FAULT_MAGIC) {
        SHELL_PRINTF("  no fault recorded\n");
        return;
    }
    if (f->fatal) {
        SHELL_PRINTF("  last fatal: %s (#%lu, %lu resets in a row)\n",
                     f->reason, (unsigned long)f->count,
                     (unsigned long)f->resets);
        return;
    }
    SHELL_PRINTF("  last %s (#%lu, %lu resets in a row)\n",
                 tiku_c5_fault_kind_name(f->mcause & 0xFFFUL),
                 (unsigned long)f->count, (unsigned long)f->resets);
    SHELL_PRINTF("    mcause %08lx  mtval %08lx  pc %08lx\n",
                 (unsigned long)f->mcause, (unsigned long)f->mtval,
                 (unsigned long)f->pc);
}

/* An address inside the PMP NULL guard (0..0xfff).  It is volatile so the
 * compiler emits the access as written. */
static volatile uintptr_t diag_unmapped = 0x10UL;

/** @brief Take one exception of kind @p which; the board resets after it. */
static void diag_fault_force(const char *which)
{
    SHELL_PRINTF("  forcing a %s fault; the board resets and `diag fault`"
                 " then shows it\n", which);
    if (strcmp(which, "fatal") == 0) {
        tiku_c5_fatal("diag fault fatal");
    } else if (strcmp(which, "illegal") == 0) {
        __asm__ volatile ("unimp");
    } else if (strcmp(which, "load") == 0) {
        (void)*(volatile uint32_t *)diag_unmapped;
    } else if (strcmp(which, "store") == 0) {
        *(volatile uint32_t *)diag_unmapped = 0UL;
    } else {
        SHELL_PRINTF("  kinds: illegal | load | store | fatal\n");
        return;
    }
    SHELL_PRINTF("  (no fault taken -- unexpected)\n");
}

void tiku_shell_cmd_diag(uint8_t argc, const char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "fault") == 0) {
        if (argc >= 3) {
            diag_fault_force(argv[2]);
        } else {
            diag_fault_show();
        }
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        tiku_c5_fault_clear();
        SHELL_PRINTF("  fault record cleared\n");
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "voltage") == 0) {
        /* The HP active regulator's DBIAS field (bits 31:27 of 0x600B0028)
         * and, under PVT tracking, the HP and LP values it follows (bits
         * 13:9 and 8:4). */
        static const char *const pvt[] = {"tracking", "off", "unsupported"};
        uint32_t reg = TIKU_C5_REG_READ(0x600B0028u);
        SHELL_PRINTF("  core dbias %lu (register %lu) pvt %s: hp %lu lp %lu\n",
                     (unsigned long)tiku_c5_core_voltage_dbias(),
                     (unsigned long)(reg >> 27), pvt[tiku_c5_pvt_result()],
                     (unsigned long)((reg >> 9) & 31u),
                     (unsigned long)((reg >> 4) & 31u));
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "clock") == 0) {
        /* PCR SYSCLK/CPU/AHB, PMU immediate clock power, analog CONF0. */
        SHELL_PRINTF("  pll result %d  sysclk %08lx cpu %08lx ahb %08lx\n"
                     "  ckpower %08lx conf0 %08lx lpcon %08lx fault %d\n",
                     tiku_c5_pll_result(),
                     (unsigned long)TIKU_C5_REG_READ(0x60096110u),
                     (unsigned long)TIKU_C5_REG_READ(0x60096118u),
                     (unsigned long)TIKU_C5_REG_READ(0x6009611Cu),
                     (unsigned long)TIKU_C5_REG_READ(0x600B00CCu),
                     (unsigned long)TIKU_C5_REG_READ(0x600AF818u),
                     (unsigned long)TIKU_C5_REG_READ(0x600AF018u),
                     tiku_c5_clock_fault());
        return;
    }
    if (argc >= 3 && strcmp(argv[1], "peek") == 0) {
        /* Up to 64 words from a 4-byte-aligned address, four a line. */
        uintptr_t a = (uintptr_t)strtoul(argv[2], NULL, 16);
        uint32_t n = argc >= 4 ? (uint32_t)strtoul(argv[3], NULL, 10) : 1U, j;
        if (a & 3u || n == 0 || n > 64u) {
            SHELL_PRINTF("  peek: aligned address, 1..64 words\n");
            return;
        }
        for (j = 0; j < n; j++) {
            if (j % 4u == 0u) {
                SHELL_PRINTF("%s  %08lx:", j ? "\n" : "",
                             (unsigned long)(a + 4u * j));
            }
            SHELL_PRINTF(" %08lx",
                         (unsigned long)TIKU_C5_REG_READ(a + 4u * j));
        }
        SHELL_PRINTF("\n");
        return;
    }
    SHELL_PRINTF("Usage: diag fault [illegal|load|store|fatal] | clear"
                 " | voltage | clock | peek <hex> [words]\n");
    diag_fault_show();
}
#endif /* PLATFORM_ESP32C5 */
