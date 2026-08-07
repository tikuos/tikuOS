/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_module.h - runtime-loadable native module ABI.
 *
 * A module is machine code compiled separately at a fixed address, so it cannot
 * link against firmware symbols and reaches every service through a jump table
 * passed to its entry point.  Included by both the firmware and the module build.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BASIC_MODULE_H_
#define TIKU_BASIC_MODULE_H_

#include <stddef.h>
#include <stdint.h>
#include "tiku_basic_ext.h"      /* the handler typedefs the table exposes */

/* 'TMOD' little-endian -- first word of a module image. */
#define TIKU_MODULE_MAGIC    0x444F4D54u
#define TIKU_MODULE_ABI      1u

/* Fixed module slot -- EXECUTABLE NVM (32 KB on ARM parts, ~4 KB on
 * MSP430), kept in sync with
 * __tiku_module_slot in the device linker script and the module's own .ld.
 * The module is linked at this VMA; the loader installs the image here and
 * runs it XIP (durable in place -- it survives reboot and power loss).
 *
 *   nordic (nRF54L15 + nRF54LM20): RRAM at the top of the shared 256 KB code
 *     window -- the SAME address on both parts, so one module image is
 *     family-portable.  SRAM is W^X (execute-never), so a module MUST run
 *     from RRAM -- which is byte-writable, so install is a store loop
 *     behind the WEN gate.
 *   apollo510/510b:     NO NVM slot -- the image is copied into the ITCM and
 *     run from there (TIKU_MODULE_EXEC_ADDR).  This is the one part where a RAM
 *     execution window has been measured, so it is the one part that uses one.
 *   apollo4l/4p:        same MRAM personality as apollo510 (bootrom-programmed,
 *     XIP), different geometry: 2 MB MRAM at 0x0, slot at the top of the
 *     0x18000-based code window.  The unified CACHECTRL cache is flushed
 *     after install (both parts define AM_PART_APOLLO4L).
 *   rp2350 (Pico 2):    QSPI flash, XIP; the slot spans EIGHT 4 KB erase
 *     sectors at the top of the code window.  Install walks them in a loop,
 *     staging each sector through a 4 KB SRAM buffer and committing
 *     erase+program via the boot-ROM path, with sector 0's header page left
 *     blank and programmed LAST -- flash can only clear bits, so the gate
 *     stays invalid until that final program.
 *   msp430 fr5994/fr6989: FRAM at the top of HIFRAM -- byte-writable in
 *     place (behind the MPU unlock window) and natively executable (the
 *     HIFRAM MPU segment is already R+W+X).  No cache, no barrier. */
#if defined(AM_PART_APOLLO510)
/* DELIBERATELY UNDEFINED on this part: there is no NVM carve (the module
 * executes from the ITCM -- see TIKU_MODULE_EXEC_ADDR below), and 0x488000 is
 * the NVM REGION BASE here, which is to say the NVM tier.  A stale reference
 * would program over live tier data, so leaving this undefined turns that
 * mistake into a compile error instead. */
#elif defined(AM_PART_APOLLO4L)
#define TIKU_MODULE_CARVE_ADDR  0x78000u
#elif defined(PLATFORM_RP2350)
/* Top 32 KB (8 erase sectors) of the flash code window; XIP.
 * Install goes sector-by-sector through the boot-ROM erase/program path. */
#define TIKU_MODULE_CARVE_ADDR  0x10060000u
#elif defined(TIKU_DEVICE_MSP430FR5994) || defined(__MSP430FR5994__)
/* Top 4 KB of HIFRAM (which the MPU already maps R+W+X, SAM 0x0755).
 * FRAM: byte-writable in place AND natively executable.  The slot ends
 * at 0x43FF0, short of the stock region's odd 0x43FF7 end (CPU47). */
#define TIKU_MODULE_CARVE_ADDR  0x43000u
#define TIKU_MODULE_CARVE_SIZE  0xFF0u
#elif defined(TIKU_DEVICE_MSP430FR6989) || defined(__MSP430FR6989__)
#define TIKU_MODULE_CARVE_ADDR  0x23000u
#define TIKU_MODULE_CARVE_SIZE  0xFF0u
#else
/* Nordic (nRF54L15 and nRF54LM20 alike): RRAM slot at the top of the
 * shared 256 KB code window.  Both parts use the SAME slot address, so
 * one module image is binary-compatible across the Nordic family. */
#define TIKU_MODULE_CARVE_ADDR  0x60000u
#endif
#ifndef TIKU_MODULE_CARVE_SIZE
#define TIKU_MODULE_CARVE_SIZE  0x8000u
#endif

