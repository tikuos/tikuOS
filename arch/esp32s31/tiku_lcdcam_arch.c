/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lcdcam_arch.c - Reference-counted LCD_CAM clocks and reset.
 * Register fields follow ESP-IDF 4d59230 S31 HP_SYS_CLKRST definitions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include "tiku_lcdcam_arch.h"

#ifndef TIKU_S31_LCDCAM_REG
#define TIKU_S31_LCDCAM_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#endif
#define REG(a) TIKU_S31_LCDCAM_REG(a)
/* HP_SYS_CLKRST_LCDCAM_CTRL0: APB and system clocks in bits 0 and 2, their
 * resets in bits 1 and 3. */
#define BLOCK 0x2058714cu
/* HP_SYS_CLKRST_LCDCAM_LCDCAM_CTRL0: source 0 (crystal) and enable, bit 2. */
#define CORE 0x20587154u

static unsigned users;

void tiku_s31_lcdcam_claim(void)
{
    if (users++) return;
    REG(CORE) = 4u;
    REG(BLOCK) = 15u;
    REG(BLOCK) = 5u;
}

void tiku_s31_lcdcam_release(void)
{
    if (!users || --users) return;
    REG(CORE) &= ~4u;
    REG(BLOCK) = 10u;
}
