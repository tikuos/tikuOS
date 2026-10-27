/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_native_arch.h - Register addresses and RAM kernel contracts for ESP32-C6.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_NATIVE_H
#define TIKU_C6_NATIVE_H
#include <stdint.h>
#include "tiku_layout_arch.h"
#ifndef C6_REG
#define C6_REG(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#endif
#define NATIVE_STAGE(n) (C6_REG(0x6001806cu)=0xc6000000u|(n))
#define C6_SYSTIMER 0x6000a000u
#define C6_INTMTX 0x60010000u
#define C6_PLIC 0x20001000u
#define C6_CPU_IRQ 17u
#define C6_IRQ_SOURCE 58u
#define C6_MAP_COUNT 77u
#define C6_TICK_COUNTS 125000u
#define C6_REPORT_WORDS 64u
extern volatile uint32_t native_ticks,native_irqs,native_errors;
extern uint32_t *native_report;
extern unsigned char __native_image_start[],__native_image_end[],__bss_start[],__bss_end[];
extern unsigned char __native_stack_top[],__native_stack_bottom[],__native_irq_bottom[],__native_irq_top[];
extern unsigned char native_probe_illegal[],native_probe_after_illegal[];
extern unsigned char native_stack_guard[];
/** @brief Read the running 16 MHz SYSTIMER unit0; record a bounded snapshot timeout. */
uint64_t native_time(void);
/** @brief Read the low 32 bits of the C6 CPU cycle counter. */
uint32_t native_cycles(void);
/** @brief Clear global MIE and return the previous mstatus value. */
uint32_t native_irq_save(void);
/** @brief Set global MIE when saved mstatus had MIE set. */
void native_irq_restore(uint32_t state);
/** @brief Borrow idle SYSTIMER target1 and CPU line17 with global MIE clear; return -1 if busy. */
int native_hardware_start(void);
/** @brief Restore borrowed timer, interrupt routing and CPU mie with global MIE clear. */
void native_hardware_restore(void);
/** @brief Handle a 36-word machine trap frame on the IRQ stack; return its address. */
uint32_t *native_trap_dispatch(uint32_t *frame);
/** @brief Fill a 64-word report on the native stack and return 0x43364b52 when checks pass. */
uint32_t tiku_c6_qualify(uint32_t *report);
/** @brief Start the resident native kernel and service IPC until reset. */
uint32_t native_main(uint32_t *report);
#if TIKU_SHELL_ENABLE
/** @brief Pump remote-console control after a flash step restores cache and IRQs.
 * @note Foreground only. Non-console diagnostics receive busy during a flash
 * step; an active diagnostic callback is never entered recursively.
 */
void tiku_c6_control_progress(void);
#endif
/** @brief Check caller and callee registers across IRQs for cycles CPU cycles; return 0 on success. */
int native_irq_frame_probe(uint32_t cycles);
/** @brief Execute the deliberate illegal instruction handled by the native trap vector. */
void native_illegal_probe(void);
#endif