/*
 * Where the image comes from.  A blob linked into the firmware would be
 * counted TWICE -- once as .rodata in the code window, once as the reserved
 * slot it is copied into.  The image is therefore an ordinary store file, and
 * the embedded blob is only an optional
 * SEEDER: when a board has never been provisioned, the first install writes the
 * embedded copy into the store and thereafter the FILE is authoritative.  That
 * is what makes a module replaceable over serial instead of by reflashing, and
 * it is why deleting the embedded copy (TIKU_BASIC_MODULE_EMBED=0) cannot brick
 * a provisioned board.
 *
 * Flat name, matching prog.bas / prog.ckpt: /data has a static "basic" node, so
 * a "mod/" prefix would render as a phantom folder beside it.
 */
#define TIKU_MODULE_FILE  "mod.bin"

/* Ship the embedded seeder by default: a board with no provisioned file must
 * still be able to install.  Set to 0 for a provisioning-only image once the
 * fleet is seeded -- that is what reclaims the image bytes. */
#ifndef TIKU_BASIC_MODULE_EMBED
#define TIKU_BASIC_MODULE_EMBED  1
#endif

/*
 * WHERE THE MODULE EXECUTES -- A SETTLED DECISION, NOT PENDING WORK.
 *
 * A module is pre-linked to an absolute address, so SOME fixed window is
 * unavoidable; nothing requires it to be in NVM.  The plan once read as "move
 * every platform's window into RAM and delete the NVM carve".  Measurement
 * turned that into a per-platform answer, because the condition that makes a
 * RAM window free holds on exactly one part:
 *
 *   apollo510   ITCM window at 0x1000, NO NVM carve (module_size = 0)
 *   apollo4l/p  XIP from the 32 KB NVM carve
 *   nRF54L15    XIP from the 32 KB NVM carve
 *   nRF54LM20   XIP from the 32 KB NVM carve
 *   rp2350      XIP from the 32 KB NVM carve
 *   MSP430      XIP from its 4 KB HIFRAM slot (natively executable; non-goal)
 *
 * Why APOLLO510 is the exception.  Its ITCM sits at 0x00000000 in a separate
 * address space and is not even declared in the linker script's MEMORY block --
 * dedicated instruction memory that nothing else can use.  Spending it costs
 * nothing, which is what let the NVM carve go.  The window starts 4 KB in
 * rather than at ITCM base so no module address can be zero and be mistaken for
 * a null pointer, by the loader's checks or the module's; it must match the
 * module's .ld exactly.
 *
 * The power question is now measured, not inferred.  ITCM and DTCM power share
 * one field, PWRCTRL->MEMPWREN.PWRENTCM, and nothing in arch/ambiq programs it,
 * so the reset default is what applies.  Arguing "the linker declares 512 KB
 * of DTCM, therefore PWRENTCM must be 7" would be unsound -- the port uses
 * ~30 KB of DTCM, so PWRENTCM=1 would fit too -- which is why a window needing
 * 36 KB against a possible 32 KB was a real risk.
 *
 * Run on an Apollo510B EVB, 2026-07-26 (TikuBench tests/memory/test_mem_tcm.c):
 * MEMPWREN=0x3f and MEMPWRSTATUS=0xdf both decode PWRENTCM/PWRSTTCM = 7, i.e.
 * ITCM 256 KB and DTCM 512 KB are powered at the reset default; ITCM accepts a
 * write and reads back through all 256 KB; and a stub copied there executes.
 * The 36 KB window fits with room to spare and the DTCM the linker declares is
 * genuinely there.  Keep the probe: nothing PROGRAMS PWRENTCM, so this is a
 * property of the reset default, and a silicon or SDK revision could move it.
 *
 * WHY EVERY OTHER PART KEEPS THE CARVE -- three independent reasons, each
 * measured or read out of the tree rather than assumed:
 *
 *   1. SRAM is the scarce resource; NVM is not.  The carve costs 0.9% (rp2350)
 *      to 2.8% (l15) of a part's NVM.  A 32 KB SRAM window would cost ~13% of
 *      the nRF54LM20's 240 KB primary bank -- the bank BASIC's arena and Axon's
 *      interlayer buffer already contend for.  Trading 2% of the abundant
 *      resource for 13% of the contested one is backwards.
 *   2. Their NVM already executes in place.  RRAM rides the background map as
 *      Normal RX (arch/nordic/tiku_mpu_arch.c deliberately does NOT re-gate it)
 *      and rp2350 runs XIP from flash by construction.  XIP needs no window, no
 *      MPU exception, and no copy.
 *   3. On Nordic, RAM execution is a HARD FAULT BY DESIGN.  TikuBench's RAM Exec
 *      Probe (tests/memory/test_mem_ramexec.c) measured it on an nRF54LM20: the
 *      primary bank accepts the write and reads it back, then executing faults
 *      (IPSR=3 over SWD), because tiku_mpu_arch.c marks both banks RW+XN as W^X
 *      hardening (2026-07 D.1).  rp2350's port uses the same W^X shape, so the
 *      same is presumed there and remains untested -- no board on the bench.
 *   And apollo4l/4p simply have no idle instruction memory to spend: one 384 KB
 *   TCM at 0x10000000, already carrying .data, .bss, heap and stack.
 *
 * Two alternatives considered and rejected.  Punching a permanently executable
 * hole in W^X recreates the exact write-then-execute primitive the hardening
 * removes.  Flipping a window's permissions in time instead (RW+XN to hold the
 * image, RO+X to run it, never both at once) preserves the invariant honestly --
 * but on Nordic it would still require reserving the 32 KB of SRAM, and an
 * unconditional feature-shaped carve is the pathology the whole v0.06 memory
 * rework exists to delete.  It is the right pattern only where a window already
 * exists: apollo510's is currently RWX with no MPU coverage at all, and that is
 * where the flip belongs.
 *
 * THE PATH THAT ACTUALLY DELETES THE CARVE is relocatable modules -- ROPI, or a
 * load-time relocation table -- so a module executes from wherever its store
 * file happens to land, with no fixed window anywhere and no SRAM cost.  The
 * design of record names it as the end state ("the RAM window is a waypoint,
 * not the end state").  It should not be built separately: P3d's A2 already
 * needs a GENERIC relocation backend for Nordic's compiled models, whose
 * baked-absolute-address problem is the same one, and absorbing this there is
 * how it gets done once instead of twice.
 */
