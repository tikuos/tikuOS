/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_irq_arch.h - C5 machine interrupt state and non-nested CLIC dispatch.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_IRQ_ARCH_H_
#define TIKU_ESP32C5_IRQ_ARCH_H_

#define TIKU_C5_FRAME_SIZE   144
#define TIKU_C5_FRAME_PC     128
#define TIKU_C5_FRAME_STATUS 132
#define TIKU_C5_FRAME_CAUSE  136

#ifndef __ASSEMBLER__
#include <stdint.h>

typedef void (*tiku_c5_isr_t)(void);
typedef uint32_t *(*tiku_c5_switch_t)(uint32_t *frame);

/** @brief Save mstatus and mask interrupts; nested calls require LIFO restores.
 */
#ifndef TIKU_C5_IRQ_SAVE
static inline uint32_t tiku_c5_irq_save(void)
{
    uint32_t state;
    __asm__ volatile("csrrci %0, mstatus, 8" : "=r"(state)::"memory");
    return state;
}
#define TIKU_C5_IRQ_SAVE() tiku_c5_irq_save()
#endif

/** @brief Restore the saved MIE bit without modifying other mstatus bits. */
#ifndef TIKU_C5_IRQ_RESTORE
static inline void tiku_c5_irq_restore(uint32_t state)
{
    if (state & 8u) {
        __asm__ volatile("csrsi mstatus, 8" ::: "memory");
    } else {
        __asm__ volatile("csrci mstatus, 8" ::: "memory");
    }
}
#define TIKU_C5_IRQ_RESTORE(state) tiku_c5_irq_restore(state)
#endif

/** @brief Disable and unroute CLIC sources; leave machine interrupts masked. */
void tiku_c5_irq_init(void);
/** @brief Claim a disabled, level-triggered line; return -1 on invalid or busy
 * input. */
int tiku_c5_irq_attach(unsigned line, unsigned source, unsigned priority,
                       tiku_c5_isr_t handler);
/** @brief Enable an attached line, or disable a line; return -1 on invalid
 * input. */
int tiku_c5_irq_enable(unsigned line, int enabled);
/** @brief Return nonzero when a line still has the specified source and
 * handler. */
int tiku_c5_irq_owned(unsigned line, unsigned source, tiku_c5_isr_t handler);
/** @brief Disable and unroute a line, then release its handler and source. */
void tiku_c5_irq_detach(unsigned line);
/** @brief Dispatch an external CLIC ID or disable an unclaimed ID. */
void tiku_c5_irq_dispatch(uint32_t id);
/** @brief Return the number of interrupts received without a handler. */
uint32_t tiku_c5_irq_unclaimed(void);
/** @brief Whether the CPU is currently dispatching an interrupt handler. */
int tiku_c5_in_isr(void);
/** @brief Claim a frame-switching line at priority 1; return -1 if occupied. */
int tiku_c5_irq_attach_switch(unsigned line, unsigned source,
                              tiku_c5_switch_t handler);
/** @brief Dispatch an IRQ and return the context frame selected for resumption.
 */
uint32_t *tiku_c5_irq_dispatch_frame(uint32_t id, uint32_t *frame);

#endif
#endif
