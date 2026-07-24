/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_arch.c - RP2350 worker-thread switcher shim
 *
 * RP2350's Cortex-M33 (ARMv8-M) uses the identical generic Cortex-M
 * switcher as the Apollo parts; only the PendSV symbol differs.  This
 * build's vector table (tiku_crt_early.c) names slot 14 as the weak
 * alias tiku_rp2350_pendsv_handler, so naming the strong handler the
 * same and including the shared body overrides that vector entry.
 *
 * Notes for this core:
 *   - FPU is fpv5-sp-d16; the shared arch_boot() enables CPACR CP10/CP11
 *     (the RP2350 crt does not), so FP workers and the S16-S31 save are
 *     valid.  {s16-s31} is the callee-saved single-precision set here.
 *   - Threads run on core 0 only; SMP is a later roadmap step and will
 *     reuse this switcher (a worker that never yields the other core).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define TIKU_THREAD_ARCH_PENDSV  tiku_rp2350_pendsv_handler
#include "kernel/threads/tiku_thread_cortexm.inl"
