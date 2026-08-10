/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_module.c - runtime-loadable native module loader.
 *
 * The image is a store file; where a part still installs into NVM, the 16-byte
 * header is invalidated first and written last, so a cut leaves an invalid magic
 * rather than a torn module.  Apollo510 instead copies it into the ITCM to run.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel/shell/basic/tiku_basic_module.h>

#if TIKU_BASIC_MODULE_ENABLE

#include <kernel/memory/tiku_mem.h>  /* WEN gate (nordic) / bootrom programmer
                                      * prototype via hal/tiku_mem_hal.h (510) */
#include <hal/tiku_cpu.h>            /* tiku_cpu_icache_invalidate */
#include <kernel/fs/tiku_tfs.h>      /* the module image is a store file */
#include <kernel/vfs/tree/tiku_vfs_tree_data.h>
#include <string.h>

/* Embedded image, wrapped as an object by the Makefile module block. */
extern const uint8_t _binary_mod_demo_bin_start[];
extern const uint8_t _binary_mod_demo_bin_end[];

/*---------------------------------------------------------------------------*/
/* IMAGE SOURCE: the store file first, the embedded blob as a seeder         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Locate the module image: the store file, optionally seeding it.
 *
 * The FILE is always authoritative, so a module uploaded over serial replaces
 * the built-in one without a reflash.  The embedded blob is only a SEEDER, and
 * only INSTALL (@p seed = 1) may fall back to it.
 *
 * @note ACTIVATE passes @p seed = 0, and that asymmetry IS the install/activate
 *       distinction: activate runs at every BASIC session start, so seeding
 *       there would silently arm the built-in module with nobody asking.  No
 *       file, no module.  The returned pointer may point straight into
 *       memory-mapped NVM, so the caller must not assume RAM.
 * @return 0 with @p src / @p len set, or -1 when no image is available at all.
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
         * here is not fatal: the install can still proceed from .rodata. */
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
/* The proven interrupt-masked, XIP-suspended boot-ROM flash path (declared
 * extern-local like the region backend does; defined in tiku_mem_arch.c). */
extern void tiku_rp2350_flash_commit_sector(uint32_t flash_offset,
                                            const uint8_t *src, size_t len);
extern void tiku_rp2350_flash_program(uint32_t flash_offset,
                                      const uint8_t *src, size_t len);
#endif

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
    const tiku_module_header_t *hdr;
    tiku_module_init_fn init;
    /* Bytes actually materialised into the window, 0 when unknown (the XIP
     * path, where the carve holds a previously installed image rather than
     * uninitialised memory).  init_off is bounded by this when it is known. */
    uint32_t img_len = 0u;

#if TIKU_MODULE_EXEC_IN_RAM
    /*
     * Materialise the image into the execution window, then run it there.  The
     * window is RAM (ITCM) so this must happen after every reset -- the module's
     * durability now lives in the store file, not in the window.
     *
     * The image is linked FOR this address, so a plain copy is enough: no
     * relocation, no fixups.  That is also the reason the window cannot simply
     * be "wherever there is room" -- see the header.
     */
    {
        const uint8_t *src = NULL;
        uint32_t       len = 0u;

        if (module_image(&src, &len, 0) != 0) {
            return -1;
        }
        /* Resting state is RW+XN, but a previously activated module left the
         * window RO+X -- make it writable again before copying over it. */
        tiku_mpu_module_window_exec(0);
        memcpy((void *)(uintptr_t)TIKU_MODULE_EXEC_ADDR, src, len);
        img_len = len;
        /* The bytes arrived through the D-side; the I-side must not serve stale
         * lines for an address about to be branched to.  The HAL brackets the
         * invalidate with DSB/ISB itself, so no barriers are needed here (and
         * the M55 reaches its TCMs directly, bypassing the D-cache). */
        tiku_cpu_icache_invalidate();
    }
#endif

    hdr = (const tiku_module_header_t *)(uintptr_t)TIKU_MODULE_EXEC_ADDR;

    if (hdr->magic != TIKU_MODULE_MAGIC ||
        hdr->abi_version != TIKU_MODULE_ABI) {
        return -1;                                /* no valid resident module */
    }
    /* The entry must land past the header, inside the IMAGE, and carry the
     * CPU's entry-address convention -- ARM Thumb wants bit0 SET, MSP430
     * wants a plain even offset.  A corrupt init_off must not send the PC
     * into the void.
     *
     * Bounding by the window (TIKU_MODULE_CARVE_SIZE) is not enough where the
     * image is COPIED in: a 200-byte module declaring init_off 20000 sits
     * inside the 32 KB window but far past the bytes memcpy wrote, so the
     * branch target is whatever the window happened to contain.  Bound by the
     * copied length once known, and never trust more than the window. */
    if (hdr->init_off < sizeof(tiku_module_header_t) ||
        hdr->init_off >= TIKU_MODULE_CARVE_SIZE ||
        (img_len != 0u && hdr->init_off >= img_len) ||
#if defined(__MSP430__)
        (hdr->init_off & 1u) != 0u) {
#else
        (hdr->init_off & 1u) == 0u) {
#endif
        return -1;
    }
    /*
     * W^X in time: the window has been writable up to here so the image could
     * be copied and its header checked.  Only now -- with magic, ABI and
     * init_off all validated -- does it become executable, and it gives up
     * write permission in the same operation.  A malformed image is therefore
     * never executable, and a running module's code is never writable.
     *
     * The flip carries its own DSB/ISB, which is what makes the very next
     * instruction fetch (the module's) see the new permissions.
     */
    tiku_mpu_module_window_exec(1);

    init = (tiku_module_init_fn)(uintptr_t)
           (TIKU_MODULE_EXEC_ADDR + hdr->init_off);
    init(&module_syscalls);   /* from the RAM window, or XIP from the carve */
    module_activated = 1u;
    return 0;
}

