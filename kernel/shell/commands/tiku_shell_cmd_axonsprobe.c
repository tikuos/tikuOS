/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_axonsprobe.c - Axon NPU (nRF54LM20B) bring-up probe.
 *
 * The nRF54LM20B carries the Axon NPU as peripheral NRF_AXONS @ 0x50056000
 * (IRQn 86, MCU power domain).  The PUBLIC register model is only a power
 * wrapper -- ENABLE.EN @ +0x400 and STATUS.READY @ +0x404 ("AXONS is
 * accessible") -- with the first 1 KB reserved and no nrfx HAL, no SVD block
 * and no documentation for the engine itself (the programming model lives in
 * Nordic's Neuton toolchain).  This probe is the on-die recon tool for that
 * unknown, the same role cryptoprobe played for CRACEN:
 *
 *   axonsprobe            ENABLE/STATUS + FICR identity
 *   axonsprobe en         enable, spin READY (bounded), report time-to-ready
 *   axonsprobe off        disable
 *   axonsprobe dump [o n] hex-dump n words of the 4 KB slot from offset o
 *                         (prints each address BEFORE reading: a bus fault
 *                         parks the core and the last line marks the edge)
 *   axonsprobe diff       snapshot the reserved window, enable, wait, then
 *                         print every word that CHANGED (finds live registers
 *                         without a single write)
 *   axonsprobe irq        NVIC-enable IRQ 86 + count what fires on enable
 *
 * READ-ONLY by design: no blind writes into an undocumented engine.  Findings
 * gate the Axon support plan's next phases.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_axonsprobe.h"

#if TIKU_SHELL_CMD_AXONSPROBE

#include <kernel/shell/tiku_shell_io.h>
#include <arch/nordic/tiku_device_select.h>
#include <arch/nordic/tiku_cpu_common.h>
#include <string.h>
#include <stdlib.h>

#if defined(TIKU_DEVICE_HAS_AXONS) && TIKU_DEVICE_HAS_AXONS

#if defined(TIKU_AXON_ENABLE) && TIKU_AXON_ENABLE
/* First-light path: Nordic's Axon driver core linked from the gitignored
 * temp/axon-models checkout, on the TikuOS platform layer
 * (arch/nordic/tiku_axon_platform.c). */
#include "drivers/axon/nrf_axon_driver.h"
#include "axon/nrf_axon_platform.h"
#include "drivers/axon/nrf_axon_dsp_intrinsics.h"
#endif

/* Base + the two documented registers (offsets from the MDK struct). */
#define AXONS_BASE        0x50056000UL
#define AXONS_REG(off)    (*(volatile uint32_t *)(AXONS_BASE + (off)))
#define AXONS_ENABLE      AXONS_REG(0x400u)
#define AXONS_STATUS      AXONS_REG(0x404u)

/** Reserved engine window: 256 words (0x000..0x3FF). */
#define AXONS_WIN_WORDS   256u

/** Bounded READY spin (each iteration ~a few cycles at 128 MHz). */
#define AXONS_READY_SPIN  2000000ul

/** IRQ-storm brake for the probe ISR. */
#define AXONS_IRQ_LIMIT   16u

static volatile uint32_t axons_irq_count;

#if !defined(TIKU_AXON_ENABLE) || !TIKU_AXON_ENABLE
/** @brief Raw-probe ISR for IRQn 86 -- count, then self-disable on a storm.
 *  (With TIKU_AXON_ENABLE the platform layer owns the ISR and forwards to
 *  the vendor driver instead.) */
void tiku_nordic_axons_isr(void)
{
    axons_irq_count++;
    if (axons_irq_count >= AXONS_IRQ_LIMIT) {
        tiku_nordic_nvic_disable(86);
    }
}
#endif

/** @brief Enable the block and spin for READY; returns spins used or 0. */
static uint32_t axons_enable_wait(void)
{
    uint32_t spins;

    AXONS_ENABLE = 1u;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    for (spins = 1u; spins <= AXONS_READY_SPIN; spins++) {
        if ((AXONS_STATUS & 1u) != 0u) {
            return spins;
        }
    }
    return 0u;
}

static void axons_info(void)
{
    SHELL_PRINTF("AXONS @ 0x%x (nRF54LM20B Axon NPU wrapper)\n",
                 (unsigned)AXONS_BASE);
    SHELL_PRINTF("  ENABLE = 0x%x\n", (unsigned)AXONS_ENABLE);
    SHELL_PRINTF("  STATUS = 0x%x %s\n", (unsigned)AXONS_STATUS,
                 (AXONS_STATUS & 1u) ? "(READY)" : "(not ready)");
    SHELL_PRINTF("  FICR part=0x%x rev=0x%x\n",
                 (unsigned)*(volatile uint32_t *)0x00FFC340ul,
                 (unsigned)*(volatile uint32_t *)0x00FFC344ul);
}

static void axons_dump(uint32_t off, uint32_t words)
{
    uint32_t i;

    if (words == 0u || words > 1024u) {
        words = 16u;
    }
    off &= ~3ul;
    for (i = 0u; i < words; i++) {
        uint32_t o = off + 4u * i;
        if ((i & 3u) == 0u) {
            /* Address printed BEFORE the reads: a faulting window edge is
             * identified by the last line that appears. */
            SHELL_PRINTF("\n  +%x:", (unsigned)o);
        }
        SHELL_PRINTF(" %x", (unsigned)AXONS_REG(o));
    }
    SHELL_PRINTF("\n");
}

static void axons_diff(void)
{
    static uint32_t before[AXONS_WIN_WORDS];  /* 1 KB: static, not stack */
    uint32_t i;
    uint32_t spins;
    uint32_t changed = 0u;

    AXONS_ENABLE = 0u;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    for (i = 0u; i < AXONS_WIN_WORDS; i++) {
        before[i] = AXONS_REG(4u * i);
    }
    spins = axons_enable_wait();
    SHELL_PRINTF("enable: READY=%u (spins=%u)\n",
                 (unsigned)(AXONS_STATUS & 1u), (unsigned)spins);
    for (i = 0u; i < AXONS_WIN_WORDS; i++) {
        uint32_t now = AXONS_REG(4u * i);
        if (now != before[i]) {
            SHELL_PRINTF("  +%x: %x -> %x\n",
                         (unsigned)(4u * i), (unsigned)before[i],
                         (unsigned)now);
            changed++;
        }
    }
    SHELL_PRINTF("%u of %u words changed across enable\n",
                 (unsigned)changed, (unsigned)AXONS_WIN_WORDS);
}

void tiku_shell_cmd_axonsprobe(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "en") == 0) {
        uint32_t spins = axons_enable_wait();
        if (spins != 0u) {
            SHELL_PRINTF("READY after %u spins\n", (unsigned)spins);
        } else {
            SHELL_PRINTF("NOT ready after %u spins (ENABLE=%x STATUS=%x)\n",
                         (unsigned)AXONS_READY_SPIN,
                         (unsigned)AXONS_ENABLE, (unsigned)AXONS_STATUS);
        }
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "off") == 0) {
        AXONS_ENABLE = 0u;
        SHELL_PRINTF("disabled (ENABLE=%x STATUS=%x)\n",
                     (unsigned)AXONS_ENABLE, (unsigned)AXONS_STATUS);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "dump") == 0) {
        uint32_t off   = (argc >= 3)
                       ? (uint32_t)strtoul(argv[2], (char **)0, 16) : 0u;
        uint32_t words = (argc >= 4)
                       ? (uint32_t)strtoul(argv[3], (char **)0, 10) : 16u;
        axons_dump(off, words);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "diff") == 0) {
        axons_diff();
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "irq") == 0) {
        axons_irq_count = 0u;
        tiku_nordic_nvic_enable(86);
        SHELL_PRINTF("IRQ 86 armed; enabling block...\n");
        (void)axons_enable_wait();
        tiku_cpu_nordic_delay_ms(50u);
        SHELL_PRINTF("irq count = %u (STATUS=%x)\n",
                     (unsigned)axons_irq_count, (unsigned)AXONS_STATUS);
        return;
    }

