/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clock_arch.c - P4 CPU and bus startup on the 40 MHz crystal.
 *
 * Moves the CPU root to the crystal before clearing the CPU, memory, system
 * and APB dividers, then waits a bounded time for the divider update.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_clock_arch.h"
#include "tiku_esp32p4_regs.h"

int tiku_p4_clock_xtal_init(void)
{
    uint32_t value, tries;
    /* Switch the root before reducing bus dividers to avoid an overclock. */
    value = TIKU_P4_REG_READ(TIKU_P4_ROOT_SELECT);
    TIKU_P4_REG_WRITE(TIKU_P4_ROOT_SELECT, value & ~3u);
    if ((TIKU_P4_REG_READ(TIKU_P4_ROOT_SELECT) & 3u) != 0) return -1;
    value = TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV0) & ~0x1fffffe0u;
    TIKU_P4_REG_WRITE(TIKU_P4_ROOT_DIV0, value);
    TIKU_P4_REG_WRITE(TIKU_P4_ROOT_DIV1, 0);
    TIKU_P4_REG_WRITE(TIKU_P4_ROOT_DIV2, 0);
    TIKU_P4_REG_WRITE(TIKU_P4_ROOT_DIV3,
                      TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV3) & ~255u);
    TIKU_P4_REG_WRITE(TIKU_P4_ROOT_DIV0, value | (1u << 4));
    for (tries = 0; tries < 4096u; tries++) {
        if (!(TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV0) & (1u << 4))) {
            if ((TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV0) & 0x1fffffe0u) != 0 ||
                TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV1) != 0 ||
                TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV2) != 0 ||
                (TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV3) & 255u) != 0) return -1;
            return 0;
        }
    }
    return -1;
}
