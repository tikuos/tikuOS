/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clock_pll.c - P4 v1.3 SRAM-only CPU frequency selection.
 *
 * Starts the CPLL through the PMU and switches the CPU among 40, 90, 180
 * and 360 MHz.  Each change returns to the crystal first and loads dividers
 * that hold memory at 180 MHz or below and APB at 90 MHz or below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_clock_arch.h"
#include "tiku_pmu_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32p4_regs.h"

static int pll_ready;

int tiku_p4_clock_pll_init(void)
{
    int result;
    pll_ready = 0;
    if (tiku_p4_clock_xtal_init() != 0) return -1;
    result = tiku_p4_pmu_cpll_init();
    if (result == 0) pll_ready = 1;
    return result;
}

static void (*clock_follower)(unsigned mhz);

void tiku_p4_clock_follow(void (*follower)(unsigned mhz))
{
    clock_follower = follower;
}

/** @brief The CPU clock in MHz from the root selection and divider. */
static unsigned current_mhz(void)
{
    if ((TIKU_P4_REG_READ(TIKU_P4_ROOT_SELECT) & 3u) == 0) return 40u;
    return 360u / (1u + ((TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV0) >> 5) & 255u));
}

int tiku_p4_clock_set_mhz(unsigned mhz)
{
    uint32_t state, root, divider, cpu_div, mem_div, apb_div, tries;
    int result = -2;
    if (mhz != 40u && mhz != 90u && mhz != 180u && mhz != 360u) return -1;
    if (mhz != 40u && !pll_ready) return -1;
    /* The follower slows before the CPU does and then takes the clock the
     * CPU ended on, which is the crystal after a failed switch. */
    if (clock_follower && mhz < current_mhz()) clock_follower(mhz);
    state = TIKU_P4_IRQ_SAVE();
    if (tiku_p4_clock_xtal_init() != 0) goto done;
    if (mhz == 40u) {
        result = 0;
        goto done;
    }
    cpu_div = 360u / mhz;
    mem_div = mhz == 360u ? 2u : 1u;
    apb_div = mhz == 90u ? 1u : 2u;
    /* Dividers are loaded on XTAL before selecting CPLL: MEM/SYS <=180 MHz,
     * APB <=90 MHz, including transitions between two CPLL rates. */
    divider = TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV0) & ~0x1ffffff0u;
    divider |= (cpu_div - 1u) << 5;
    TIKU_P4_REG_WRITE(TIKU_P4_ROOT_DIV0, divider);
    TIKU_P4_REG_WRITE(TIKU_P4_ROOT_DIV1, mem_div - 1u);
    TIKU_P4_REG_WRITE(TIKU_P4_ROOT_DIV2, (apb_div - 1u) << 16);
    TIKU_P4_REG_WRITE(TIKU_P4_ROOT_DIV0, divider | (1u << 4));
    for (tries = 0; tries < 4096u; tries++) {
        if (!(TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV0) & (1u << 4))) {
            if ((TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV0) & 0x1fffffe0u) !=
                (cpu_div - 1u) << 5 ||
                TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV1) != mem_div - 1u ||
                TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV2) != (apb_div - 1u) << 16 ||
                (TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV3) & 255u) != 0) break;
            root = TIKU_P4_REG_READ(TIKU_P4_ROOT_SELECT);
            TIKU_P4_REG_WRITE(TIKU_P4_ROOT_SELECT, (root & ~3u) | 1u);
            if ((TIKU_P4_REG_READ(TIKU_P4_ROOT_SELECT) & 3u) == 1u) result = 0;
            break;
        }
    }
done:
    TIKU_P4_IRQ_RESTORE(state);
    if (clock_follower) clock_follower(current_mhz());
    return result;
}
