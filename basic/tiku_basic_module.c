/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_module.c - runtime-loadable native module loader.
 *
 * Installs the /data image into the part's NVM slot with the header's magic
 * invalidated first and written last, so a cut never leaves a valid header over
 * a torn body.  Apollo510 and the ESP32-C61 copy it into a RAM window instead.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <basic/tiku_basic_module.h>

#if TIKU_BASIC_MODULE_ENABLE

#if defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_psram_arch.h>
#endif

#include <kernel/memory/tiku_mem.h>  /* NVM unlock and flush, window flip,
                                      * MRAM programmer (apollo4l/4p) */
#include <hal/tiku_cpu.h>            /* tiku_cpu_icache_invalidate */
#include <kernel/fs/tiku_tfs.h>      /* the module image is a store file */
#include <kernel/vfs/tree/tiku_vfs_tree_data.h>
#include <string.h>

/* Embedded image, wrapped as an object by the Makefile module block. */
extern const uint8_t _binary_mod_demo_bin_start[];
extern const uint8_t _binary_mod_demo_bin_end[];

/*---------------------------------------------------------------------------*/
/* IMAGE SOURCE                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Whether a mapped image starts with a valid module header.
 *
 * Install and activate check the source before they write anything, so a
 * refused image leaves the resident module and its registered words intact.
 *
 * @param src  Mapped image bytes, at least a header long
 * @param len  Image length, or 0 for an installed image of unknown length
 * @return Non-zero when magic and ABI match and init_off is a valid entry
 */
static int
module_src_ok(const uint8_t *src, uint32_t len)
{
    const tiku_module_header_t *h = (const tiku_module_header_t *)(uintptr_t)src;

    if (h->magic != TIKU_MODULE_MAGIC || h->abi_version != TIKU_MODULE_ABI) {
        return 0;
    }
    /* The entry must land past the header and inside the image, and follow
     * the CPU's entry convention: bit 0 set on ARM (Thumb), clear on MSP430
     * and RISC-V.  An installed image of unknown length is bounded by the
     * slot alone. */
    if (h->init_off < sizeof(tiku_module_header_t) ||
        h->init_off >= TIKU_MODULE_CARVE_SIZE ||
        (len != 0u && h->init_off >= len) ||
#if defined(__MSP430__) || defined(__riscv)
        (h->init_off & 1u) != 0u) {
#else
        (h->init_off & 1u) == 0u) {
#endif
        return 0;
    }
    return 1;
}

/**
 * @brief Locate the module image: the store file, optionally seeding it.
 *
 * The file is used when its size lies between a header and the slot.  Otherwise
 * only install (@p seed = 1) falls back to the embedded copy and writes it to
 * the store; activate passes 0, so it never arms the built-in module unasked.
 *
 * @note The returned pointer may point into memory-mapped NVM, not RAM.
 * @return 0 with @p src / @p len set, or -1 when no image is available.
 */
static int
module_image(const uint8_t **src, uint32_t *len, int seed)
{
    tiku_tfs_t *fs = tiku_vfs_tree_data_store();
    const void *p  = NULL;
    size_t      n  = 0u;

    if (fs != NULL && tiku_tfs_map(fs, TIKU_MODULE_FILE, &p, &n) == TFS_OK &&
        n >= sizeof(tiku_module_header_t) && n <= TIKU_MODULE_CARVE_SIZE) {
        *src = (const uint8_t *)p;
        *len = (uint32_t)n;
        return 0;
    }
#if TIKU_BASIC_MODULE_EMBED
    if (seed) {
        uint32_t elen = (uint32_t)(_binary_mod_demo_bin_end -
                                   _binary_mod_demo_bin_start);
        if (elen < sizeof(tiku_module_header_t) ||
            elen > TIKU_MODULE_CARVE_SIZE) {
            return -1;
        }
        /* Seed the store so the file becomes the source from now on.  A failure
         * here is not fatal: the install can still proceed from .rodata.  On a
         * part with a RAM window the module then lasts until the next reset,
         * since activate at boot copies from the file. */
        if (fs != NULL) {
            (void)tiku_tfs_write(fs, TIKU_MODULE_FILE,
                                 _binary_mod_demo_bin_start, elen);
        }
        *src = _binary_mod_demo_bin_start;
        *len = elen;
        return 0;
    }
#else
    (void)seed;                      /* provisioning-only build, none present */
#endif
    return -1;
}

#if defined(PLATFORM_RP2350)
/* The interrupt-masked, XIP-suspended boot-ROM flash path, defined in
 * tiku_mem_arch.c and declared locally as the region backend declares it. */
