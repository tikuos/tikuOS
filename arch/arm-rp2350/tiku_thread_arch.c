/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_arch.c - RP2350 worker-thread switcher shim.
 *
 * Includes the shared Cortex-M switcher with its PendSV handler named
 * tiku_rp2350_pendsv_handler, the strong definition of the weak alias in the
 * vector table.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define TIKU_THREAD_ARCH_PENDSV  tiku_rp2350_pendsv_handler
#include "kernel/threads/tiku_thread_cortexm.inl"
