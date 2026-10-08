/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * mod_demo.c - a runtime-loadable native module.
 *
 * Compiled separately from the firmware at a fixed address with no firmware
 * symbols linked, it reaches the interpreter only through the jump table passed
 * to module_init().  Its handlers are pure, so the module holds no state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define TIKU_MODULE_BUILD 1
#include "../tiku_basic_module.h"

/** @brief MODFIB(n): the nth Fibonacci number (MODFIB(10) = 55). */
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

/** @brief MODMUL(a, b): a * b, a second word registered by the same module. */
static int
mod_mul(const long *args, int argc, long *out)
{
    (void)argc;
    *out = args[0] * args[1];
    return 0;
}

/** Image header (.modhdr, placed first by the module script); module_init
 *  follows it at offset 16, encoded with TIKU_MODULE_INIT_OFF(). */
__attribute__((section(".modhdr"), used))
const tiku_module_header_t mod_header = {
    TIKU_MODULE_MAGIC, TIKU_MODULE_ABI, TIKU_MODULE_INIT_OFF(16u), 0u
};

/**
 * @brief Entry point, placed at image offset 16 (.modinit) by the module
 *        script: registers MODFIB and MODMUL through the service table.
 */
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