#if defined(TIKU_AXON_ENABLE) && TIKU_AXON_ENABLE
    /* One-time vendor platform/driver init shared by every subcommand that
     * enters the blob.  Skipping it leaves the driver's engine base NULL and
     * the first intrinsic bus-faults at base+offset (BFAR 0x520, seen on HW
     * when `fir` ran before `hw`). */
    {
        static int axon_inited;
        if (!axon_inited && argc >= 2 &&
            (strcmp(argv[1], "hw") == 0 ||
             strcmp(argv[1], "acc") == 0 ||
             strcmp(argv[1], "fir") == 0)) {
            nrf_axon_result_e rc = nrf_axon_platform_init();
            SHELL_PRINTF("nrf_axon_platform_init -> %d\n", (int)rc);
            if (rc != NRF_AXON_RESULT_SUCCESS) {
                return;
            }
            axon_inited = 1;
        }
    }
    if (argc >= 2 && strcmp(argv[1], "hw") == 0) {
        if (!nrf_axon_platform_reserve_for_user()) {
            SHELL_PRINTF("reserve failed\n");
            return;
        }
        SHELL_PRINTF("powered on: ENABLE=%x STATUS=%x %s\n",
                     (unsigned)AXONS_ENABLE, (unsigned)AXONS_STATUS,
                     (AXONS_STATUS & 1u) ? "(READY!)" : "(still not ready)");
        axons_dump(0u, 16u);
        nrf_axon_platform_free_reservation_from_user();
        SHELL_PRINTF("released: ENABLE=%x STATUS=%x\n",
                     (unsigned)AXONS_ENABLE, (unsigned)AXONS_STATUS);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "acc") == 0) {
        /* KAT: sum of a 24-bit vector on the NPU vs the CPU loop.
         * axon_acc_24_32 handles reservation itself (keep_reservation=false)
         * -- but platform init must have run (axonsprobe hw first). */
        static int32_t x[16] __attribute__((aligned(4)));
        int32_t hw_out = 0;
        int32_t sw_out = 0;
        uint32_t i;
        uint32_t t0, t1;
        nrf_axon_result_e rc;

        for (i = 0u; i < 16u; i++) {
            x[i] = (int32_t)(i * 1000u + 7u) - 8000;   /* mixed signs */
            sw_out += x[i];
        }
        t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        rc = axon_acc_24_32(x, &hw_out, 16u, 0u,
                            NRF_AXON_SYNC_MODE_BLOCKING_POLLING, false);
        t1 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        SHELL_PRINTF("axon_acc_24_32 rc=%d  hw=%d sw=%d  %s  (%u us)\n",
                     (int)rc, (int)hw_out, (int)sw_out,
                     (rc == NRF_AXON_RESULT_SUCCESS && hw_out == sw_out)
                         ? "MATCH" : "MISMATCH",
                     (unsigned)(t1 - t0));
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "fir") == 0) {
        /* MAC-throughput benchmark with an EXACT CPU reference: a batch of
         * dot products (axon_mar_24_24_32 -- 32-bit output, no truncation).
         * REPS x LEN multiply-accumulates on the NPU vs a plain int32 CPU
         * loop, both timed on the 1 MHz GRTC.  Values are bounded so 24-bit
         * operands and the 32-bit accumulator never saturate, so hw==sw is a
         * true KAT rather than a convention guess (the FIR intrinsic's
         * fixed-point rounding makes a naive reference ambiguous). */
        enum { DOT_LEN = 512, DOT_REPS = 128 };  /* 65,536 MACs/rep */
        static int32_t x[DOT_LEN] __attribute__((aligned(4)));
        static int32_t y[DOT_LEN] __attribute__((aligned(4)));
        uint32_t i, r, t0, t_hw, t_sw;
        int32_t hw = 0, sw = 0;
        uint32_t mism = 0u;
        nrf_axon_result_e rc = NRF_AXON_RESULT_SUCCESS;

        for (i = 0u; i < DOT_LEN; i++) {
            x[i] = (int32_t)(((i * 2654435761u) >> 20) & 0x7Fu) - 64;  /* [-64,63] */
            y[i] = (int32_t)(((i * 40503u) >> 6) & 0x7Fu) - 64;
        }

        t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        for (r = 0u; r < DOT_REPS; r++) {
            int32_t out = 0;
            nrf_axon_result_e rr = axon_mar_24_24_32(
                x, y, &out, DOT_LEN, 0u,
                NRF_AXON_SYNC_MODE_BLOCKING_POLLING, false);
            if (rr != NRF_AXON_RESULT_SUCCESS) { rc = rr; }
            hw = out;
        }
        t_hw = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0;

        t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        for (r = 0u; r < DOT_REPS; r++) {
            int32_t acc = 0;
            for (i = 0u; i < DOT_LEN; i++) {
                acc += x[i] * y[i];
            }
            sw = acc;
        }
        t_sw = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0;

        if (hw != sw) {
            SHELL_PRINTF("  hw=%d sw=%d\n", (int)hw, (int)sw);
            mism = 1u;
        }
        SHELL_PRINTF("dot 128x512 (65K MAC) rc=%d  npu=%u us  cpu=%u us  "
                     "speedup=%u.%ux  %s\n",
                     (int)rc, (unsigned)t_hw, (unsigned)t_sw,
                     (unsigned)(t_sw / (t_hw ? t_hw : 1u)),
                     (unsigned)((10u * t_sw / (t_hw ? t_hw : 1u)) % 10u),
                     (rc == NRF_AXON_RESULT_SUCCESS && mism == 0u)
                         ? "MATCH" : "CHECK");
        return;
    }
