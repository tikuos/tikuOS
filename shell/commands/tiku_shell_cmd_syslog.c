/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_syslog.c - "syslog" command (remote log line).
 *
 * Sends one RFC 3164 datagram to the SLIP host (.1 on the device's subnet).
 * Syslog has no reply, so the command sends synchronously and has no
 * per-tick driver.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_syslog.h"
#include "tiku_shell_cmd_slip.h"                     /* slip_enable */
#include <string.h>                                  /* strcmp (net-test) */
#include <shell/tiku_shell.h>                 /* SHELL_PRINTF */
#include <tikukits/net/tiku_kits_net.h>              /* TIKU_KITS_NET_OK */
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>    /* the address in use */
#include <tikukits/net/ipv4/tiku_kits_net_udp.h>     /* udp_init */
#include <tikukits/net/ipv4/tiku_kits_net_syslog.h>

/* Bound on the joined message text.  The syslog kit also cuts the whole
 * datagram to TIKU_KITS_NET_UDP_MAX_PAYLOAD (100 bytes at the default
 * 128-byte MTU), which can leave less than this for the text. */
#define SYSLOG_MSG_MAX  96u

void
tiku_shell_cmd_syslog(uint8_t argc, const char *argv[])
{
    static uint8_t udp_ready;
    const uint8_t *self = tiku_kits_net_ipv4_get_addr();
    uint8_t  server[4];
    char     msg[SYSLOG_MSG_MAX];
    uint8_t  i;
    uint8_t  pos = 0;

    if (argc < 2) {
        SHELL_PRINTF("usage: syslog <message>\n");
        return;
    }

#if TIKU_SHELL_NET_TEST
    /* `syslog @burst` sends the five messages the APP=net syslog process
     * (tiku_kits_net_syslog_process.c) sends at boot: "boot ok" and four
     * clamping and truncation cases.  Each send completes before the next,
     * so the five SLIP frames leave in order; TikuBench's
     * test_syslog_boundary.py checks them. */
    if (argc == 2 && strcmp(argv[1], "@burst") == 0) {
        server[0] = self[0];
        server[1] = self[1];
        server[2] = self[2];
        server[3] = 1u;

        tiku_shell_cmd_slip_enable();
        if (!udp_ready) {
            tiku_kits_net_udp_init();
            udp_ready = 1;
        }
        tiku_kits_net_syslog_init();
        tiku_kits_net_syslog_set_server(server);

        /* 1: plain message  ->  <134>tikuOS os: boot ok */
        tiku_kits_net_syslog_send(TIKU_KITS_NET_SYSLOG_SEV_INFO, "boot ok");
        /* 2: severity 255 clamps to 7/DEBUG  ->  <135>tikuOS os: sev-clamp */
        tiku_kits_net_syslog_send(255, "sev-clamp");
        /* 3: 120 'A's cut to TIKU_KITS_NET_UDP_MAX_PAYLOAD (100 at MTU 128) */
        tiku_kits_net_syslog_send(TIKU_KITS_NET_SYSLOG_SEV_INFO,
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
        /* 4: 16-char hostname cut to 8 -> <134>ABCDEFGH os: host-trunc */
        tiku_kits_net_syslog_set_hostname("ABCDEFGHIJKLMNOP");
        tiku_kits_net_syslog_send(TIKU_KITS_NET_SYSLOG_SEV_INFO, "host-trunc");
        /* 5: 10-char tag cut to 8 -> <134>ABCDEFGH ZYXWVUTS: tag-trunc */
        tiku_kits_net_syslog_set_tag("ZYXWVUTSRQ");
        tiku_kits_net_syslog_send(TIKU_KITS_NET_SYSLOG_SEV_INFO, "tag-trunc");

        SHELL_PRINTF("syslog: boundary burst sent (5 messages)\n");
        return;
    }
#endif

    /* Re-join the tokenised message with single spaces. */
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (i > 1u && pos < (uint8_t)(SYSLOG_MSG_MAX - 1u)) {
            msg[pos++] = ' ';
        }
        while (*a != '\0' && pos < (uint8_t)(SYSLOG_MSG_MAX - 1u)) {
            msg[pos++] = *a++;
        }
    }
    msg[pos] = '\0';

    /* Log to the SLIP host (the device subnet's .1 address). */
    server[0] = self[0];
    server[1] = self[1];
    server[2] = self[2];
    server[3] = 1u;

    tiku_shell_cmd_slip_enable();
    if (!udp_ready) {
        tiku_kits_net_udp_init();
        udp_ready = 1;
    }

    tiku_kits_net_syslog_init();
    tiku_kits_net_syslog_set_server(server);
    if (tiku_kits_net_syslog_send(TIKU_KITS_NET_SYSLOG_SEV_INFO, msg)
            != TIKU_KITS_NET_OK) {
        SHELL_PRINTF("syslog: send failed\n");
        return;
    }
    SHELL_PRINTF("syslog -> %u.%u.%u.%u: %s\n",
                 server[0], server[1], server[2], server[3], msg);
}
