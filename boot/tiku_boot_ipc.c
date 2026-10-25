/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot_ipc.c - default boot endpoint table for local process exchange.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel/ipc/tiku_ipc_backend.h>
#include <hal/tiku_compiler.h>
#include "tiku_boot_ipc.h"
#include <kernel/vfs/tree/tiku_vfs_tree_ipc.h>
#if defined(PLATFORM_ESP32P4) && defined(TIKU_P4_FIRMWARE)
#include <kernel/memory/tiku_mem.h>
#include <string.h>
#define BOOT_STORAGE __attribute__((section(".p4.ipc"),aligned(64)))
extern uint8_t __ipc_device_start[],__ipc_device_end[];
#else
#define BOOT_STORAGE
#endif
#if (TIKU_HAS_SDIO_C6 + 0)
#include "tiku_boot_c6.h"
#include <arch/esp32p4/tiku_sdmmc_arch.h>
#endif
#if (TIKU_HAS_COPROC + 0)
#include <services/ipc/tiku_ipc_coproc.h>
#include <string.h>
#if defined(PLATFORM_NORDIC)
static const tiku_ipc_backend_ops_t coproc_ops = {0};
#endif
#endif
#if (TIKU_IPC_CONSOLE_ENABLE + 0)
#include <services/ipc/tiku_ipc_wire.h>
#include <services/link/tiku_link_console.h>
#include <hal/tiku_ipc_epoch_hal.h>
static tiku_ipc_wire_t console_wire BOOT_STORAGE;
static tiku_link_console_t console_link BOOT_STORAGE;
static uint8_t console_frame[TIKU_IPC_WIRE_FRAME + TIKU_LINK_CONSOLE_OVERHEAD] BOOT_STORAGE;
#endif
#if (TIKU_IPC_HAS_NPU + 0)
#include <services/ipc/tiku_ipc_npu.h>
static tiku_ipc_npu_context_t npu = { .ops = &tiku_npu_engine_ops };
#endif
#if (TIKU_IPC_HAS_SHARED + 0)
#include <services/ipc/tiku_ipc_shared.h>
static tiku_ipc_shared_context_t shared;
#endif

#if TIKU_IPC_ENABLE
TIKU_WEAK const tiku_ipc_endpoint_config_t tiku_ipc_backend_table[] = {
    {"local0", NULL, NULL, 1},
    {"local1", NULL, NULL, 0},
#if (TIKU_HAS_SDIO_C6 + 0)
    {"c6/bulk", &tiku_boot_c6_ops, NULL, 0},
#endif
#if defined(PLATFORM_NORDIC) && (TIKU_HAS_COPROC + 0)
    {"flpr", &coproc_ops, NULL, 0},
#endif
#if (TIKU_IPC_HAS_NPU + 0)
    {"npu/inference", &tiku_ipc_npu_ops, &npu, 0},
#endif
#if (TIKU_IPC_HAS_SHARED + 0)
#if defined(PLATFORM_ESP32P4)
    {"lp", &tiku_ipc_shared_ops, &shared, 0},
#else
    {"cpu1", &tiku_ipc_shared_ops, &shared, 0},
#endif
#endif
#if (TIKU_IPC_CONSOLE_ENABLE + 0)
    {"console/ipc", &tiku_ipc_wire_ops, &console_wire, 0},
#endif
};
TIKU_WEAK const uint16_t tiku_ipc_backend_count =
    sizeof tiku_ipc_backend_table / sizeof tiku_ipc_backend_table[0];

int tiku_boot_ipc_init(void)
{
#if defined(PLATFORM_ESP32P4) && defined(TIKU_P4_FIRMWARE)
    {
        size_t length=(size_t)(__ipc_device_end-__ipc_device_start);
#if (TIKU_HAS_SDIO_C6 + 0)
        if(tiku_p4_sdmmc_boot_quiesce()!=TIKU_IPC_OK)return -1;
#endif
        if(length){
            if(tiku_region_claim(__ipc_device_start,length,0xe1)!=TIKU_MEM_OK)return -1;
            memset(__ipc_device_start,0,length);
        }
    }
#endif
#if (TIKU_IPC_HAS_SHARED + 0)
    if (tiku_ipc_shared_init(&shared, tiku_ipc_hal_shared(), 0xe0) != TIKU_IPC_OK)
        return -1;
#endif
    if (tiku_ipc_init(tiku_ipc_backend_table, tiku_ipc_backend_count) != TIKU_IPC_OK)
        return -1;
#if (TIKU_HAS_SDIO_C6 + 0)
    for(uint16_t e=0;e<tiku_ipc_backend_count;e++)
        if(tiku_ipc_backend_table[e].ops==&tiku_boot_c6_ops&&tiku_boot_c6_init(e))return -1;
#endif
#if (TIKU_HAS_COPROC + 0)
    {
        uint16_t e;
#if defined(PLATFORM_ESP32P4)
        const char *name = "lp";
#elif defined(PLATFORM_RA8P1)
        const char *name = "cpu1";
#else
        const char *name = "flpr";
#endif
        for (e = 0; e < tiku_ipc_backend_count; e++)
            if (!strcmp(tiku_ipc_backend_table[e].name,name)) break;
        if (tiku_ipc_coproc_init(e) != TIKU_IPC_OK) return -1;
    }
#endif
#if (TIKU_IPC_CONSOLE_ENABLE + 0)
    {
        uint64_t epoch;
        uint16_t e;
        tiku_link_t *link;
        if (tiku_ipc_epoch_fresh(&epoch) != TIKU_IPC_OK) return -1;
        link = tiku_link_console_open(&console_link,0xf4,console_frame,sizeof console_frame);
        if (!link) return -1;
        for (e = 0; e < tiku_ipc_backend_count; e++)
            if (tiku_ipc_backend_table[e].context == &console_wire) break;
        if (tiku_ipc_wire_init(&console_wire,link,"console/ipc",e,epoch,
                              TIKU_IPC_WIRE_FRAME,NULL) != TIKU_IPC_OK) return -1;
    }
#endif
    return tiku_vfs_tree_ipc_init();
}
#endif
