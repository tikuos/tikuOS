/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot_arch.c - P4 watchdog handoff using peripheral registers.
 *
 * Clears the TG0, TG1 and LP watchdogs' enable and flash-boot bits under
 * their keys, sets the super watchdog to feed itself, and holds CPU1 (and,
 * in an image without the LP payload, the LP core) in reset, clock gated.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_boot_arch.h"
#include "tiku_esp32p4_regs.h"
#if defined(TIKU_BOARD_ESP32_P4X_FUNCTION_EV_BOARD)
#include "boards/tiku_board_p4x_function_ev.h"
#include "tiku_irq_arch.h"
#include <string.h>
#endif

void tiku_esp32p4_boot_park_cores(void)
{
    uint32_t value = TIKU_P4_REG_READ(TIKU_P4_HP_RESET0);
    TIKU_P4_REG_WRITE(TIKU_P4_HP_RESET0, value | (1u << 8));
    value = TIKU_P4_REG_READ(TIKU_P4_SOC_CLK0);
    TIKU_P4_REG_WRITE(TIKU_P4_SOC_CLK0, value & ~((1u << 4) | (1u << 1)));
#if !TIKU_LPCORE_ENABLE
    value = TIKU_P4_REG_READ(TIKU_P4_LP_RESET);
    TIKU_P4_REG_WRITE(TIKU_P4_LP_RESET, value | (1u << 31));
    value = TIKU_P4_REG_READ(TIKU_P4_LP_CLK);
    TIKU_P4_REG_WRITE(TIKU_P4_LP_CLK, value & ~(1u << 31));
#endif
}

/** @brief Unlock, clear the watchdog enable bits, and restore write protection.
 */
static void disable(uintptr_t config, uintptr_t protect, uint32_t mask)
{
    TIKU_P4_REG_WRITE(protect, TIKU_P4_WDT_KEY);
    TIKU_P4_REG_WRITE(config, TIKU_P4_REG_READ(config) & ~mask);
    TIKU_P4_REG_WRITE(protect, 0u);
}

void tiku_esp32p4_boot_watchdogs_disable(void)
{
    disable(TIKU_P4_TG0_WDT_CONFIG, TIKU_P4_TG0_WDT_PROTECT,
            TIKU_P4_WDT_ENABLE | TIKU_P4_TG_WDT_FLASHBOOT);
    disable(TIKU_P4_TG1_WDT_CONFIG, TIKU_P4_TG1_WDT_PROTECT,
            TIKU_P4_WDT_ENABLE | TIKU_P4_TG_WDT_FLASHBOOT);
    disable(TIKU_P4_LP_WDT_CONFIG, TIKU_P4_LP_WDT_PROTECT,
            TIKU_P4_WDT_ENABLE | TIKU_P4_LP_WDT_FLASHBOOT);
    TIKU_P4_REG_WRITE(TIKU_P4_SWD_PROTECT, TIKU_P4_WDT_KEY);
    TIKU_P4_REG_WRITE(TIKU_P4_SWD_CONFIG, TIKU_P4_REG_READ(TIKU_P4_SWD_CONFIG) |
                                              TIKU_P4_SWD_AUTO_FEED);
    TIKU_P4_REG_WRITE(TIKU_P4_SWD_PROTECT, 0u);
}

#if defined(TIKU_BOARD_ESP32_P4X_FUNCTION_EV_BOARD)
int tiku_p4x_boot_read(uint32_t offset, void *dest, uint32_t size)
{
    /* The ECO7 ROM SPI reader takes an internal SRAM destination. */
    int (*const read_flash)(uint32_t, uint32_t *, int32_t) =
        (int (*)(uint32_t, uint32_t *, int32_t))0x4fc00158u;
    uint32_t block[128];
    uint8_t *out = dest;
    if (!out || ((offset | size) & 3u) || offset > 0x1000000u ||
        size > 0x1000000u - offset) return -1;
    while (size) {
        unsigned n = size > sizeof block ? sizeof block : size;
        uint32_t irq = TIKU_P4_IRQ_SAVE();
        int rc = read_flash(offset, block, n);
        TIKU_P4_IRQ_RESTORE(irq);
        if (rc) return -1;
        memcpy(out, block, n);
        offset += n;
        out += n;
        size -= n;
    }
    return 0;
}

void tiku_p4x_boot_i2c_pads(void)
{
    const unsigned pads[] = {TIKU_BOARD_I2C_SDA_PIN, TIKU_BOARD_I2C_SCL_PIN};
    for (unsigned i = 0; i < 2; ++i) {
        const unsigned pin = pads[i];
        const uintptr_t mux = 0x500e1004u + 4u * pin;
        /* USB download leaves these pads on the SPI2 slave function. */
        if (((TIKU_P4_REG_READ(mux) >> 12) & 7u) == 3u) {
            TIKU_P4_REG_WRITE(0x500e0028u, 1u << pin);
            TIKU_P4_REG_WRITE(mux, (TIKU_P4_REG_READ(mux) & ~(7u << 12)) |
                                   (1u << 12));
        }
    }
}
#endif