#if defined(TIKU_AXON_MODEL_TEST) && TIKU_AXON_MODEL_TEST
    if (argc >= 2 && strcmp(argv[1], "model") == 0) {
        /* Nordic's portable inference test: runs the compiled model
         * (TIKU_AXON_MODEL=... on the make line) against its shipped test
         * vectors and prints per-vector results. */
        extern void base_inference_main(void);
        uint32_t t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        base_inference_main();
        SHELL_PRINTF("model run total: %u us\n",
                     (unsigned)(NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0));
        return;
    }
#endif
#endif /* TIKU_AXON_ENABLE */

    axons_info();
#if defined(TIKU_AXON_ENABLE) && TIKU_AXON_ENABLE
    SHELL_PRINTF("usage: axonsprobe [en|off|dump <off> <n>|diff|irq|hw|acc]\n");
#else
    SHELL_PRINTF("usage: axonsprobe [en|off|dump <off> <n>|diff|irq]\n");
#endif
}

#else /* !TIKU_DEVICE_HAS_AXONS */

void tiku_shell_cmd_axonsprobe(int argc, char **argv)
{
    (void)argc; (void)argv;
    SHELL_PRINTF("no AXONS block on this device (build MCU=nrf54lm20b)\n");
}

#endif /* TIKU_DEVICE_HAS_AXONS */

#endif /* TIKU_SHELL_CMD_AXONSPROBE */
