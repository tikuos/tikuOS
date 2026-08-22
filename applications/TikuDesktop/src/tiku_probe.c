/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_probe.c - headless exercise of the session and namespace layers.
 *
 * Everything the desktop will do minus the pixels: connect, mirror the
 * namespace, read, write, subscribe and watch.  Runs against any board, so S0
 * is testable long before there is a window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_ns.h"
#include "tiku_session.h"
#include "tiku_tx.h"

static void
usage(void)
{
    fprintf(stderr,
            "usage: tiku-desk-probe [-p PORT] [-b BAUD] [-t HOST:PORT] "
            "[-w PATH=VALUE] [-s PATH] [-n SECONDS] [-v]\n"
            "  -p  serial device (default: first TikuOS-looking by-id link)\n"
            "  -b  baud (default 115200; MSP430 boards want 9600)\n"
            "  -t  TCP endpoint instead of serial\n"
            "  -w  write a value, then read it back\n"
            "  -s  subscribe and watch this node\n"
            "  -n  seconds to watch after subscribing (default 10)\n");
}

static void
print_tree(const tiku_ns_t *ns, const char *path, int indent, int budget)
{
    const tiku_node_t *kids[64];
    int n = tiku_ns_children(ns, path, kids, 64);
    int i;

    for (i = 0; i < n && budget > 0; i++) {
        const tiku_node_t *k = kids[i];
        printf("%*s%s%s%s%s\n", indent, "", k->name,
               k->is_dir ? "/" : "",
               (k->perm & TIKU_NS_P_WRITE) ? "  [w]" : "",
               (strcmp(k->cap, "-") != 0) ? k->cap : "");
        if (k->is_dir && indent < 4) {
            print_tree(ns, k->path, indent + 2, budget - 1);
        }
    }
}

