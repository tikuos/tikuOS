/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_module.h - runtime-loadable native module ABI (Tier 3 of
 * kintsugi/loadable.md).
 *
 * A native module is machine code compiled SEPARATELY from the firmware, at a
 * fixed load address (the module carve), that registers Tier-2 BASIC words at
 * load time.  Because it is separately compiled, it cannot link against
 * firmware symbols -- it reaches every firmware service through a JUMP TABLE
 * (tiku_basic_syscalls_t) passed to its entry point.  This is the exact
 * Tier-2 ABI (tiku_basic_ext.h) re-exposed as a table so a load-time-resolved
 * module can call it.
 *
 * The module image begins with a tiku_module_header_t at the carve base; its
 * init routine is at carve_base + init_off.  The loader validates the header,
 * then calls init(&syscalls); the module registers its words and returns.
 * This header is included by BOTH the firmware and the separate module build,
 * so it stays plain C99 + the Tier-2 handler typedefs.
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
 *   nordic (nRF54LM20): RRAM just below the durable-persist region.  SRAM is
 *     W^X (execute-never), so a module MUST run from RRAM -- which is byte-
 *     writable, so install is a store loop behind the WEN gate.
 *   apollo510/510b:     MRAM at the top of the (shrunk) code window.  MRAM is
 *     executable but NOT CPU-writable -- install goes through the bootrom
 *     programmer (tiku_nvm_mram_program), and the M55's I-cache is
 *     invalidated before the first XIP call.
 *   apollo4l/4p:        same MRAM personality as apollo510 (bootrom-programmed,
 *     XIP), different geometry: 2 MB MRAM at 0x0, slot at the top of the
 *     0x18000-based code window.  The unified CACHECTRL cache is flushed
 *     after install (both parts define AM_PART_APOLLO4L).
 *   rp2350 (Pico 2):    QSPI flash, XIP; the slot is exactly ONE 4 KB erase
 *     sector at the top of the code window.  Install stages the image in
 *     SRAM with the header page blank, commits erase+program via the
 *     boot-ROM path, then programs the header page LAST -- flash can only
 *     clear bits, so the gate stays invalid until that final program.
 *   msp430 fr5994/fr6989: FRAM at the top of HIFRAM -- byte-writable in
 *     place (behind the MPU unlock window) and natively executable (the
 *     HIFRAM MPU segment is already R+W+X).  No cache, no barrier. */
#if defined(AM_PART_APOLLO510)
#define TIKU_MODULE_CARVE_ADDR  0x488000u
#elif defined(AM_PART_APOLLO4L)
#define TIKU_MODULE_CARVE_ADDR  0x90000u
#elif defined(PLATFORM_RP2350)
/* Top 32 KB (8 erase sectors) of the flash code window; XIP.
 * Install goes sector-by-sector through the boot-ROM erase/program path. */
#define TIKU_MODULE_CARVE_ADDR  0x100F8000u
#elif defined(TIKU_DEVICE_MSP430FR5994) || defined(__MSP430FR5994__)
/* Top 4 KB of HIFRAM (which the MPU already maps R+W+X, SAM 0x0755).
 * FRAM: byte-writable in place AND natively executable.  The slot ends
 * at 0x43FF0, short of the stock region's odd 0x43FF7 end (CPU47). */
#define TIKU_MODULE_CARVE_ADDR  0x43000u
#define TIKU_MODULE_CARVE_SIZE  0xFF0u
#elif defined(TIKU_DEVICE_MSP430FR6989) || defined(__MSP430FR6989__)
#define TIKU_MODULE_CARVE_ADDR  0x23000u
#define TIKU_MODULE_CARVE_SIZE  0xFF0u
#elif defined(TIKU_DEVICE_NRF54L15)
/* RRAM slot at the top of the code window, canonical order. */
#define TIKU_MODULE_CARVE_ADDR  0x0B8000u
#else                                    /* nordic nRF54LM20 RRAM slot */
#define TIKU_MODULE_CARVE_ADDR  0x0F8000u
#endif
#ifndef TIKU_MODULE_CARVE_SIZE
#define TIKU_MODULE_CARVE_SIZE  0x8000u
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
