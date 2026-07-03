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
#include <kernel/shell/tiku_shell_io.h>       /* io-backend swap, rx_ready/getc */
#include <kernel/shell/tiku_shell_parser.h>   /* tiku_shell_parser_execute      */
#include <kernel/shell/tiku_shell_cwd.h>      /* tiku_shell_cwd_get (prompt)    */
#include <kernel/timers/tiku_clock.h>         /* tiku_clock_time (heartbeat)   */
#include <arch/ambiq/tiku_em9305.h>
#include <arch/ambiq/tiku_ble_nus.h>
#include <string.h>

/** Ctrl+C / ETX -- stops an interactive BLE session. */
#define BLE_CANCEL 0x03

/* Shell io-backend routing console traffic over the Nordic UART Service:
 * output -> TX notifications, input <- RX writes. \n expands to \r\n for the
 * phone's terminal view; no local echo (the phone shows what it sent). */
static const tiku_shell_io_t s_ble_io = {
    tiku_ble_nus_putc,        /* putc     */
    tiku_ble_nus_rx_ready,    /* rx_ready */
    tiku_ble_nus_getc,        /* getc     */
    TIKU_SHELL_IO_CRLF        /* flags    */
};

/* Busy-wait @p ticks system ticks, pumping the BLE stack so the controller's
 * completed-packet acks (and any RX) are serviced while we wait. */
static void ble_uart_settle(uint8_t ticks) {
    tiku_clock_time_t t = (tiku_clock_time_t)(tiku_clock_time() + ticks);
    while (TIKU_CLOCK_LT(tiku_clock_time(), t)) {
        (void)tiku_ble_nus_poll();
    }
}

/* Push buffered shell output to the phone as paced TX notifications so a burst
 * (e.g. `ps`) does not overrun the controller's ACL buffers. */
static void ble_uart_drain_tx(void) {
    int guard = 128;
    while (tiku_ble_nus_tx_pending() > 0u && guard-- > 0) {
        tiku_ble_nus_flush();
        ble_uart_settle(2u);      /* ~16 ms between notifications */
    }
}

/* Route the shell's output to BLE, print @p emit (banner/prompt) and/or execute
 * @p line, then restore the UART backend and flush the notifications. */
static void ble_uart_to_ble(const tiku_shell_io_t *uart_be,
                            const char *emit, char *line) {
    tiku_shell_io_set_backend(&s_ble_io);
    if (emit != (const char *)0) {
        tiku_shell_io_puts(emit);
    }
    if (line != (char *)0 && line[0] != '\0') {
        tiku_shell_parser_execute(line);
    }
    tiku_shell_io_printf("tikuOS:%s> ", tiku_shell_cwd_get());
    tiku_shell_io_set_backend(uart_be);
    ble_uart_drain_tx();
}

/**
 * @brief "ble uart [name]" -- interactive tikuOS shell over BLE (M3.3).
 *
 * Advertises connectably; when a central connects and subscribes to TX
 * notifications, its RX writes become shell input and shell output is streamed
 * back as TX notifications -- a wireless console in nRF Connect's UART tab.
 * Ctrl-C on the local UART stops the session.
 */