#if defined(AM_PART_APOLLO510)
#define TIKU_MODULE_EXEC_IN_RAM  1
#define TIKU_MODULE_EXEC_ADDR    0x00001000u   /* ITCM + 4 KB (mod_demo_apollo510.ld) */
#else
#define TIKU_MODULE_EXEC_IN_RAM  0
#define TIKU_MODULE_EXEC_ADDR    TIKU_MODULE_CARVE_ADDR
#endif

/* Entry-offset convention: ARM Thumb entry addresses carry bit0 SET so
 * the loader can branch (carve_base + init_off) directly; MSP430 has no
 * Thumb bit and entry offsets are plain (even) byte offsets.  Modules
 * use this macro so one source builds for either CPU. */
#if defined(__MSP430__)
#define TIKU_MODULE_INIT_OFF(off)  (off)
#else
#define TIKU_MODULE_INIT_OFF(off)  ((off) | 1u)
#endif

/* Image header at the carve base.  init_off is the byte offset from the carve
 * base to the module's init routine, with the Thumb bit (bit0) SET so the
 * loader can call (carve_base + init_off) directly. */
typedef struct {
    uint32_t magic;          /* TIKU_MODULE_MAGIC                          */
    uint32_t abi_version;    /* TIKU_MODULE_ABI                            */
    uint32_t init_off;       /* offset to init routine | 1 (Thumb)         */
    uint32_t reserved;       /* 0 (image size / CRC live in the gate)      */
} tiku_module_header_t;

/* The firmware services a module may call -- the Tier-2 ABI as a table.  A
 * module stores nothing global for the MVP (its handlers are pure), but the
 * table is passed so stateful modules and the durable path can use it. */
typedef struct {
    uint32_t abi_version;
    int  (*register_fn)(const char *name, uint8_t arity,
                        tiku_basic_ext_nfn fn);
    int  (*register_strfn)(const char *name, tiku_basic_ext_strfn fn);
    int  (*register_stmt)(const char *name, tiku_basic_ext_stmt_fn fn);
    int  (*parse_expr)(const char **p, long *out);
    int  (*parse_strexpr)(const char **p, char *buf, size_t cap);
    void (*print)(const char *s);
    void (*error)(int cat, const char *msg);
    int  (*expect)(const char **p, char ch);
} tiku_basic_syscalls_t;

/* Module entry point.  The module defines this; the loader calls it. */
typedef void (*tiku_module_init_fn)(const tiku_basic_syscalls_t *sys);

/* --- Firmware-side loader (not seen by the module build) --- */
#ifndef TIKU_MODULE_BUILD

/**
 * @brief Install the embedded image into the RRAM slot (gate-last, durable)
 *        and activate it (validate + run init -> registers its BASIC words).
 * @return 0 loaded, -1 no image / too big / bad magic / feature off.
 */
int tiku_basic_module_load(void);

/**
 * @brief Activate the module already resident in the RRAM slot: validate its
 *        header and run its init (re-registers its words).  Safe to call every
 *        boot -- a no-op (-1) when the slot holds no valid module.
 * @return 0 activated, -1 no valid resident module / feature off.
 */
int tiku_basic_module_activate(void);

/** @brief 1 once a module has been activated this boot. */
int tiku_basic_module_loaded(void);

#endif /* TIKU_MODULE_BUILD */

#endif /* TIKU_BASIC_MODULE_H_ */
