/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_s31_ram.c - S31 shell assembly with optional native Wi-Fi services.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tiku.h>
#include <arch/esp32s31/tiku_runtime_arch.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/threads/tiku_thread.h>
#include <shell/tiku_shell.h>
#if TIKU_MEM_NVM_ENABLE
#include "tiku_boot.h"
#include "tiku_boot_vfs.h"
#include <kernel/cpu/tiku_stack.h>
#include <kernel/memory/tiku_nvm_sync.h>
#include <kernel/cpu/tiku_rtc.h>
static unsigned services_ready;
tiku_boot_stage_e tiku_boot_get_stage(void)
{ return services_ready ? TIKU_BOOT_STAGE_COMPLETE : TIKU_BOOT_STAGE_SERVICES; }
int tiku_boot_is_complete(void) { return services_ready; }
#endif
#if TIKU_S31_WIFI_ENABLE
#include <drivers/wifi/esp/tiku_drv_wifi_esp.h>
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>
#include <tikukits/net/ipv4/tiku_kits_net_dhcp.h>
#include <tikukits/net/ipv4/tiku_kits_net_dhcps.h>
#include <tikukits/net/wifi/tiku_kits_net_wifi.h>
#include <interfaces/wireless/tiku_wireless_ap.h>
TIKU_PROCESS(s31_wifi_link, "Wi-Fi IP");
static struct tiku_timer link_timer;
static unsigned link_mode = 3;
/** @brief Switch DHCP roles with the native radio's station and AP state. */
TIKU_PROCESS_THREAD(s31_wifi_link, ev, data)
{
    (void)ev; (void)data;
    TIKU_PROCESS_BEGIN();
    for (;;) {
        tiku_timer_set_event(&link_timer, TIKU_CLOCK_SECOND / 8);
        TIKU_PROCESS_WAIT_EVENT();
        tiku_wireless_status_t station;
        tiku_wireless_ap_status_t ap;
        unsigned mode = 0;
        if (!tiku_wireless_ap_status(&ap) && ap.active) mode = 2;
        else if (!tiku_wireless_status(&station) && station.link_state == TIKU_WIRELESS_LINK_JOINED) mode = 1;
        if (mode != link_mode) {
            tiku_kits_net_dhcps_stop(); tiku_kits_net_wifi_reset();
            (void)tiku_kits_net_wifi_init();
            if (mode == 1 && !tiku_kits_net_dhcp_start(station.mac))
                tiku_process_start(&tiku_kits_net_dhcp_process, NULL);
            link_mode = mode;
        }
        if (mode == 2 && !tiku_kits_net_dhcps_start()) tiku_kits_net_dhcps_poll();
    }
    TIKU_PROCESS_END();
}
#ifdef TIKU_APPLICATION_HEADER
TIKU_PROCESS(s31_wifi_start, "Wi-Fi startup");
static struct tiku_timer startup_timer;
/** @brief Start network services without holding the application/console boot path. */
TIKU_PROCESS_THREAD(s31_wifi_start, ev, data)
{
    (void)ev; (void)data;
    TIKU_PROCESS_BEGIN();
    tiku_timer_set_event(&startup_timer, 1);
    TIKU_PROCESS_WAIT_EVENT();
    if (tiku_drv_wifi_esp.init()) {
        tiku_s31_puts("Wi-Fi unavailable\n");
        TIKU_PROCESS_EXIT();
    }
    tiku_process_start(&tiku_kits_net_process, NULL);
    while (!tiku_kits_net_ipv4_get_link()) {
        tiku_timer_set_event(&startup_timer, 1);
        TIKU_PROCESS_WAIT_EVENT();
    }
    if (tiku_kits_net_wifi_init()) tiku_s31_puts("Wi-Fi IP unavailable\n");
    else tiku_process_start(&s31_wifi_link, NULL);
    TIKU_PROCESS_END();
}
#endif
#endif
#ifdef TIKU_APPLICATION_HEADER
#include TIKU_APPLICATION_HEADER
#endif
#ifdef TIKU_S31_FLASH_BOOT
#include <arch/esp32s31/tiku_image_desc.h>
#endif

/** @brief Start the UART shell and dispatch processes on CPU0. */
int main(void)
{
    tiku_s31_runtime_init();
    tiku_uart_init();
    tiku_clock_arch_delay(150000);
    int memory_result = tiku_s31_memory_init();
    if (memory_result) {
        tiku_s31_report("memory_result", (uint32_t)memory_result);
        tiku_s31_fatal("shell memory init");
    }
#if TIKU_S31_SERVICES_TEST
    extern void tiku_s31_services_test(void);
    tiku_s31_services_test();
#endif
    tiku_clock_init();
    tiku_sched_init();
    tiku_s31_puts("TIKU S31 SRAM shell v1\n");
    tiku_mem_stats_t memory;
    if (tiku_tier_stats(TIKU_MEM_PSRAM, &memory) != TIKU_MEM_OK)
        tiku_s31_fatal("shell PSRAM tier");
    tiku_s31_report("psram_tier_bytes", memory.total_bytes);
#ifdef TIKU_S31_FLASH_BOOT
    tiku_s31_puts("flash_project=");
    tiku_s31_puts(tiku_s31_image_project());
    tiku_s31_puts("\n");
#endif
#if TIKU_MEM_NVM_ENABLE
    tiku_stack_paint();
    tiku_rtc_init();
    if (tiku_boot_vfs_init()) tiku_s31_fatal("namespace init");
    services_ready = 1;
#endif
    tiku_shell_init();
    tiku_watchdog_config(TIKU_WDT_MODE_WATCHDOG, TIKU_WDT_SRC_ACLK,
                         65535u, 0, 1);
    tiku_cpu_irq_enable();
#ifdef TIKU_APPLICATION_HEADER
    tiku_s31_report("application_started", (uint32_t)tiku_application_start());
#endif
#if TIKU_S31_WIFI_ENABLE
#ifdef TIKU_APPLICATION_HEADER
    tiku_process_start(&s31_wifi_start, NULL);
#else
    if (tiku_drv_wifi_esp.init()) tiku_s31_fatal("Wi-Fi init");
    tiku_process_start(&tiku_kits_net_process, NULL);
    while (!tiku_kits_net_ipv4_get_link()) (void)tiku_sched_run_once();
    if (tiku_kits_net_wifi_init()) tiku_s31_fatal("Wi-Fi IP init");
    tiku_process_start(&s31_wifi_link, NULL);
#endif
#endif
    tiku_sched_start(&tiku_shell_process, NULL);
    for (;;) {
        while (tiku_sched_run_once()) { }
        tiku_watchdog_kick();
#if TIKU_MEM_NVM_ENABLE
        tiku_nvm_sync_poll();
#endif
        tiku_atomic_enter();
        if (!tiku_sched_has_pending()) {
            if (tiku_thread_worker_ready()) tiku_thread_kernel_block();
            else __asm__ volatile ("wfi" ::: "memory");
        }
        tiku_atomic_exit();
    }
}