int
tiku_basic_module_load(void)
{
    const uint8_t *src = NULL;
    uint32_t       len = 0u;

#if !TIKU_MODULE_EXEC_IN_RAM && !defined(PLATFORM_MSP430)
    /*
     * The slot has drifted from the link three separate times in this file's
     * history (an MPU region a quarter of the slot, two module scripts linked
     * for windows that had since moved).  The linker's __tiku_code_limit is
     * where the reserve actually begins, so agree with it or install nothing.
     */
    {
        extern uint8_t __tiku_code_limit;

        if ((uintptr_t)&__tiku_code_limit != TIKU_MODULE_CARVE_ADDR) {
            return -1;
        }
    }
#endif
    if (module_image(&src, &len, 1) != 0) {
        return -1;
    }

#if TIKU_MODULE_EXEC_IN_RAM
    /*
     * P3f: nothing to install.  The image is durable in the STORE, and the
     * execution window is RAM, so "install" is finished the moment the file
     * exists -- module_image() has already guaranteed that (seeding it from the
     * embedded copy if this board had none).  The copy into the window happens
     * in activate(), which must run after every reset anyway because RAM does
     * not survive one.
     *
     * This is what deletes an entire class of code: the three-phase gate-last
     * NVM programming below exists purely to make a torn install detectable,
     * and a torn install is impossible when the durable copy is a file the
     * store already commits atomically.
     */
    (void)src;
    return tiku_basic_module_activate();   /* same contract as every backend */
#elif defined(AM_PART_APOLLO4L)
    /* MRAM install via the bootrom programmer (apollo4l/4p).  apollo510 shared
     * this path until its image moved to the ITCM above; the backends are still
     * interchangeable -- each supplies its own tiku_nvm_mram_program with the
     * part's bootrom entry and geometry.  Three-phase so a power cut
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
#elif defined(PLATFORM_RP2350)
    /* Flash install: the slot spans several 4 KB erase sectors, each
     * replaced through the proven interrupt-masked, XIP-suspended boot-ROM
     * path.  Flash can only clear bits, so the gate discipline inverts the
     * RRAM ordering: every sector the image touches is staged and
     * committed with the FIRST 256-byte page of sector 0 left ERASED
     * (0xFF -- an invalid magic), and that header page is programmed LAST
     * onto still-erased cells.  A cut at any point -- between sectors,
     * mid-sector, or mid-header -- leaves 0xFF or a torn word where the
     * magic should be, never a valid header over a torn body. */
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
    /* FRAM install: byte-write in place behind the MPU unlock window -- FRAM is
     * natively executable and uncached, so no barrier or flush is needed (the
     * FRAM controller completes a store before the next instruction retires;
     * only the RRAM branch below has a controller to wait on).
     *
     * Three phases, gate-last, for the same reason as RRAM: zero the magic
     * FIRST so a power cut part-way through a REinstall cannot leave the old
     * valid magic standing over a half-written image. */
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
        for (i = 0u; i < 4u; i++) {          /* 3. magic LAST -- the commit */
            slot[i] = src[i];
        }
        tiku_mpu_lock_nvm(saved);
    }
#else
    /* RRAM install: byte-write in place behind the WEN gate, in three phases so
     * the magic word is never valid over a body that is not fully written --
     * the same shape the Ambiq branch above uses, and the reason it matters is
     * REinstall: writing the body straight over a resident module left the old
     * (valid) magic standing, so a power cut mid-body produced a module that
     * passed the gate with a torn image behind it.  Zero the magic FIRST.
     *
     * Each phase ends with tiku_mem_arch_nvm_flush(), which spins until the
     * RRAM controller has committed.  Without it the phases are only ordered in
     * C, not in the controller, and the gate-last guarantee is not actually
     * enforced -- region_write() in arch/nordic/tiku_nvm_region_nordic.c
     * already follows this discipline for the same reason. */
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
        for (i = 0u; i < 4u; i++) {          /* 3. magic LAST -- the commit */
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

#else  /* feature off / non-RRAM platform */

int tiku_basic_module_load(void)     { return -1; }
int tiku_basic_module_activate(void) { return -1; }
int tiku_basic_module_loaded(void)   { return 0; }

#endif /* TIKU_BASIC_MODULE_ENABLE */
