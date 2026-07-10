/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_arch.c - Ambiq worker-thread switcher shim
 *
 * The Apollo parts -- Apollo510 (Cortex-M55) and Apollo4 Lite/Plus
 * (Cortex-M4F) -- share the one generic Cortex-M switcher.  Their
 * crt-early vector tables all name PendSV slot 14 as the weak alias
 * tiku_ambiq_pendsv_handler (see tiku_crt_early*.c), so naming the
 * strong handler the same and pulling in the shared body is the whole
 * port: the strong definition overrides the weak vector alias.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define TIKU_THREAD_ARCH_PENDSV  tiku_ambiq_pendsv_handler
#include "kernel/threads/tiku_thread_cortexm.inl"
