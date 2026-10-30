/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot_c6_native.c - Native C6 namespace and shell assembly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku.h"
#if defined(PLATFORM_ESP32C6) && TIKU_SHELL_ENABLE
#include "tiku_boot.h"
#include "tiku_boot_vfs.h"
#include <shell/tiku_shell.h>
#include <kernel/cpu/tiku_hang.h>
#include <kernel/cpu/tiku_stack.h>
#include <kernel/timers/tiku_htimer.h>
#include <kernel/vfs/tree/tiku_vfs_tree_ipc.h>
#include <services/console/tiku_console_remote.h>
#if HAS_DRIVERS
#include <kernel/drivers/tiku_drv_registry.h>
#endif
#if TIKU_KIT_NET_ENABLE
#include <kernel/process/tiku_process.h>
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>
#endif
static uint8_t complete;
/** @brief Initialize CPU services, namespace providers and the remote shell.
 * @return Zero after shell startup, or -1 if namespace admission fails. */
int tiku_boot_c6_services(void)
{
    tiku_cpu_boot_init();
    tiku_hang_boot_init();
    tiku_stack_paint();
    tiku_htimer_init();
    if (tiku_vfs_tree_ipc_init())
        return -1;
    if (tiku_boot_vfs_init())
        return -1;
    tiku_console_remote_init(TIKU_C6_BUILD);
#if TIKU_KIT_NET_ENABLE
    if (tiku_process_register("net", &tiku_kits_net_process) < 0)
        return -1;
#endif
#if HAS_DRIVERS
    tiku_drv_init_all();
#endif
    tiku_shell_init();
    complete = 1;
    return 0;
}
tiku_boot_stage_e tiku_boot_get_stage(void)
{
    return complete ? TIKU_BOOT_STAGE_COMPLETE : TIKU_BOOT_STAGE_SERVICES;
}
int tiku_boot_is_complete(void)
{
    return complete;
}

#endif
