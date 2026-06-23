/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_data.c - /data VFS nodes (user-data / persisted state)
 *
 * /data is a DYNAMIC directory backed by the Tiku File Store (kernel/fs):
 * arbitrary files can be created, written, read, listed and deleted at run
 * time and are kept in NVM.  So:
 *
 *   write /data/blink.bas "10 LED 0,1 : ..."   # create / overwrite
 *   ls /data                                    # list files
 *   cat /data/blink.bas                         # read
 *
 * The file store sits on a reserved NVM region: `.persistent` FRAM on MSP430
 * (durable now); plain `.bss` elsewhere until the direct-MRAM / Flash backend
 * lands (the backend interface makes that a drop-in swap, so the namespace and
 * shell already work everywhere).  When BASIC is built, the legacy /data/basic
 * bridge to the interpreter's program store is kept as a static child.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_data.h"
#include "tiku.h"

#if defined(TIKU_SHELL_ENABLE)

#include <string.h>

#include "kernel/fs/tiku_tfs.h"
#include <kernel/memory/tiku_mem.h>      /* tiku_mpu_(un)lock_nvm */

#if TIKU_SHELL_CMD_BASIC
#include "kernel/shell/basic/tiku_basic.h"
#endif

/*---------------------------------------------------------------------------*/
/* NVM-BACKED FILE STORE FOR /data                                           */
/*---------------------------------------------------------------------------*/

/* The store's NVM region.  On MSP430 it is `.persistent` (FRAM, durable now);
 * elsewhere it is `.bss` (functional but volatile until the direct-MRAM /
 * Flash backend lands). */
#if defined(PLATFORM_MSP430)
#define DATA_TFS_SECTION __attribute__((section(".persistent")))
#else
#define DATA_TFS_SECTION
#endif

static DATA_TFS_SECTION uint8_t data_tfs_region[TIKU_TFS_REGION_BYTES];
static tiku_tfs_t          data_fs;
static tiku_nvm_backend_t  data_be;
static uint8_t             data_fs_ready;

/* Byte-writable region backend: each write brackets the NVM protection window
 * (which gates FRAM on MSP430; on the other parts it is a no-op today and the
 * data is plain RAM). */
static int
data_be_write(tiku_nvm_backend_t *be, size_t off, const void *src, size_t len)
{
    uint16_t mpu = tiku_mpu_unlock_nvm();
    memcpy(be->base + off, src, len);
    tiku_mpu_lock_nvm(mpu);
    return 0;
}

static int
data_tfs_ensure(void)
{
    if (data_fs_ready) {
        return 0;
    }
    data_be.base  = data_tfs_region;
    data_be.size  = sizeof data_tfs_region;
    data_be.write = data_be_write;
    data_be.erase = NULL;
    data_be.ctx   = NULL;
    if (tiku_tfs_mount(&data_fs, &data_be) != TFS_OK) {
        return -1;
    }
    data_fs_ready = 1;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* DYNAMIC-DIRECTORY OPS — bridge /data to the file store                     */
/*---------------------------------------------------------------------------*/

typedef struct { tiku_vfs_dyn_list_cb cb; void *ctx; } data_list_w_t;

static void
data_list_thunk(const char *name, size_t len, void *vw)
{
    data_list_w_t *w = (data_list_w_t *)vw;
    (void)len;
    w->cb(name, w->ctx);
}

static void
data_dyn_list(tiku_vfs_dyn_list_cb cb, void *ctx)
{
    data_list_w_t w;
    if (data_tfs_ensure() != 0) {
        return;
    }
    w.cb = cb;
    w.ctx = ctx;
    (void)tiku_tfs_list(&data_fs, data_list_thunk, &w);
}

static int
data_dyn_read(const char *name, char *buf, size_t max)
{
    size_t n = 0;
    if (data_tfs_ensure() != 0) {
        return -1;
    }
    if (tiku_tfs_read(&data_fs, name, buf, max, &n) != TFS_OK) {
        return -1;
    }
    return (int)n;
}

static int
data_dyn_write(const char *name, const char *buf, size_t len)
{
    if (data_tfs_ensure() != 0) {
        return -1;
    }
    return (tiku_tfs_write(&data_fs, name, buf, len) == TFS_OK) ? 0 : -1;
}

static int
data_dyn_unlink(const char *name)
{
    if (data_tfs_ensure() != 0) {
        return -1;
    }
    return (tiku_tfs_delete(&data_fs, name) == TFS_OK) ? 0 : -1;
}

static const tiku_vfs_dynops_t data_dynops = {
    data_dyn_list, data_dyn_read, data_dyn_write, data_dyn_unlink
};

/*---------------------------------------------------------------------------*/
/* /data/basic — legacy BASIC program bridge (only when BASIC is built)      */
/*---------------------------------------------------------------------------*/

#if TIKU_SHELL_CMD_BASIC

static int
data_basic_read(char *buf, size_t max)
{
    return tiku_basic_vfs_read(buf, (unsigned int)max);
}

static int
data_basic_write(const char *buf, size_t len)
{
    return tiku_basic_vfs_write(buf, (unsigned int)len);
}

static const tiku_vfs_node_t data_children[] = {
    { "basic", TIKU_VFS_FILE, data_basic_read, data_basic_write, NULL, 0, NULL, NULL },
};

static const tiku_vfs_node_t data_node = {
    "data", TIKU_VFS_DIR, NULL, NULL,
    data_children, (uint8_t)(sizeof(data_children) / sizeof(data_children[0])),
    NULL, &data_dynops
};

#else  /* no BASIC: /data is purely the dynamic file store */

static const tiku_vfs_node_t data_node = {
    "data", TIKU_VFS_DIR, NULL, NULL,
    NULL, 0,
    NULL, &data_dynops
};

#endif

/*---------------------------------------------------------------------------*/
/* PUBLIC                                                                     */
/*---------------------------------------------------------------------------*/

const tiku_vfs_node_t *
tiku_vfs_tree_data_get(void)
{
    return &data_node;
}

#endif /* TIKU_SHELL_ENABLE */
