/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_p4x_panel.c - EK79007 reset, two-lane setup and backlight.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_p4x_panel.h"
#include <hal/tiku_clock_hal.h>
#include "tiku_board_p4x_function_ev.h"
#include "../tiku_esp32p4_regs.h"
#include "../tiku_dsi_arch.h"

static void gpio_output(unsigned pin, unsigned high)
{
    const uintptr_t mux = 0x500e1004u + 4u * pin;
    TIKU_P4_REG_WRITE(high ? 0x500e0008u : 0x500e000cu, 1u << pin);
    TIKU_P4_REG_WRITE(mux, (TIKU_P4_REG_READ(mux) & ~(7u << 12)) | (1u << 12));
    TIKU_P4_REG_WRITE(0x500e0558u + 4u * pin, 256u);
    TIKU_P4_REG_WRITE(0x500e0024u, 1u << pin);
}

int tiku_p4x_panel_init(void)
{
    static const uint8_t settings[] = {0x8b, 0x78, 0x84, 0x88, 0xa8, 0xe3, 0x88};
    gpio_output(TIKU_BOARD_LCD_BL_PIN, 0);
    int rc = tiku_p4_dsi_init();
    if (rc) return rc;
    gpio_output(TIKU_BOARD_LCD_RESET_PIN, 0);
    tiku_clock_arch_delay(10000);
    gpio_output(TIKU_BOARD_LCD_RESET_PIN, 1);
    tiku_clock_arch_delay(20000);
    if (tiku_p4_dsi_command(0xb2, 0x10)) return -3;
    for (unsigned i = 0; i < sizeof settings; ++i) {
        if (tiku_p4_dsi_command(0x80u + i, settings[i])) return -3;
    }
    if (tiku_p4_dsi_command(0x11, -1)) return -3;
    tiku_clock_arch_delay(120000);
    return 0;
}

void tiku_p4x_panel_light(void)
{
    gpio_output(TIKU_BOARD_LCD_BL_PIN, 1);
}

int tiku_p4x_panel_pattern(void)
{
    int rc = tiku_p4x_panel_init();
    if (rc) return rc;
    if (tiku_p4_dsi_pattern()) return -4;
    tiku_p4x_panel_light();
    return 0;
}
