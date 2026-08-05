/*
 * Tiku Operating System v0.06
 *
 * msc_diff_test.c - U0: prove the extraction changed no behaviour.
 *
 * Build: tools/usbmsc/Makefile     Run: ./msc_diff_test
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * THE GATE THIS DISCHARGES.  U0 moved proven code out of a driver that only
 * an Apollo510 can exercise.  "It still compiles" is not evidence, and "the
 * new code passes its own tests" only says the new code agrees with itself.
 * What is wanted is that the extraction answers every input exactly as the
 * shipped version did -- so the shipped version is kept as an oracle and both
 * are run over the same space.
 *
 * Coverage is exhaustive where it can be: every one of the 256 opcodes, at
 * every interesting transfer length, in both sense states and both product
 * configurations, plus a directed sweep of READ/WRITE block ranges including
 * the ones that wrap 32 bits.  Anything the two disagree about is printed
 * with the input that caused it.
 */

#include <stdio.h>
#include <string.h>

#include "kernel/usb/tiku_usbd_msc.h"
#include "ref_original.h"

static unsigned long g_cases;
static unsigned      g_diffs;

static void differ(const char *what, const uint8_t *cdb, uint32_t host_len,
                   unsigned long a, unsigned long b)
{
    if (g_diffs < 12u) {
        printf("  DIFF %-10s op=%02X lba=%02X%02X%02X%02X nblk=%02X%02X"
               " host_len=%-8u old=%lu new=%lu\n",
               what, cdb[0], cdb[2], cdb[3], cdb[4], cdb[5], cdb[7], cdb[8],
               host_len, a, b);
    }
    g_diffs++;
}

/*---------------------------------------------------------------------------*/
/* One case: same command, both implementations, every field compared        */
/*---------------------------------------------------------------------------*/

static void one(const uint8_t *cdb, uint32_t host_len, int emmc,
                uint8_t sense_key, uint8_t sense_asc)
{
    tiku_usbd_msc_t m;
    tiku_usbd_msc_cbw_t cbw;
    tiku_usbd_msc_cmd_t neu;
    ref_cmd_t old;
    uint8_t new_reply[TIKU_USBD_MSC_REPLY_MAX];
    uint8_t raw[TIKU_USBD_MSC_CBW_LEN];
    unsigned i;

    g_cases++;

    /* Put both into the same starting state. */
    ref_msc_blocks     = 2048u;
    ref_store_is_emmc  = emmc;
    ref_sense_key      = sense_key;
    ref_sense_asc      = sense_asc;
    ref_bot_status     = 0u;
    memset(ref_reply, 0xEE, sizeof(ref_reply));

    m.blocks    = 2048u;
    m.product   = emmc ? "eMMC 8GB" : "RAM Disk";
    m.sense_key = sense_key;
    m.sense_asc = sense_asc;
    memset(new_reply, 0xEE, sizeof(new_reply));

    /* The old decoder took a bare CDB; the new one takes a parsed wrapper. */
    memset(raw, 0, sizeof(raw));
    raw[0] = 0x55; raw[1] = 0x53; raw[2] = 0x42; raw[3] = 0x43;
    raw[8]  = (uint8_t)host_len;         raw[9]  = (uint8_t)(host_len >> 8);
    raw[10] = (uint8_t)(host_len >> 16); raw[11] = (uint8_t)(host_len >> 24);
    raw[14] = 10u;
    memcpy(&raw[15], cdb, 10);
    if (!tiku_usbd_msc_parse_cbw(raw, TIKU_USBD_MSC_CBW_LEN, &cbw)) {
        printf("  DIFF internal: oracle harness built an invalid CBW\n");
        g_diffs++;
        return;
    }

    ref_msc_scsi(cdb, host_len, &old);
    tiku_usbd_msc_decode(&m, &cbw, new_reply, &neu);

    if ((unsigned)old.action != (unsigned)neu.action) {
        differ("action", cdb, host_len, old.action, neu.action);
    }
    if (old.lba != neu.lba)         { differ("lba", cdb, host_len,
                                             old.lba, neu.lba); }
    if (old.nblk != neu.nblk)       { differ("nblk", cdb, host_len,
                                             old.nblk, neu.nblk); }
    if (old.bytes != neu.bytes)     { differ("bytes", cdb, host_len,
                                             old.bytes, neu.bytes); }
    if (old.residue != neu.residue) { differ("residue", cdb, host_len,
                                             old.residue, neu.residue); }
    if (old.len != neu.len)         { differ("len", cdb, host_len,
                                             old.len, neu.len); }
    if (old.status != neu.status)   { differ("status", cdb, host_len,
                                             old.status, neu.status); }

    /* The sense latch is state, and both must have left it the same way. */
    if (ref_sense_key != m.sense_key) {
        differ("sense_key", cdb, host_len, ref_sense_key, m.sense_key);
    }
    if (ref_sense_asc != m.sense_asc) {
        differ("sense_asc", cdb, host_len, ref_sense_asc, m.sense_asc);
    }

    /* And the reply bytes themselves, out to whatever length was produced. */
    for (i = 0u; i < old.len && i < TIKU_USBD_MSC_REPLY_MAX; i++) {
        if (ref_reply[i] != new_reply[i]) {
            differ("reply byte", cdb, host_len, ref_reply[i], new_reply[i]);
            break;
        }
    }
}

