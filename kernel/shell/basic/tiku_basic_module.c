/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_module.c - runtime-loadable native module loader (Tier 3 of
 * kintsugi/loadable.md).
 *
 * INSTALL writes a separately-compiled module image into the executable NVM
 * slot with a gate discipline that keeps every power-cut outcome safe: the
 * 16-byte header (whose magic word is the activation gate) is INVALIDATED
 * first, the body written next, and the real header written LAST.  A cut
 * mid-install therefore always leaves an invalid magic -- never a half-
 * written (or half-REwritten) module that "activates".  ACTIVATE validates
 * the resident image's header and calls its entry point, passing the jump
 * table (the Tier-2 ABI as a struct); the module registers its BASIC words
 * and returns.  The code executes in place from the slot -- no copy-to-RAM
 * -- and survives reboot: a boot only has to re-ACTIVATE (re-register into
 * the volatile Tier-2 table), never re-install.
 *
 * Two install backends, one per NVM personality:
 *   nordic (nRF54LM20 RRAM): byte-writable in place -- a store loop behind
 *     the WEN gate (tiku_mpu_unlock_nvm window).
 *   apollo510/510b (MRAM):   not CPU-writable -- spans are programmed via
 *     the bootrom (tiku_nvm_mram_program, SSRAM-staged), and the M55's
 *     I-cache is invalidated before the first XIP fetch of the new code.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel/shell/basic/tiku_basic_module.h>

#if TIKU_BASIC_MODULE_ENABLE

#include <kernel/memory/tiku_mem.h>  /* WEN gate (nordic) / bootrom programmer
                                      * prototype via hal/tiku_mem_hal.h (510) */
#include <hal/tiku_cpu.h>            /* tiku_cpu_icache_invalidate */
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
    /* The entry must land past the header, inside the slot, with the Thumb
     * bit set -- a corrupt init_off must not send the PC into the void. */
    if (hdr->init_off < sizeof(tiku_module_header_t) ||
        hdr->init_off >= TIKU_MODULE_CARVE_SIZE ||
        (hdr->init_off & 1u) == 0u) {
        return -1;
    }
    init = (tiku_module_init_fn)(uintptr_t)
           (TIKU_MODULE_CARVE_ADDR + hdr->init_off);
    init(&module_syscalls);                       /* XIP from the NVM slot     */
    module_activated = 1u;
    return 0;
}

int
tiku_basic_module_load(void)
{
    uint32_t len = (uint32_t)(_binary_mod_demo_bin_end -
                              _binary_mod_demo_bin_start);
    const uint8_t *src = _binary_mod_demo_bin_start;

    if (len < sizeof(tiku_module_header_t) || len > TIKU_MODULE_CARVE_SIZE) {
        return -1;
    }

#if defined(AM_PART_APOLLO510) || defined(AM_PART_APOLLO4L)
    /* MRAM install via the bootrom programmer (apollo510/510b and apollo4l/4p
     * share the mechanism; each backend supplies its own tiku_nvm_mram_program
     * with the part's bootrom entry and geometry).  Three-phase so a power cut
     * at ANY point -- including during a REinstall over a resident module --
     * leaves an invalid gate, never a valid header over a torn body:
     *   1. invalidate the gate  (program a zeroed 16-byte header)
     *   2. program the body     (offset 16..len)
     *   3. program the real header LAST (the magic word arms the gate)
     * Each phase is 16-byte aligned, so no RMW spillover between them. */
    {
        static const uint8_t zero_hdr[sizeof(tiku_module_header_t)] = { 0 };

        if (tiku_nvm_mram_program(TIKU_MODULE_CARVE_ADDR, zero_hdr,
                                  sizeof(zero_hdr)) != 0) {
            return -1;
        }
        if (len > sizeof(tiku_module_header_t) &&
            tiku_nvm_mram_program(TIKU_MODULE_CARVE_ADDR +
                                      sizeof(tiku_module_header_t),
                                  src + sizeof(tiku_module_header_t),
                                  len - sizeof(tiku_module_header_t)) != 0) {
            return -1;
        }
        if (tiku_nvm_mram_program(TIKU_MODULE_CARVE_ADDR, src,
                                  sizeof(tiku_module_header_t)) != 0) {
            return -1;
        }
    }
    /* The D-side is coherent (the programmer invalidates each programmed
     * span); the I-side is not -- drop stale lines before fetching the
     * just-installed code. */
    tiku_cpu_icache_invalidate();
#else
    /* RRAM install: byte-write in place behind the WEN gate: body first
     * (offset 4..), then the 4-byte magic LAST -- the magic is the install
     * gate.  (Known gap, deferred until it can be re-proven on the LM20:
     * a REinstall over a resident module should zero the magic first, as
     * the apollo510 branch above does.) */
    {
        volatile uint8_t *slot =
            (volatile uint8_t *)(uintptr_t)TIKU_MODULE_CARVE_ADDR;
        uint16_t saved;
        uint32_t i;

        saved = tiku_mpu_unlock_nvm();
        for (i = 4u; i < len; i++) {
            slot[i] = src[i];
        }
        for (i = 0u; i < 4u; i++) {
            slot[i] = src[i];
        }
        tiku_mpu_lock_nvm(saved);
    }
    __asm__ volatile ("dsb 0xF; isb 0xF" ::: "memory");  /* code just written */
#endif
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
