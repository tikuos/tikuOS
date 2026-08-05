/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_arch.c - RA8P1 worker-thread switcher shim.
 *
 * The Cortex-M85 uses the generic switcher; naming this part's PendSV vector
 * symbol is the whole port, and the DWT supplies the cycle counter.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define TIKU_THREAD_ARCH_PENDSV  tiku_ra8p1_pendsv_handler
#include "kernel/threads/tiku_thread_cortexm.inl"
