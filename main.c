/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * main.c - Main application entry point
 *
 * System initialization and main event loop for the Tiku Operating System.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"
#include "boot/tiku_boot.h"
#include "kernel/scheduler/tiku_sched.h"

#if TEST_ENABLE
#include "tests/test_runner.h"
#endif

#if defined(HAS_TIKUKITS) && defined(HAS_EXAMPLES)
#include "examples/kits/example_kits_runner.h"
#endif

#include "boot/tiku_boot_vfs.h"

#if TIKU_SHELL_ENABLE
#include "shell/tiku_shell.h"
#endif

#if TIKU_INIT_ENABLE
#include "kernel/memory/tiku_nvm_map.h"
#include "services/init/tiku_init.h"
#endif

#ifdef TIKU_BASIC_EMBEDDED
#include "basic/tiku_basic.h"
extern const char tiku_basic_embedded_src[];
#endif

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Main application entry point
 *
 * Boots the system, starts the shell and loads the init entries when they are
 * built, builds the VFS tree, starts the drivers, runs the test suite when
 * TEST_ENABLE is set, then enters the scheduler loop.
 *
 * @return 0, reached only after tiku_sched_stop() ends the scheduler loop
 */
int main(void) {
  int ret;

  /* Step 1: Disable watchdog immediately (before any other init) */
  tiku_watchdog_off();

  /* Step 2: Full system boot sequence (boot/tiku_boot.c)
   *   - CPU bring-up and frequency configuration
   *   - Memory init, then the saved CPU rate
   *   - UART init (enables printf under GCC; no-op under CCS)
   *   - Clock initialization
   *   - Process subsystem, hardware timer, software timers (via scheduler)
   */
  ret = tiku_cpu_full_init(MAIN_CPU_FREQ);
  if (ret != TIKU_BOOT_SUCCESS) {
    MAIN_PRINTF("ERROR: Boot failed at stage %d\n", ret);
    while (1) { /* halt */ }
  }

  MAIN_PRINTF("TikuOS starting up...\n");

  MAIN_PRINTF("Boot complete\n");

#if defined(TIKU_POWER_AUTORUN) && TIKU_POWER_AUTORUN
  /* Deep-sleep measurement firmware: runs the console-free power staircase
   * (tiku_ambiq_power_autorun) and never reaches the scheduler. */
  {
    extern void tiku_ambiq_power_autorun(void);
    MAIN_PRINTF("POWER AUTORUN: spin3s / idle8s / deep30s / "
                "spin2s / deep20s, repeat.\n");
    MAIN_PRINTF("Unplug J16 (J-Link) and power via the Apollo5 USB connector\n");
    MAIN_PRINTF("for the real deep-sleep measurement; reconnect J16 to flash.\n");
    tiku_ambiq_power_autorun();
  }
#endif

#if TIKU_TURBO_BENCH
  /* Frequency-scaling benchmark firmware: run heavy TikuKits workloads at
   * 96 MHz (LP) and 192 MHz (HP), emit serial markers for host-side timing,
   * then halt (never reaches the shell/scheduler). */
  {
    extern void turbo_bench_run(void);
    turbo_bench_run();
  }
  for (;;) { /* benchmark complete -- halt */ }
#endif

#if TIKU_SHELL_ENABLE
  tiku_shell_init();
#endif

#ifdef TIKU_BASIC_EMBEDDED
  /* Build-time-embedded BASIC program, parsed and run here: after the shell
   * init, before the VFS tree, the drivers and the scheduler.  It returns
   * when the program ends (END, STOP or past its last line); the scheduler
   * then takes over, with a shell prompt when the shell is built. */
  tiku_basic_run_source(tiku_basic_embedded_src);
#endif

#if TIKU_INIT_ENABLE
  /* Load only; the shell process runs the entries on its first schedule
   * (tiku_shell.c).  The parser's command table and the console backend are
   * set up at process start, so an entry dispatched from here hits a NULL
   * table and does nothing.  From the shell the entries also run after the
   * driver registry and the VFS tree, as typed commands do. */
  tiku_nvm_map_init();
  tiku_init_load();
#endif

  /* Initialize the VFS tree.  /proc is assembled here, once: a process
   * registered after this point has no /proc/<pid> directory. */
  ret = tiku_boot_vfs_init();
  if (ret != 0) {
    MAIN_PRINTF("ERROR: VFS assembly failed (%d)\n", ret);
    while (1) { /* halt */ }
  }

  /* Hand off to the driver registry. With HAS_DRIVERS=0 the table
   * is empty and this is a no-op; with a populated drivers/ tree
   * each enabled driver's init() runs here. See drivers.md. */
  {
    extern void tiku_drv_init_all(void);
    tiku_drv_init_all();
  }

#if TEST_ENABLE
  test_run_all();
#endif

#if defined(HAS_TIKUKITS) && defined(HAS_EXAMPLES) && TIKU_EXAMPLES_ENABLE
  example_kits_run();
#endif

#if TIKU_APPS_ENABLE
  MAIN_PRINTF("App mode active\n");
#endif

  /* Step 3: Enter the scheduler loop (dispatches events, runs protothreads) */
  MAIN_PRINTF("Entering scheduler\n");
  tiku_sched_loop();

  /* Reached only after tiku_sched_stop(). */
  return 0;
}
