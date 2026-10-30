/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_irq_arch.c - C6 peripheral PLIC ownership and trap dispatch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_irq_arch.h"
#include <stddef.h>
#ifndef TIKU_C6_MIE_SET
#define TIKU_C6_MIE_SET(v) __asm__ volatile("csrs mie,%0" ::"r"(v) : "memory")
#define TIKU_C6_MIE_CLEAR(v) __asm__ volatile("csrc mie,%0" ::"r"(v) : "memory")
#endif
#ifndef TIKU_C6_MIE_READ
/** @brief Read the machine interrupt enables without changing them. */
static uint32_t mie_read(void)
{
    uint32_t value;
    __asm__ volatile("csrr %0,mie" : "=r"(value));
    return value;
}
#define TIKU_C6_MIE_READ() mie_read()
#endif
static struct {
    tiku_c6_isr_t handler;
    uint32_t priority, type;
    unsigned source, previous_route;
} slots[TIKU_C6_IRQ_LAST - 19u + 1u];
int tiku_c6_irq_attach(unsigned line, unsigned source, unsigned priority, tiku_c6_isr_t handler)
{
    if (line < 19 || line > TIKU_C6_IRQ_LAST || source >= C6_MAP_COUNT || priority < 1 || priority > 7 ||
        !handler)
        return -1;
    uint32_t state = TIKU_C6_IRQ_SAVE(), bit = 1u << line;
    unsigned slot = line - 19;
    unsigned route = TIKU_C6_REG_READ(C6_INTMTX + 4 * source);
    if (slots[slot].handler || (TIKU_C6_REG_READ(C6_PLIC) & bit) || (TIKU_C6_MIE_READ() & bit) ||
        route >= 32 ||
        (route && ((TIKU_C6_REG_READ(C6_PLIC) | TIKU_C6_MIE_READ()) & (1u << route))) ||
        (route >= 19 && route <= TIKU_C6_IRQ_LAST && slots[route - 19].handler)) {
        TIKU_C6_IRQ_RESTORE(state);
        return -1;
    }
    for (unsigned i = 0; i < C6_MAP_COUNT; i++)
        if (TIKU_C6_REG_READ(C6_INTMTX + 4 * i) == line) {
            TIKU_C6_IRQ_RESTORE(state);
            return -1;
        }
    slots[slot].priority = TIKU_C6_REG_READ(C6_PLIC + 0x10 + 4 * line);
    slots[slot].type = TIKU_C6_REG_READ(C6_PLIC + 4) & bit;
    slots[slot].source = source;
    slots[slot].previous_route = route;
    slots[slot].handler = handler;
    TIKU_C6_REG_WRITE(C6_PLIC + 4, TIKU_C6_REG_READ(C6_PLIC + 4) & ~bit);
    TIKU_C6_REG_WRITE(C6_PLIC + 0x10 + 4 * line, priority);
    TIKU_C6_REG_WRITE(C6_INTMTX + 4 * source, line);
    TIKU_C6_IRQ_RESTORE(state);
    return 0;
}
int tiku_c6_irq_owned(unsigned line, unsigned source, tiku_c6_isr_t handler)
{
    return line >= 19 && line <= TIKU_C6_IRQ_LAST && slots[line - 19].handler == handler && handler &&
           slots[line - 19].source == source && TIKU_C6_REG_READ(C6_INTMTX + 4 * source) == line;
}
int tiku_c6_irq_enable(unsigned line, int enabled)
{
    if (line < 19 || line > TIKU_C6_IRQ_LAST || !slots[line - 19].handler)
        return -1;
    uint32_t state = TIKU_C6_IRQ_SAVE(), bit = 1u << line;
    uint32_t value = TIKU_C6_REG_READ(C6_PLIC);
    TIKU_C6_REG_WRITE(C6_PLIC, enabled ? value | bit : value & ~bit);
    if (enabled)
        TIKU_C6_MIE_SET(bit);
    else
        TIKU_C6_MIE_CLEAR(bit);
    TIKU_C6_IRQ_RESTORE(state);
    return 0;
}
void tiku_c6_irq_detach(unsigned line)
{
    if (line < 19 || line > TIKU_C6_IRQ_LAST || !slots[line - 19].handler)
        return;
    uint32_t state = TIKU_C6_IRQ_SAVE();
    unsigned slot = line - 19;
    (void)tiku_c6_irq_enable(line, 0);
    TIKU_C6_REG_WRITE(C6_INTMTX + 4 * slots[slot].source, slots[slot].previous_route);
    TIKU_C6_REG_WRITE(C6_PLIC + 0x10 + 4 * line, slots[slot].priority);
    TIKU_C6_REG_WRITE(C6_PLIC + 4,
                      (TIKU_C6_REG_READ(C6_PLIC + 4) & ~(1u << line)) | slots[slot].type);
    slots[slot].handler = NULL;
    TIKU_C6_IRQ_RESTORE(state);
}
int tiku_c6_irq_trap(uint32_t cause)
{
    if (cause < ((1u << 31) | 19u) || cause > ((1u << 31) | TIKU_C6_IRQ_LAST))
        return 0;
    unsigned slot = (cause & 31u) - 19;
    if (!slots[slot].handler)
        return 0;
    slots[slot].handler();
    return 1;
}
