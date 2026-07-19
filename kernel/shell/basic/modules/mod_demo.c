/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * mod_demo.c - a runtime-loadable native module (Tier 3 of loadable.md).
 *
 * Compiled SEPARATELY from the firmware, at the module carve VMA, with no
 * firmware symbols linked in.  It reaches the interpreter only through the
 * jump table passed to module_init().  The loader copies this image into the
 * carve and calls module_init(&syscalls); the module registers its words and
 * returns.  Handlers here are PURE (no firmware callback), so the module holds
 * no state -- the minimal proof that separately-compiled native code executes
 * from the carve and plugs into BASIC through the ABI seam.
 *
 * Build: arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -ffreestanding -nostdlib
 *        -T mod_demo.ld ... ; objcopy -O binary.  See the Makefile module block.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define TIKU_MODULE_BUILD 1
#include "../tiku_basic_module.h"

/* MODFIB(n): the nth Fibonacci number (MODFIB(10) = 55).  Pure integer math
 * -- calls no firmware service, so it needs no jump-table state. */
static int
mod_fib(const long *args, int argc, long *out)
{
    long n = args[0], a = 0, b = 1, i;
    (void)argc;
    for (i = 0; i < n; i++) {
        long t = a + b;
        a = b;
        b = t;
    }
    *out = a;
    return 0;
}

/* MODMUL(a, b): a * b -- a second word, to prove multiple registrations from
 * one module. */
static int
mod_mul(const long *args, int argc, long *out)
{
    (void)argc;
    *out = args[0] * args[1];
    return 0;
}

/* Image header at the carve base (.modhdr, placed first by the linker).
 * init_off = 16 | 1: module_init sits immediately after this 16-byte header,
 * with the Thumb bit set so the loader calls (carve_base + init_off). */
__attribute__((section(".modhdr"), used))
const tiku_module_header_t mod_header = {
    TIKU_MODULE_MAGIC, TIKU_MODULE_ABI, 16u | 1u, 0u
};

/* Entry point -- the linker forces it to carve_base + 16 (.modinit). */
__attribute__((section(".modinit"), used))
void
module_init(const tiku_basic_syscalls_t *sys)
{
    if (sys == 0 || sys->abi_version != TIKU_MODULE_ABI) {
        return;
    }
    sys->register_fn("MODFIB", 1u, mod_fib);
    sys->register_fn("MODMUL", 2u, mod_mul);
}
