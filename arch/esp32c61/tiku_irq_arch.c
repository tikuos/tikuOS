/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_irq_arch.c - ESP32-C61 interrupt lines over the matrix and the CLIC.
 *
 * Every line is non-vectored, so each interrupt enters at the trap entry and
 * reaches its handler through tiku_esp32c61_irq_dispatch().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>

#include "tiku_irq_arch.h"
#include "tiku_esp32c61_regs.h"

static tiku_esp32c61_isr_t irq_isr[ESP32C61_CLIC_LINES];
static volatile uint32_t   irq_on;
static volatile uint32_t   irq_spurious;

/** @brief One line's IE bit, then a read back so the write has landed. */
static void line_ie(unsigned line, int on) {
    uint32_t a = ESP32C61_CLIC_CTRL(ESP32C61_CLIC_LINE_ID(line));

    if (on) {
        TIKU_REG32(a) |= ESP32C61_CLIC_IE;
    } else {
        TIKU_REG32(a) &= ~ESP32C61_CLIC_IE;
    }
    (void)TIKU_REG32(a);
}

void tiku_esp32c61_irq_init(void) {
    uint32_t cfg;

    (void)tiku_esp32c61_mie_off();
    for (unsigned i = 0U; i < ESP32C61_CLIC_LINES; i++) {
        line_ie(i, 0);
        irq_isr[i] = NULL;
    }
    irq_on = 0UL;
    /* Whatever the ROM routed goes: a source raises nothing until attached. */
    for (unsigned s = 0U; s < ESP32C61_INTMTX_SOURCES; s++) {
        TIKU_REG32(ESP32C61_INTMTX_MAP(s)) = 0UL;
    }
    /* Level bits reset to none, which flattens every line to one level. */
    cfg = TIKU_REG32(ESP32C61_CLIC_CONFIG) & ~ESP32C61_CLIC_MNLBITS_MSK;
    TIKU_REG32(ESP32C61_CLIC_CONFIG) = cfg | ESP32C61_CLIC_NLBITS;
    ESP32C61_CSR_WRITE(ESP32C61_CSR_MINTTHRESH, 0UL);  /* every level */
}

void tiku_esp32c61_irq_attach(unsigned line, unsigned source,
                              unsigned level, tiku_esp32c61_isr_t isr) {
    uint32_t id, ctrl, ctl;

    if (line >= ESP32C61_CLIC_LINES || source >= ESP32C61_INTMTX_SOURCES) {
        return;
    }
    tiku_esp32c61_irq_disable(line);
    irq_isr[line] = isr;

    /* The level sits in the top NLBITS of the control byte, ones below. */
    level = (level == 0U) ? 1U : (level > 7U ? 7U : level);
    ctl = (level << (8U - ESP32C61_CLIC_NLBITS)) |
          ((1UL << (8U - ESP32C61_CLIC_NLBITS)) - 1UL);
    id = ESP32C61_CLIC_LINE_ID(line);
    ctrl = TIKU_REG32(ESP32C61_CLIC_CTRL(id));
    ctrl &= ~(ESP32C61_CLIC_CTL_MSK | ESP32C61_CLIC_SHV |
              ESP32C61_CLIC_TRIG_EDGE | ESP32C61_CLIC_TRIG_LOW |
              ESP32C61_CLIC_IE);
    ctrl |= ESP32C61_CLIC_MODE_M | (ctl << ESP32C61_CLIC_CTL_POS);
    TIKU_REG32(ESP32C61_CLIC_CTRL(id)) = ctrl;
    TIKU_REG32(ESP32C61_INTMTX_MAP(source)) = id;
}

void tiku_esp32c61_irq_enable(unsigned line) {
    uint32_t s;

    if (line >= ESP32C61_CLIC_LINES) {
        return;
    }
    s = tiku_esp32c61_mie_off();
    irq_on |= 1UL << line;
    line_ie(line, 1);
    tiku_esp32c61_mie_restore(s);
}

void tiku_esp32c61_irq_disable(unsigned line) {
    uint32_t s;

    if (line >= ESP32C61_CLIC_LINES) {
        return;
    }
    s = tiku_esp32c61_mie_off();
    irq_on &= ~(1UL << line);
    line_ie(line, 0);
    tiku_esp32c61_mie_restore(s);
}

uint32_t tiku_esp32c61_irq_enabled(void) {
    return irq_on;
}

void tiku_esp32c61_irq_set_enabled(uint32_t lines) {
    uint32_t s = tiku_esp32c61_mie_off();
    uint32_t change = irq_on ^ lines;

    for (unsigned i = 0U; change != 0UL; i++, change >>= 1) {
        if (change & 1UL) {
            line_ie(i, (int)((lines >> i) & 1UL));
        }
    }
    irq_on = lines;
    tiku_esp32c61_mie_restore(s);
}

uint32_t tiku_esp32c61_irq_spurious(void) {
    return irq_spurious;
}

/**
 * @brief Every interrupt arrives here from the trap path.
 *
 * @param id     The CLIC id from mcause: line + 16
 * @param frame  The saved registers, unused by the lines
 */
void tiku_esp32c61_irq_dispatch(uint32_t id, uint32_t *frame) {
    uint32_t line = id - ESP32C61_CLIC_LINE_ID(0U);

    (void)frame;
    if (line < ESP32C61_CLIC_LINES && irq_isr[line] != NULL) {
        irq_isr[line]();
        return;
    }
    /* Unclaimed: quiet the id, or a level source takes the core forever. */
    irq_spurious++;
    if (line < ESP32C61_CLIC_LINES) {
        irq_on &= ~(1UL << line);
    }
    if (id < ESP32C61_CLIC_LINE_ID(ESP32C61_CLIC_LINES)) {
        TIKU_REG32(ESP32C61_CLIC_CTRL(id)) &= ~ESP32C61_CLIC_IE;
    }
}
