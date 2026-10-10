/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_irq_arch.c - C5 interrupt matrix and machine-mode CLIC lines.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_irq_arch.h"
#include "tiku_esp32c5_regs.h"

static tiku_c5_isr_t handlers[TIKU_C5_IRQ_LINES];
static uint8_t sources[TIKU_C5_IRQ_LINES];
static volatile uint32_t unclaimed;
static volatile uint8_t in_isr;
static tiku_c5_switch_t frame_switch;
static unsigned switch_line = TIKU_C5_IRQ_LINES;

/** @brief Change a line's enable bit and complete the peripheral write. */
static void line_enable(unsigned id, int enabled)
{
    uint32_t value = TIKU_C5_REG_READ(TIKU_C5_CLIC_CTRL(id));
    value = enabled ? value | TIKU_C5_CLIC_IE : value & ~TIKU_C5_CLIC_IE;
    TIKU_C5_REG_WRITE(TIKU_C5_CLIC_CTRL(id), value);
    (void)TIKU_C5_REG_READ(TIKU_C5_CLIC_CTRL(id));
}

void tiku_c5_irq_init(void)
{
    unsigned i;
    uint32_t config;

    (void)TIKU_C5_IRQ_SAVE();
    for (i = 0; i < TIKU_C5_IRQ_LINES + 16u; i++) {
        line_enable(i, 0);
    }
    for (i = 0; i < TIKU_C5_IRQ_SOURCES; i++) {
        TIKU_C5_REG_WRITE(TIKU_C5_IRQ_MAP(i), 0);
    }
    for (i = 0; i < TIKU_C5_IRQ_LINES; i++) {
        handlers[i] = NULL;
        sources[i] = TIKU_C5_IRQ_SOURCES;
    }
    config = TIKU_C5_REG_READ(TIKU_C5_CLIC_CONFIG);
    TIKU_C5_REG_WRITE(TIKU_C5_CLIC_CONFIG, (config & ~15u) | 3u);
    TIKU_C5_SET_THRESHOLD(0);
    unclaimed = 0;
    in_isr = 0;
    frame_switch = NULL;
    switch_line = TIKU_C5_IRQ_LINES;
}

int tiku_c5_irq_attach(unsigned line, unsigned source, unsigned priority,
                       tiku_c5_isr_t handler)
{
    uint32_t state, control;
    unsigned i;

    if (line >= TIKU_C5_IRQ_LINES || source >= TIKU_C5_IRQ_SOURCES ||
        priority == 0 || priority > 7 || handler == NULL) {
        return -1;
    }
    state = TIKU_C5_IRQ_SAVE();
    for (i = 0; i < TIKU_C5_IRQ_LINES; i++) {
        if (handlers[i] != NULL && (i == line || sources[i] == source)) {
            TIKU_C5_IRQ_RESTORE(state);
            return -1;
        }
    }
    line_enable(line + 16u, 0);
    handlers[line] = handler;
    sources[line] = (uint8_t)source;
    control = ((priority << 5) | 31u) << 24;
    control |= 3u << 22;
    TIKU_C5_REG_WRITE(TIKU_C5_CLIC_CTRL(line + 16u), control);
    TIKU_C5_REG_WRITE(TIKU_C5_IRQ_MAP(source), line + 16u);
    TIKU_C5_IRQ_RESTORE(state);
    return 0;
}

int tiku_c5_irq_owned(unsigned line, unsigned source, tiku_c5_isr_t handler)
{
    return line < TIKU_C5_IRQ_LINES && handler != NULL &&
           sources[line] == source && handlers[line] == handler;
}

int tiku_c5_irq_enable(unsigned line, int enabled)
{
    uint32_t state;

    if (line >= TIKU_C5_IRQ_LINES) {
        return -1;
    }
    state = TIKU_C5_IRQ_SAVE();
    if (enabled && handlers[line] == NULL) {
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    line_enable(line + 16u, enabled);
    TIKU_C5_IRQ_RESTORE(state);
    return 0;
}

void tiku_c5_irq_detach(unsigned line)
{
    uint32_t state;

    if (line >= TIKU_C5_IRQ_LINES) {
        return;
    }
    state = TIKU_C5_IRQ_SAVE();
    line_enable(line + 16u, 0);
    if (sources[line] < TIKU_C5_IRQ_SOURCES) {
        TIKU_C5_REG_WRITE(TIKU_C5_IRQ_MAP(sources[line]), 0);
    }
    handlers[line] = NULL;
    sources[line] = TIKU_C5_IRQ_SOURCES;
    if (line == switch_line) {
        frame_switch = NULL;
        switch_line = TIKU_C5_IRQ_LINES;
    }
    TIKU_C5_IRQ_RESTORE(state);
}

void tiku_c5_irq_dispatch(uint32_t id)
{
    unsigned line = id - 16u;
    uint8_t previous = in_isr;
    in_isr = 1;

    if (line < TIKU_C5_IRQ_LINES && handlers[line] != NULL) {
        handlers[line]();
    } else {
        unclaimed++;
        if (id < TIKU_C5_IRQ_LINES + 16u) {
            line_enable(id, 0);
        }
    }
    in_isr = previous;
}

int tiku_c5_in_isr(void)
{
    return in_isr;
}

uint32_t tiku_c5_irq_unclaimed(void)
{
    return unclaimed;
}

/** @brief Reserve the ordinary handler slot for a frame-switching interrupt. */
static void switch_claim(void)
{
}

int tiku_c5_irq_attach_switch(unsigned line, unsigned source,
                              tiku_c5_switch_t handler)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    int result = -1;
    if (handler != NULL && frame_switch == NULL &&
        tiku_c5_irq_attach(line, source, 1, switch_claim) == 0) {
        switch_line = line;
        frame_switch = handler;
        result = 0;
    }
    TIKU_C5_IRQ_RESTORE(state);
    return result;
}

uint32_t *tiku_c5_irq_dispatch_frame(uint32_t id, uint32_t *frame)
{
    if (frame_switch != NULL && id == switch_line + 16u) {
        uint8_t previous = in_isr;
        uint32_t *next;
        in_isr = 1;
        next = frame_switch(frame);
        in_isr = previous;
        return next;
    }
    tiku_c5_irq_dispatch(id);
    return frame;
}
