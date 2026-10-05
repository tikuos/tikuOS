/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_arch.c - Ambiq worker-thread switcher shim.
 *
 * Apollo510 and Apollo4 Lite/Plus share the generic Cortex-M switcher.  Their
 * vector tables name PendSV (slot 14) as the weak tiku_ambiq_pendsv_handler;
 * including the shared body here defines that handler strongly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define TIKU_THREAD_ARCH_PENDSV  tiku_ambiq_pendsv_handler
#include "kernel/threads/tiku_thread_cortexm.inl"