static void ble_cmd_uart(uint8_t argc, const char *argv[]) {
    const char *name = (argc >= 3u) ? argv[2] : "tikuOS";
    const tiku_shell_io_t *uart_be = tiku_shell_io_get_backend();
    static char line[128];
    uint16_t lpos = 0u;
    uint8_t  greeted = 0u;
    tiku_clock_time_t beat;
    int rc;

    rc = tiku_ble_nus_start(name);
    if (rc != TIKU_EM9305_OK) {
        static const char *const names[5] = {
            "Reset", "EvtMask", "AdvParams", "AdvData", "AdvEnable"
        };
        int8_t  srcs[5];
        uint8_t ssts[5];
        uint8_t n = tiku_ble_nus_start_steps(srcs, ssts, 5u);
        uint8_t i;
        SHELL_PRINTF("ble uart: start FAILED (rc=%d)\n", rc);
        for (i = 0u; i < n; i++) {
            SHELL_PRINTF("  step %u %-9s: rc=%d status=0x%02x\n",
                         (unsigned)i, names[i], (int)srcs[i], (unsigned)ssts[i]);
        }
        return;
    }
    SHELL_PRINTF("ble: advertising as \"%s\" (connectable)\n", name);
    SHELL_PRINTF("     connect in nRF Connect, open its UART view, then type "
                 "commands.\n     Ctrl-C here stops the wireless shell.\n");

    beat = (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND);
    for (;;) {
        int ev = tiku_ble_nus_poll();

        if (ev == TIKU_BLE_EVT_CONNECTED) {
            SHELL_PRINTF("\nble: CONNECTED\n");
            greeted = 0u;
            lpos = 0u;
        } else if (ev == TIKU_BLE_EVT_DISCONNECTED) {
            SHELL_PRINTF("\nble: DISCONNECTED -- re-advertising\n");
            greeted = 0u;
            lpos = 0u;
        }

        /* Greet once the peer subscribes to TX notifications (CCCD enabled). */
        if (!greeted && tiku_ble_nus_connected() && tiku_ble_nus_notify_enabled()) {
            ble_uart_to_ble(uart_be,
                            "\r\ntikuOS wireless shell -- type 'help'\r\n", 0);
            greeted = 1u;
            SHELL_PRINTF("ble: wireless shell active (subscriber attached)\n");
        }

        /* Feed NUS RX into the line buffer; execute on newline. */
        while (tiku_ble_nus_rx_ready()) {
            int c = tiku_ble_nus_getc();
            if (c < 0) {
                break;
            }
            if (c == '\r' || c == '\n') {
                line[lpos] = '\0';
                lpos = 0u;
                ble_uart_to_ble(uart_be, 0, line);
            } else if (c == 0x08 || c == 0x7F) {          /* backspace */
                if (lpos > 0u) {
                    lpos--;
                }
            } else if (lpos < (uint16_t)(sizeof(line) - 1u)) {
                line[lpos++] = (char)c;
            }
        }

        /* Local Ctrl-C on the UART console stops the session (read the UART
         * backend directly -- the active backend flips to BLE during exec). */
        if (uart_be && uart_be->rx_ready && uart_be->getc &&
            uart_be->rx_ready()) {
            if (uart_be->getc() == BLE_CANCEL) {
                break;
            }
        }

        /* Heartbeat dot once a second while idle-advertising. */
        if (TIKU_CLOCK_LT(beat, tiku_clock_time())) {
            if (!tiku_ble_nus_connected()) {
                SHELL_PRINTF(".");
            }
            beat = (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND);
        }
    }

    tiku_shell_io_set_backend(uart_be);   /* make sure the console is restored */
    tiku_ble_nus_stop();
    SHELL_PRINTF("\nble: session stopped.\n");
}

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

    /* "ble uart [name]" -- connectable serial-over-GATT session (M3). */
    if (argc >= 2u && strcmp(argv[1], "uart") == 0) {
        ble_cmd_uart(argc, argv);
        return;
    }

    /* "ble beacon [name]" -- reset, configure LE advertising, enable it. */
    if (argc >= 2u && strcmp(argv[1], "beacon") == 0) {
        const char *name = (argc >= 3u) ? argv[2] : "tikuOS";
        tiku_em9305_beacon_t b;
        int brc = tiku_em9305_beacon(name, &b);
        SHELL_PRINTF("EM9305 beacon setup (reset + LE advertising)...\n");
        if (b.init_rc != TIKU_EM9305_OK) {
            SHELL_PRINTF("  reset FAIL (rc=%d)\n", b.init_rc);
            return;
        }
        SHELL_PRINTF("  HCI status: Reset=0x%02x AdvParams=0x%02x "
                     "AdvData=0x%02x AdvEnable=0x%02x\n",
                     (unsigned)b.st_reset, (unsigned)b.st_params,
                     (unsigned)b.st_data, (unsigned)b.st_enable);
        SHELL_PRINTF("%s\n", (brc == TIKU_EM9305_OK)
                     ? "ble: ADVERTISING -- scan for the name with nRF Connect"
                     : "ble: beacon setup failed -- see statuses above");
        if (brc == TIKU_EM9305_OK) {
            SHELL_PRINTF("      advertising as \"%s\" (non-connectable, 100ms)\n",
                         name);
        }
        return;
    }

    /* "ble stop" -- disable advertising. */
    if (argc >= 2u && strcmp(argv[1], "stop") == 0) {
        int src = tiku_em9305_beacon_stop();
        SHELL_PRINTF("ble: advertising %s\n",
                     (src == TIKU_EM9305_OK) ? "stopped" : "stop FAILED");
        return;
    }

    /* default: M0/M1 first-contact probe. */
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
