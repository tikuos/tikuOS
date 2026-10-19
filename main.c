/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
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
 * Turns the watchdog off and boots; then, each where built, the power autorun
 * or turbo benchmark (neither returns), shell, embedded BASIC and init load;
 * then the VFS tree, the drivers, tests or kit examples, and the scheduler.
 *
 * @return 0, reached only after tiku_sched_stop() ends the scheduler loop
 */
int main(void) {
  int ret;

  /* The watchdog goes off before any other init: on MSP430 it runs from
   * reset. */
  tiku_watchdog_off();

  /* The boot sequence (boot/tiku_boot.c): CPU and its rate; memory, then
   * the saved CPU rate; the console and the system clock; then the
   * scheduler, which starts processes and both timer services. */
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
  /* Frequency-scaling benchmark firmware (TikuBench turbo_bench.c): runs
   * TikuKits workloads after tiku_cpu_freq_init(96) and again after
   * tiku_cpu_freq_init(192), which on Ambiq select low-power mode and turbo,
   * prints serial markers the host times, then halts here. */
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
  /* Loads the init entries without running them.  The shell process runs
   * them in its first pass (tiku_shell.c), once its command table and
   * console backend exist and after the VFS tree and the drivers below. */
  tiku_nvm_map_init();
  tiku_init_load();
#endif

  /* /proc is assembled here, once: a process registered after this point
   * has no /proc/<pid> directory. */
  ret = tiku_boot_vfs_init();
  if (ret != 0) {
    MAIN_PRINTF("ERROR: VFS assembly failed (%d)\n", ret);
    while (1) { /* halt */ }
  }

  /* Publishes /sys/drivers and runs init() of each driver in the table;
   * with HAS_DRIVERS=0 the table is empty.  See drivers.md. */
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

  /* The scheduler loop dispatches events and runs the processes. */
  MAIN_PRINTF("Entering scheduler\n");
  tiku_sched_loop();

  /* Reached only after tiku_sched_stop(). */
  return 0;
}