/*---------------------------------------------------------------------------*/

int main(void)
{
    static const uint32_t lens[] = {
        0u, 1u, 4u, 8u, 13u, 17u, 18u, 35u, 36u, 37u, 63u, 512u, 1024u,
        4096u, 65535u, 0x100000u, 0xFFFFFFFFu
    };
    static const uint32_t lbas[] = {
        0u, 1u, 1023u, 2046u, 2047u, 2048u, 2049u, 0x10000u,
        0x7FFFFFFFu, 0x80000000u, 0xFFFFFF00u, 0xFFFFFFFFu
    };
    static const uint32_t nblks[] = { 0u, 1u, 2u, 8u, 255u, 256u, 2048u,
                                      2049u, 0xFFFFu };
    uint8_t cdb[10];
    unsigned op, li, bi, ni, emmc, sense;
    uint8_t csw_old[TIKU_USBD_MSC_CSW_LEN], csw_new[TIKU_USBD_MSC_CSW_LEN];

    printf("tiku_usbd_msc differential test"
           " (extraction vs the code it replaced)\n");

    /* 1. Every opcode, every length, both stores, clean and dirty sense. */
    for (op = 0u; op < 256u; op++) {
        for (li = 0u; li < sizeof(lens) / sizeof(lens[0]); li++) {
            for (emmc = 0u; emmc < 2u; emmc++) {
                for (sense = 0u; sense < 2u; sense++) {
                    memset(cdb, 0, sizeof(cdb));
                    cdb[0] = (uint8_t)op;
                    /* Non-zero payload in the unused CDB bytes, so a field
                     * read from the wrong offset shows up as a difference
                     * rather than as two zeroes agreeing. */
                    cdb[1] = 0x5Au; cdb[9] = 0xA5u;
                    one(cdb, lens[li], (int)emmc,
                        sense ? 0x06u : 0x00u, sense ? 0x28u : 0x00u);
                }
            }
        }
    }

    /* 2. READ and WRITE across the block ranges that matter. */
    for (bi = 0u; bi < sizeof(lbas) / sizeof(lbas[0]); bi++) {
        for (ni = 0u; ni < sizeof(nblks) / sizeof(nblks[0]); ni++) {
            for (li = 0u; li < sizeof(lens) / sizeof(lens[0]); li++) {
                for (op = 0u; op < 2u; op++) {
                    uint32_t lba = lbas[bi], nblk = nblks[ni];
                    memset(cdb, 0, sizeof(cdb));
                    cdb[0] = op ? TIKU_USBD_MSC_WRITE10
                                : TIKU_USBD_MSC_READ10;
                    cdb[2] = (uint8_t)(lba >> 24);
                    cdb[3] = (uint8_t)(lba >> 16);
                    cdb[4] = (uint8_t)(lba >> 8);
                    cdb[5] = (uint8_t)lba;
                    cdb[7] = (uint8_t)(nblk >> 8);
                    cdb[8] = (uint8_t)nblk;
                    one(cdb, lens[li], 0, 0u, 0u);
                }
            }
        }
    }

    /* 3. The status wrapper, over the same field values. */
    for (bi = 0u; bi < sizeof(lbas) / sizeof(lbas[0]); bi++) {
        for (li = 0u; li < sizeof(lens) / sizeof(lens[0]); li++) {
            uint8_t st = (uint8_t)(li & 1u);
            g_cases++;
            memset(csw_old, 0xEE, sizeof(csw_old));
            memset(csw_new, 0xEE, sizeof(csw_new));
            ref_build_csw(csw_old, lbas[bi], lens[li], st);
            tiku_usbd_msc_build_csw(csw_new, lbas[bi], lens[li], st);
            if (memcmp(csw_old, csw_new, TIKU_USBD_MSC_CSW_LEN) != 0) {
                printf("  DIFF csw tag=%08X residue=%08X\n",
                       lbas[bi], lens[li]);
                g_diffs++;
            }
        }
    }

    /* 4. The range check on its own, over the full grid. */
    for (bi = 0u; bi < sizeof(lbas) / sizeof(lbas[0]); bi++) {
        for (ni = 0u; ni < sizeof(nblks) / sizeof(nblks[0]); ni++) {
            tiku_usbd_msc_t m = { 2048u, "RAM Disk", 0u, 0u };
            int a, b;
            g_cases++;
            ref_msc_blocks = 2048u;
            a = ref_msc_lba_ok(lbas[bi], nblks[ni]);
            b = tiku_usbd_msc_lba_ok(&m, lbas[bi], nblks[ni]);
            if (a != b) {
                printf("  DIFF lba_ok lba=%08X nblk=%u old=%d new=%d\n",
                       lbas[bi], nblks[ni], a, b);
                g_diffs++;
            }
        }
    }

    printf("\n%lu cases compared, %u differences\n", g_cases, g_diffs);
    if (g_diffs == 0u) {
        printf("the extraction is behaviour-identical to the code it"
               " replaced\n");
    }
    return g_diffs ? 1 : 0;
}