extern void tiku_rp2350_flash_commit_sector(uint32_t flash_offset,
                                            const uint8_t *src, size_t len);
extern void tiku_rp2350_flash_program(uint32_t flash_offset,
                                      const uint8_t *src, size_t len);
#endif

/** 1 once a module has been activated this boot. */
static uint8_t module_activated;

/* The service table passed to module_init(): the registry, parser and output
 * services tiku_basic_ext.h declares. */
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

/**
 * @brief Activate the module: on a part with a RAM window copy the image in
 *        first, then validate the resident header and run its init.
 *
 * @param src  Image for the RAM window, or NULL to take the store file
 * @param len  Bytes in @p src
 * @return 0 activated, -1 no valid module
 */
static int
module_activate(const uint8_t *src, uint32_t len)
{
    const tiku_module_header_t *hdr;
    tiku_module_init_fn init;
    /* Bytes copied into the window; 0 on the XIP path, where the slot holds an
     * installed image of unknown length.  Bounds init_off when known. */
    uint32_t img_len = 0u;

#if TIKU_MODULE_EXEC_IN_RAM
    /*
     * Copy the image into the RAM window and run it there.  The window does
     * not survive a reset, so this runs at every activate; the durable copy is
     * the store file.  The image is linked for this address, so a plain copy
     * needs no relocation.
     */
    {
        if (src == NULL && module_image(&src, &len, 0) != 0) {
            return -1;
        }
        if (!module_src_ok(src, len)) {
            return -1;
        }
#if defined(PLATFORM_ESP32C61)
        /* The window is the PSRAM's, which may not be up yet this boot. */
        if (tiku_esp32c61_psram_init() != TIKU_ESP32C61_PSRAM_OK) {
            return -1;
        }
#endif
        /* Resting state is RW+XN, but a previously activated module left the
         * window RO+X -- make it writable again before copying over it. */
        tiku_mpu_module_window_exec(0);
        memcpy((void *)(uintptr_t)TIKU_MODULE_EXEC_ADDR, src, len);
        img_len = len;
        /* The copy went through the data side, so drop stale instruction
         * lines before branching into the window.  The HAL issues the barriers
         * itself (and on the ESP32-C61 writes the cached PSRAM back first);
         * the M55 reaches its TCMs without the D-cache. */
        tiku_cpu_icache_invalidate();
    }
#else
    (void)src;
    (void)len;
#endif

    hdr = (const tiku_module_header_t *)(uintptr_t)TIKU_MODULE_EXEC_ADDR;

    /* Where the image was copied in, the copied length bounds init_off as
     * well as the window, since the window past the copy holds stale
     * bytes. */
    if (!module_src_ok((const uint8_t *)hdr, img_len)) {
        return -1;                                /* no valid resident module */
    }
    /*
     * W^X in time, on the parts with a RAM window: the window becomes
     * executable only now that magic, ABI and init_off are valid, and loses
     * write permission in the same step, so a malformed image never runs and
     * running code is never writable.  The flip carries its own barriers, so
     * the module's first fetch sees it.  On XIP parts this is a no-op.
     */
    tiku_mpu_module_window_exec(1);

    init = (tiku_module_init_fn)(uintptr_t)
           (TIKU_MODULE_EXEC_ADDR + hdr->init_off);
    init(&module_syscalls);   /* from the RAM window, or in place in the slot */
    module_activated = 1u;
    return 0;
}

int
tiku_basic_module_activate(void)
{
    return module_activate(NULL, 0u);
}

