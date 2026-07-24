/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_trng.c - "trng" command implementation
 *
 * Reads bytes from the platform entropy source and prints them as hex, so
 * the randomness behind the cert-TLS handshake can be sanity-checked on the
 * bench (non-zero, varying across reads).  Platform-gated: RP2350 and Ambiq
 * Apollo, and Nordic nRF54L (CRACEN) expose a hardware-TRNG HAL, and MSP430 a
 * software entropy source
 * (when the crypto kit is built); other builds print an "unsupported" line.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_trng.h"
#include <kernel/shell/tiku_shell.h>             /* SHELL_PRINTF */

#if defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_trng_arch.h>
#define TIKU_SHELL_TRNG_HAVE 1
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_trng_arch.h>
#define TIKU_SHELL_TRNG_HAVE 1
#elif defined(PLATFORM_NORDIC)
/* nRF54L CRACEN ring-oscillator TRNG (AES-conditioned). */
#include <arch/nordic/tiku_trng_arch.h>
#define TIKU_SHELL_TRNG_HAVE 1
#elif defined(PLATFORM_MSP430) && TIKU_KIT_CRYPTO_ENABLE
/* Software entropy source; only linked when the crypto kit (SHA-256
 * conditioner) is compiled in. */
#include <arch/msp430/tiku_trng_arch.h>
#define TIKU_SHELL_TRNG_HAVE 1
#else
#define TIKU_SHELL_TRNG_HAVE 0
#endif

void
tiku_shell_cmd_trng(uint8_t argc, const char *argv[])
{
#if TIKU_SHELL_TRNG_HAVE
    static const char hexd[] = "0123456789abcdef";
    uint8_t buf[64];
    int n = 16, i, rc;

    if (argc >= 2 && argv[1] != (const char *)0) {
        const char *p = argv[1];
        n = 0;
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + (*p++ - '0');
        }
        if (n <= 0) {
            n = 16;
        }
        if (n > (int)sizeof buf) {
            n = (int)sizeof buf;
        }
    }

    rc = tiku_trng_arch_read_bytes(buf, (size_t)n);
    if (rc != TIKU_TRNG_OK) {
        SHELL_PRINTF("trng: entropy source error %d\n", rc);
        return;
    }
    for (i = 0; i < n; i++) {
        char pair[3];
        pair[0] = hexd[(buf[i] >> 4) & 0x0f];
        pair[1] = hexd[buf[i] & 0x0f];
        pair[2] = '\0';
        SHELL_PRINTF("%s", pair);
    }
    SHELL_PRINTF("\n");
#else
    (void)argc;
    (void)argv;
    SHELL_PRINTF("trng: no entropy source available in this build\n");
#endif
}
