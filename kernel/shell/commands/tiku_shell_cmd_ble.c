/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ble.c - "ble" command: EM9305 radio first-contact probe.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_ble.h"

#include <kernel/shell/tiku_shell.h>          /* SHELL_PRINTF */
#include <arch/ambiq/tiku_em9305.h>

/**
 * @brief Run the EM9305 M0/M1 self-test and print the results.
 *
 * M0 gate: STS1 reads 0xC0 -- the IOM6 SPI master genuinely reaches the radio.
 * M1 gate: HCI Reset returns an HCI Command Complete event (status 0 = ok).
 */
void tiku_shell_cmd_ble(uint8_t argc, const char *argv[]) {
    tiku_em9305_probe_t p;
    int rc;
    uint16_t i;

    (void)argc;
    (void)argv;

    SHELL_PRINTF("EM9305 first-contact probe (IOM6 SPI @16MHz)...\n");
    rc = tiku_em9305_probe(&p);

    /* --- reset (with RDY diagnostics) --- */
    SHELL_PRINTF("  reset:  spi_init=%s RDY[init=%u low=%u high=%u final=%u]\n",
                 p.spi_rc ? "FAIL" : "ok",
                 (unsigned)p.rdy_initial, (unsigned)p.saw_low,
                 (unsigned)p.saw_high, (unsigned)p.rdy_final);
    if (p.reset_rc != TIKU_EM9305_OK) {
        SHELL_PRINTF("  reset:  FAIL (rc=%d) -- radio never signalled ready\n",
                     p.reset_rc);
        return;
    }
    SHELL_PRINTF("  reset:  ok (RDY handshake completed)\n");

    /* --- M0: SPI status handshake --- */
    SHELL_PRINTF("  SPI:    STS1=0x%02x STS2=0x%02x  [M0 %s]\n",
                 (unsigned)p.sts1, (unsigned)p.sts2,
                 (p.sts1 == 0xC0u) ? "PASS: SPI talks to the radio"
                                   : "FAIL: no 0xC0 ready status");
    SHELL_PRINTF("  boot:   active-state event %s\n",
                 p.active_evt ? "seen (04 FF 01 01)" : "NOT seen");

    /* --- M1: HCI Reset round-trip --- */
    if (p.cc_seen) {
        SHELL_PRINTF("  HCI:    Reset -> Command Complete, status=0x%02x  "
                     "[M1 %s]\n", (unsigned)p.hci_status,
                     (p.hci_status == 0u) ? "PASS" : "returned error");
    } else {
        SHELL_PRINTF("  HCI:    Reset send_rc=%d recv_rc=%d -- no Command "
                     "Complete  [M1 FAIL]\n", (int)p.send_rc, (int)p.recv_rc);
    }

    /* --- raw last event bytes (for debugging) --- */
    if (p.evt_len) {
        SHELL_PRINTF("  event: ");
        for (i = 0u; i < p.evt_len; i++) {
            SHELL_PRINTF(" %02x", (unsigned)p.evt[i]);
        }
        SHELL_PRINTF("\n");
    }

    SHELL_PRINTF("%s\n", (rc == TIKU_EM9305_OK)
                 ? "ble: first contact OK (M0+M1 green)"
                 : "ble: first contact incomplete -- see above");
}