int
tiku_basic_module_load(void)
{
    const uint8_t *src = NULL;
    uint32_t       len = 0u;

#if !TIKU_MODULE_EXEC_IN_RAM && !defined(PLATFORM_MSP430)
    /*
     * The linker's __tiku_code_limit is where the module reserve begins.
     * Install only when it equals TIKU_MODULE_CARVE_ADDR, so a slot address
     * that disagrees with the link is never written.
     */
    {
        extern uint8_t __tiku_code_limit;

        if ((uintptr_t)&__tiku_code_limit != TIKU_MODULE_CARVE_ADDR) {
            return -1;
        }
    }
#endif
    if (module_image(&src, &len, 1) != 0 || !module_src_ok(src, len)) {
        return -1;
    }

#if TIKU_MODULE_EXEC_IN_RAM
    /*
     * Nothing to install: the durable copy is the store file, which
     * module_image() has seeded if it was missing.  The window takes the
     * image already in hand, so a seed write that failed does not fail the
     * load.  The store commits a file atomically, so no gate-last programming
     * is needed here.
     */
    return module_activate(src, len);      /* same contract as every backend */
#elif defined(AM_PART_APOLLO4L)
    /* MRAM install through the bootrom programmer (apollo4l/4p), in three
     * phases so a power cut at any point, a reinstall included, leaves an
     * invalid header rather than a valid header over a torn body:
     *   1. program a zeroed 16-byte header
     *   2. program the body          (offset 16..len)
     *   3. program the real header last (its magic makes the image valid)
     * Each phase is 16-byte aligned, so no phase's read-modify-program spills
     * into another's bytes. */
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
    /* Drop stale instruction lines before fetching the new code. */
    tiku_cpu_icache_invalidate();
#elif defined(PLATFORM_RP2350)
    /* Flash install, one 4 KB erase sector at a time through the
     * interrupt-masked, XIP-suspended boot-ROM path, covering only the
     * sectors the image touches.  Flash can only clear bits, so the first
     * 256-byte page of sector 0 stays erased (0xFF, an invalid magic) while
     * the sectors are committed and is programmed last, onto erased cells.
     * A cut at any point leaves 0xFF or a torn word where the magic should
     * be, never a valid header over a torn body. */
    {
        static uint8_t stage[4096u];
        uint32_t base = (uint32_t)(TIKU_MODULE_CARVE_ADDR - 0x10000000u);
        uint32_t done;

        for (done = 0u; done < len; done += 4096u) {
            uint32_t chunk = len - done;
            if (chunk > 4096u) {
                chunk = 4096u;
            }
            memset(stage, 0xFF, sizeof(stage));
            memcpy(stage, src + done, chunk);
            if (done == 0u) {
                memset(stage, 0xFF, 256u);         /* header page: erased  */
            }
            tiku_rp2350_flash_commit_sector(base + done, stage,
                                            sizeof(stage));
        }

        memset(stage, 0xFF, 256u);
        memcpy(stage, src, (len < 256u) ? len : 256u);  /* real first page */
        tiku_rp2350_flash_program(base, stage, 256u);
    }
    __asm__ volatile ("dsb 0xF; isb 0xF" ::: "memory");  /* code just written */
#elif defined(PLATFORM_MSP430)
    /* FRAM install: byte writes in place inside the MPU unlock window.  FRAM
     * is natively executable and the FRAM controller completes a store before
     * the next instruction retires, so no barrier or flush is needed.  The
     * magic is zeroed first and written last, so a cut during a reinstall
     * cannot leave the old magic over a half-written image. */
    {
        volatile uint8_t *slot =
            (volatile uint8_t *)(uintptr_t)TIKU_MODULE_CARVE_ADDR;
        uint16_t saved;
        uint32_t i;

        saved = tiku_mpu_unlock_nvm();
        for (i = 0u; i < 4u; i++) {          /* 1. invalidate the gate */
            slot[i] = 0u;
        }
        for (i = 4u; i < len; i++) {         /* 2. body */
            slot[i] = src[i];
        }
        for (i = 0u; i < 4u; i++) {          /* 3. magic last: the commit */
            slot[i] = src[i];
        }
        tiku_mpu_lock_nvm(saved);
    }
#else
    /* RRAM install: byte writes in place behind the WEN gate.  The magic is
     * zeroed first and written last, so a cut during a reinstall cannot leave
     * the old magic over a torn body.  Each phase ends with
     * tiku_mem_arch_nvm_flush(), which waits until the RRAM controller has
     * committed; without it the phases are ordered only in C, not in the
     * controller.  region_write() in tiku_nvm_region_nordic.c does the same. */
    {
        volatile uint8_t *slot =
            (volatile uint8_t *)(uintptr_t)TIKU_MODULE_CARVE_ADDR;
        uint16_t saved;
        uint32_t i;

        saved = tiku_mpu_unlock_nvm();
        for (i = 0u; i < 4u; i++) {          /* 1. invalidate the gate */
            slot[i] = 0u;
        }
        tiku_mem_arch_nvm_flush();
        for (i = 4u; i < len; i++) {         /* 2. body */
            slot[i] = src[i];
        }
        tiku_mem_arch_nvm_flush();
        for (i = 0u; i < 4u; i++) {          /* 3. magic last: the commit */
            slot[i] = src[i];
        }
        tiku_mem_arch_nvm_flush();
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

#else  /* TIKU_BASIC_MODULE_ENABLE == 0 */

int tiku_basic_module_load(void)     { return -1; }
int tiku_basic_module_activate(void) { return -1; }
int tiku_basic_module_loaded(void)   { return 0; }

#endif /* TIKU_BASIC_MODULE_ENABLE */
