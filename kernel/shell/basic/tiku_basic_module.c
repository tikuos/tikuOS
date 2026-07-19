/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_module.c - runtime-loadable native module loader (Tier 3 of
 * kintsugi/loadable.md).
 *
 * INSTALL writes a separately-compiled module image into the executable RRAM
 * slot in place behind the WEN gate -- gate-last (the whole body first, then
 * the magic word), so a power cut mid-install leaves an invalid magic, never a
 * half-written module that "activates".  ACTIVATE validates the resident
 * image's header and calls its entry point, passing the jump table (the Tier-2
 * ABI as a struct); the module registers its BASIC words and returns.  Because
 * RRAM is execute-in-place, the code runs from the slot with no copy-to-RAM and
 * survives reboot -- a boot only has to re-ACTIVATE (re-register into the
 * volatile Tier-2 table), never re-install.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel/shell/basic/tiku_basic_module.h>

#if TIKU_BASIC_MODULE_ENABLE

#include <kernel/memory/tiku_mem.h>      /* tiku_mpu_unlock_nvm / lock_nvm */
#include <string.h>

/* Embedded image, wrapped as an ARM object by the Makefile module block. */
extern const uint8_t _binary_mod_demo_bin_start[];
extern const uint8_t _binary_mod_demo_bin_end[];

static uint8_t module_activated;

/* The firmware services the module reaches through the jump table -- exactly
 * the Tier-2 ABI (tiku_basic_ext.h). */
static const tiku_basic_syscalls_t module_syscalls = {
    TIKU_MODULE_ABI,
    tiku_basic_register_fn,
    tiku_basic_register_strfn,
    tiku_basic_register_stmt,
    tiku_basic_ext_parse_expr,
    tiku_basic_ext_parse_strexpr,
    tiku_basic_ext_print,
    tiku_basic_ext_error,
    tiku_basic_ext_expect,
};

int
tiku_basic_module_activate(void)
{
    const tiku_module_header_t *hdr =
        (const tiku_module_header_t *)(uintptr_t)TIKU_MODULE_CARVE_ADDR;
    tiku_module_init_fn init;

    if (hdr->magic != TIKU_MODULE_MAGIC ||
        hdr->abi_version != TIKU_MODULE_ABI) {
        return -1;                                /* no valid resident module */
    }
    init = (tiku_module_init_fn)(uintptr_t)
           (TIKU_MODULE_CARVE_ADDR + hdr->init_off);
    init(&module_syscalls);                       /* XIP from RRAM             */
    module_activated = 1u;
    return 0;
}

int
tiku_basic_module_load(void)
{
    uint32_t len = (uint32_t)(_binary_mod_demo_bin_end -
                              _binary_mod_demo_bin_start);
    volatile uint8_t *slot = (volatile uint8_t *)(uintptr_t)TIKU_MODULE_CARVE_ADDR;
    const uint8_t *src = _binary_mod_demo_bin_start;
    uint16_t saved;
    uint32_t i;

    if (len < sizeof(tiku_module_header_t) || len > TIKU_MODULE_CARVE_SIZE) {
        return -1;
    }
    /* Byte-write in place behind the WEN gate: body first (offset 4..), then
     * the 4-byte magic LAST -- the magic is the install gate. */
    saved = tiku_mpu_unlock_nvm();
    for (i = 4u; i < len; i++) {
        slot[i] = src[i];
    }
    for (i = 0u; i < 4u; i++) {
        slot[i] = src[i];
    }
    tiku_mpu_lock_nvm(saved);
    __asm__ volatile ("dsb 0xF; isb 0xF" ::: "memory");  /* code just written */
    return tiku_basic_module_activate();
}

int
tiku_basic_module_loaded(void)
{
    return module_activated;
}

#else  /* feature off / non-RRAM platform */

int tiku_basic_module_load(void)     { return -1; }
int tiku_basic_module_activate(void) { return -1; }
int tiku_basic_module_loaded(void)   { return 0; }

#endif /* TIKU_BASIC_MODULE_ENABLE */
