/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_arch.c - RP2350 worker-thread switcher shim.
 *
 * The Cortex-M33 uses the same generic switcher as the Apollo parts; only the
 * PendSV symbol differs, so naming the strong handler after the vector's weak
 * alias and including the shared body overrides that entry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define TIKU_THREAD_ARCH_PENDSV  tiku_rp2350_pendsv_handler
#include "kernel/threads/tiku_thread_cortexm.inl"
