/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_io_arch.h - C6 peripheral register, IRQ and time access.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_IO_ARCH_H
#define TIKU_C6_IO_ARCH_H
#include "tiku_native_arch.h"
#ifndef TIKU_C6_REG_READ
#define TIKU_C6_REG_READ(a) C6_REG(a)
#define TIKU_C6_REG_WRITE(a, v) (C6_REG(a) = (v))
#endif
#ifndef TIKU_C6_IRQ_SAVE
#define TIKU_C6_IRQ_SAVE() native_irq_save()
#define TIKU_C6_IRQ_RESTORE(s) native_irq_restore(s)
#endif
#ifndef TIKU_C6_TIMER_READ
/** @brief Snapshot SYSTIMER and refuse a hardware snapshot timeout. */
static inline int tiku_c6_systimer_read(uint64_t *value)
{
    uint32_t before = native_errors;
    *value = native_time();
    return before == native_errors ? 0 : -1;
}
#else
#define tiku_c6_systimer_read(p) TIKU_C6_TIMER_READ(p)
#endif
#endif