int
main(int argc, char **argv)
{
    const char *port = NULL, *tcp = NULL, *wr = NULL, *sub = NULL;
    int baud = 115200, secs = 10, verbose = 0, i;
    char scan[8][256];
    tiku_tx_t *tx;
    tiku_session_t *s;
    tiku_ns_t *ns;
    char buf[4096];
    int count;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)      { port = argv[++i]; }
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) { baud = atoi(argv[++i]); }
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) { tcp = argv[++i]; }
        else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) { wr = argv[++i]; }
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) { sub = argv[++i]; }
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) { secs = atoi(argv[++i]); }
        else if (strcmp(argv[i], "-v") == 0)                 { verbose = 1; }
        else { usage(); return 2; }
    }

    if (tcp != NULL) {
        char host[128];
        const char *colon = strrchr(tcp, ':');
        int tport = (colon != NULL) ? atoi(colon + 1) : 23;
        size_t hl = (colon != NULL) ? (size_t)(colon - tcp) : strlen(tcp);
        if (hl >= sizeof host) { hl = sizeof host - 1u; }
        memcpy(host, tcp, hl);
        host[hl] = '\0';
        tx = tiku_tx_open_tcp(host, tport);
    } else {
        if (port == NULL) {
            int found = tiku_tx_scan_serial(scan, 8);
            if (found <= 0) {
                fprintf(stderr, "probe: no serial device found; pass -p\n");
                return 1;
            }
            port = scan[0];
            printf("port      : %s (auto)\n", port);
        }
        tx = tiku_tx_open_serial(port, baud);
    }
    if (tx == NULL) {
        perror("probe: open");
        return 1;
    }

    s = tiku_session_new(tx);
    if (s == NULL) {
        fprintf(stderr, "probe: session\n");
        return 1;
    }
    if (tiku_session_sync(s, 4000) != 0) {
        fprintf(stderr, "probe: no prompt from %s -- wrong baud, or the "
                        "board is busy\n", tiku_session_name(s));
        tiku_session_free(s);
        return 1;
    }
    printf("connected : %s\n", tiku_session_name(s));

    if (tiku_session_cmd(s, "info", buf, sizeof buf, 4000) > 0) {
        char *nl = strchr(buf, '\n');
        if (nl != NULL) { *nl = '\0'; }
        printf("device    : %s\n", buf);
    }

    ns = tiku_ns_new(s);
    count = tiku_ns_load(ns);
    if (count < 0) {
        fprintf(stderr, "probe: no %s on this build\n", "/sys/vfs/manifest");
        tiku_ns_free(ns);
        tiku_session_free(s);
        return 1;
    }
    printf("manifest  : %d nodes%s\n", count,
           tiku_ns_truncated(ns) ? " (CUT by the device read buffer)"
                                      : "");
    if (tiku_ns_truncated(ns)) {
        int added = tiku_ns_complete(ns, 4);
        printf("completed : +%d nodes by ls-walk (no descriptors on those)\n",
               added);
        count = tiku_ns_count(ns);
    }
    {
        int dirs = 0, writable = 0, capped = 0;
        for (i = 0; i < count; i++) {
            const tiku_node_t *n = tiku_ns_at(ns, i);
            if (n->is_dir) { dirs++; }
            if (n->perm & TIKU_NS_P_WRITE) { writable++; }
            if (strcmp(n->cap, "-") != 0) { capped++; }
        }
        printf("            %d dirs, %d writable, %d capability-gated\n",
               dirs, writable, capped);
    }

    if (verbose) {
        printf("\ntree:\n");
        print_tree(ns, "/", 2, 3);
    }

    /* A read the UI will make on every board: uptime proves cat + parse. */
    if (tiku_ns_read(ns, "/sys/uptime") == 0) {
        const tiku_node_t *n = tiku_ns_find(ns, "/sys/uptime");
        printf("uptime    : %s\n", n->value);
    }

    {
        char names[32][64];
        int n = tiku_ns_ls(ns, "/data", names, 32);
        if (n >= 0) {
            printf("/data     : %d entries", n);
            for (i = 0; i < n && i < 4; i++) {
                printf("%s%s", (i == 0) ? " (" : ", ", names[i]);
            }
            printf("%s\n", (n > 0) ? ")" : "");
        }
    }

    if (wr != NULL) {
        char path[TIKU_NS_PATH_MAX], err[256];
        const char *eq = strchr(wr, '=');
        if (eq == NULL) {
            fprintf(stderr, "probe: -w wants PATH=VALUE\n");
        } else {
            size_t pl = (size_t)(eq - wr);
            if (pl >= sizeof path) { pl = sizeof path - 1u; }
            memcpy(path, wr, pl);
            path[pl] = '\0';
            if (tiku_ns_write(ns, path, eq + 1, err, sizeof err) == 0) {
                const tiku_node_t *n = tiku_ns_find(ns, path);
                printf("write     : %s <- %s (now %s)\n", path, eq + 1,
                       (n != NULL && n->value_valid) ? n->value : "?");
            } else {
                printf("write     : refused -- %s\n", err);
            }
        }
    }

    if (sub != NULL) {
        const tiku_node_t *n;
        int rc = tiku_ns_subscribe(ns, sub);
        unsigned gen0;

        if (rc < 0) {
            fprintf(stderr, "probe: cannot subscribe %s\n", sub);
        } else {
            printf("subscribe : %s (%s)\n", sub,
                   (rc == 0) ? "push via sub" : "polling -- no `sub` command");
            (void)tiku_ns_read(ns, sub);
            n = tiku_ns_find(ns, sub);
            gen0 = (n != NULL) ? n->generation : 0u;
            printf("watching  : %d s -- change the value elsewhere\n", secs);
            for (i = 0; i < secs * 4; i++) {
                if (tiku_ns_pump(ns, 250) < 0) {
                    printf("            link lost; reconnecting\n");
                    if (tiku_session_reconnect(s) != 0) { break; }
                }
                n = tiku_ns_find(ns, sub);
                if (n != NULL && n->generation != gen0) {
                    printf("            [%2d.%1ds] %s = %s\n", i / 4, (i % 4) * 25 / 10,
                           sub, n->value);
                    gen0 = n->generation;
                }
            }
            (void)tiku_ns_unsubscribe(ns, sub);
        }
    }

    printf("push      : %s\n",
           tiku_ns_has_push(ns) ? "yes (`sub` present)"
                                     : "no (poll fallback)");
    tiku_ns_free(ns);
    tiku_session_free(s);
    return 0;
}
