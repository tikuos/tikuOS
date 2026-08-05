/*
 * Tiku Operating System v0.06
 *
 * msc_host_test.c - U0: exercise kernel/usb/tiku_usbd_msc.c on a Linux host.
 *
 * Build: tools/usbmsc/Makefile     Run: ./msc_host_test
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * THE POINT OF THIS FILE.  Mass storage is a wire format, and a wire format
 * is nothing but byte offsets and endianness -- the class of thing that is
 * silent when wrong.  A CSW with its residue in the wrong byte order still
 * arrives; a READ CAPACITY off by one still mounts.  The host finds out
 * later, usually while writing a filesystem.
 *
 * So every field this code emits is checked against the offsets the
 * specification names, and every guard is checked against an input that
 * MUST be refused -- including one that a plausible wrong implementation
 * accepts, so the guard is shown to be load-bearing rather than merely
 * present.
 */

#include <stdio.h>
#include <string.h>

#include "kernel/usb/tiku_usbd_msc.h"

static int g_pass, g_fail;

static void ok(int cond, const char *what)
{
    if (cond) { g_pass++; } else { g_fail++; }
    printf("  %s  %s\n", cond ? "pass" : "FAIL", what);
}

static void eq(uint32_t got, uint32_t want, const char *what)
{
    if (got == want) {
        g_pass++;
        printf("  pass  %s\n", what);
    } else {
        g_fail++;
        printf("  FAIL  %s (got 0x%X want 0x%X)\n", what, got, want);
    }
}

/*---------------------------------------------------------------------------*/
/* Helpers: build wrappers the way a host would                              */
/*---------------------------------------------------------------------------*/

/** @brief Lay out a command wrapper exactly as the specification orders it. */
static void make_cbw(uint8_t *b, uint32_t tag, uint32_t len, int dir_in,
                     const uint8_t *cdb, uint8_t cdb_len)
{
    memset(b, 0, TIKU_USBD_MSC_CBW_LEN);
    b[0] = 0x55; b[1] = 0x53; b[2] = 0x42; b[3] = 0x43;   /* "USBC" */
    b[4] = (uint8_t)tag;         b[5] = (uint8_t)(tag >> 8);
    b[6] = (uint8_t)(tag >> 16); b[7] = (uint8_t)(tag >> 24);
    b[8]  = (uint8_t)len;         b[9]  = (uint8_t)(len >> 8);
    b[10] = (uint8_t)(len >> 16); b[11] = (uint8_t)(len >> 24);
    b[12] = dir_in ? 0x80u : 0x00u;
    b[13] = 0u;
    b[14] = cdb_len;
    memcpy(&b[15], cdb, cdb_len);
}

/** @brief Decode one command against a 1 MB medium, returning the decision. */
static void run(tiku_usbd_msc_t *m, const uint8_t *cdb, uint8_t cdb_len,
                uint32_t host_len, int dir_in,
                uint8_t *reply, tiku_usbd_msc_cmd_t *cmd)
{
    uint8_t raw[TIKU_USBD_MSC_CBW_LEN];
    tiku_usbd_msc_cbw_t cbw;

    make_cbw(raw, 0xAABBCCDDu, host_len, dir_in, cdb, cdb_len);
    if (!tiku_usbd_msc_parse_cbw(raw, TIKU_USBD_MSC_CBW_LEN, &cbw)) {
        printf("  FAIL  internal: run() built an invalid CBW\n");
        g_fail++;
        return;
    }
    tiku_usbd_msc_decode(m, &cbw, reply, cmd);
}

/*---------------------------------------------------------------------------*/
/* 1. Command wrapper parsing                                                */
/*---------------------------------------------------------------------------*/

static void test_cbw(void)
{
    uint8_t raw[TIKU_USBD_MSC_CBW_LEN];
    uint8_t cdb[10] = { TIKU_USBD_MSC_READ10 };
    tiku_usbd_msc_cbw_t c;

    printf("\nCBW parsing\n");

    make_cbw(raw, 0x12345678u, 0x00010000u, 1, cdb, 10u);
    ok(tiku_usbd_msc_parse_cbw(raw, TIKU_USBD_MSC_CBW_LEN, &c) == 1,
       "a well-formed wrapper is accepted");
    eq(c.tag, 0x12345678u, "tag is little-endian");
    eq(c.host_len, 0x00010000u, "transfer length is little-endian");
    eq(c.dir_in, 1u, "direction bit is bit 7 of bmCBWFlags");
    eq(c.cdb_len, 10u, "command block length");
    ok(c.cdb == &raw[15], "the command block starts at offset 15");

    make_cbw(raw, 1u, 0u, 0, cdb, 10u);
    ok(tiku_usbd_msc_parse_cbw(raw, TIKU_USBD_MSC_CBW_LEN, &c) == 1,
       "device-to-host cleared parses too");
    eq(c.dir_in, 0u, "host-to-device direction is 0");

    /* The two rejections the specification asks for, and no others. */
    make_cbw(raw, 1u, 0u, 1, cdb, 10u);
    ok(tiku_usbd_msc_parse_cbw(raw, 30u, &c) == 0,
       "a 30-byte wrapper is refused");
    ok(tiku_usbd_msc_parse_cbw(raw, 31u, &c) == 1,
       "...and 31 bytes is the only accepted length");

    make_cbw(raw, 1u, 0u, 1, cdb, 10u);
    raw[0] = 0x56u;                       /* corrupt the signature */
    ok(tiku_usbd_msc_parse_cbw(raw, TIKU_USBD_MSC_CBW_LEN, &c) == 0,
       "a wrong signature is refused");

    ok(tiku_usbd_msc_parse_cbw(NULL, 31u, &c) == 0, "NULL buffer is refused");
    ok(tiku_usbd_msc_parse_cbw(raw, 31u, NULL) == 0, "NULL output is refused");
}

/*---------------------------------------------------------------------------*/
/* 2. Status wrapper                                                         */
/*---------------------------------------------------------------------------*/

static void test_csw(void)
{
    uint8_t w[TIKU_USBD_MSC_CSW_LEN];

    printf("\nCSW layout\n");

    memset(w, 0xEE, sizeof(w));
    tiku_usbd_msc_build_csw(w, 0x11223344u, 0x0000ABCDu, 1u);

    ok(w[0] == 0x55u && w[1] == 0x53u && w[2] == 0x42u && w[3] == 0x53u,
       "signature is \"USBS\" in that byte order");
    ok(w[4] == 0x44u && w[5] == 0x33u && w[6] == 0x22u && w[7] == 0x11u,
       "tag is echoed little-endian");
    ok(w[8] == 0xCDu && w[9] == 0xABu && w[10] == 0x00u && w[11] == 0x00u,
       "residue is little-endian");
    eq(w[12], 1u, "status is the thirteenth byte");
}

/*---------------------------------------------------------------------------*/
/* 3. The range check, and proof that it is load-bearing                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief The plausible wrong version, written out so it can be compared.
 *
 * This is what the check looks like when written the obvious way, and it is
 * what this test exists to rule out.
 */
static int naive_lba_ok(uint32_t blocks, uint32_t lba, uint32_t nblk)
{
    return ((lba + nblk) <= blocks) ? 1 : 0;
}

static void test_lba(void)
{
    tiku_usbd_msc_t m = { 2048u, "RAM Disk", 0u, 0u };   /* 1 MB */

    printf("\nLBA range check\n");

    ok(tiku_usbd_msc_lba_ok(&m, 0u, 2048u) == 1, "the whole medium fits");
    ok(tiku_usbd_msc_lba_ok(&m, 2047u, 1u) == 1, "the last block fits");
    ok(tiku_usbd_msc_lba_ok(&m, 2048u, 1u) == 0, "one past the end is refused");
    ok(tiku_usbd_msc_lba_ok(&m, 0u, 2049u) == 0, "one block too many refused");
    ok(tiku_usbd_msc_lba_ok(&m, 2047u, 2u) == 0,
       "a range straddling the end is refused");
    ok(tiku_usbd_msc_lba_ok(&m, 4000u, 0u) == 1,
       "a zero-length range is allowed wherever it points");

    /*
     * THE CASE THE GUARD EXISTS FOR.  The host controls both numbers, so it
     * can name an LBA near 2^32; the sum then wraps small and the obvious
     * check waves it through.  Showing the naive version ACCEPT it is the
     * only way to know this test would notice if the guard were rewritten.
     */
    printf("\n  the wrap case -- naive and correct must DISAGREE:\n");
    {
        const uint32_t lba = 0xFFFFFF00u, nblk = 0x200u;

        ok(naive_lba_ok(2048u, lba, nblk) == 1,
           "  naive check ACCEPTS lba=0xFFFFFF00 nblk=512 (the bug)");
        ok(tiku_usbd_msc_lba_ok(&m, lba, nblk) == 0,
           "  real check REFUSES it");
        ok(naive_lba_ok(2048u, lba, nblk) != tiku_usbd_msc_lba_ok(&m, lba,
                                                                  nblk),
           "  the two disagree, so the guard is doing work");
    }
}

/*---------------------------------------------------------------------------*/
/* 4. SCSI replies, field by field                                           */
/*---------------------------------------------------------------------------*/

static void test_inquiry(void)
{
    tiku_usbd_msc_t m = { 2048u, "RAM Disk", 0u, 0u };
    uint8_t r[TIKU_USBD_MSC_REPLY_MAX];
    tiku_usbd_msc_cmd_t c;
    uint8_t cdb[6] = { TIKU_USBD_MSC_INQUIRY, 0, 0, 0, 36, 0 };

    printf("\nINQUIRY\n");

    memset(r, 0xEE, sizeof(r));
    run(&m, cdb, 6u, 36u, 1, r, &c);

    eq(c.action, TIKU_USBD_MSC_ACT_REPLY, "answers with a reply");
    eq(c.len, 36u, "36 bytes");
    eq(c.residue, 0u, "residue zero when the host asked for exactly 36");
    eq(r[0], 0x00u, "peripheral type: direct-access block device");
    eq(r[1], 0x80u, "RMB set -- removable, which is what makes hosts mount");
    eq(r[2], 0x04u, "version SPC-2");
    eq(r[3], 0x02u, "response data format 2");
    eq(r[4], 31u, "additional length 31, i.e. 36 total");
    ok(memcmp(&r[8], "TikuOS  ", 8) == 0, "vendor is \"TikuOS  \"");
    ok(memcmp(&r[16], "RAM Disk        ", 16) == 0,
       "product is space padded to 16");
    ok(memcmp(&r[32], "0.06", 4) == 0, "revision is \"0.06\"");

    /* A short allocation length must truncate, not overrun. */
    memset(r, 0xEE, sizeof(r));
    run(&m, cdb, 6u, 8u, 1, r, &c);
    eq(c.len, 8u, "truncated to the host's allocation length");
    eq(c.residue, 0u, "residue zero after truncation to the ask");

    /* The other product string the Apollo510 driver presents. */
    m.product = "eMMC 8GB";
    run(&m, cdb, 6u, 36u, 1, r, &c);
    ok(memcmp(&r[16], "eMMC 8GB        ", 16) == 0,
       "a different product pads identically");

    /* A caller that names nothing still emits a well-formed field. */
    m.product = NULL;
    run(&m, cdb, 6u, 36u, 1, r, &c);
    ok(memcmp(&r[16], "                ", 16) == 0,
       "a NULL product is 16 spaces, not a crash");
}

static void test_sense(void)
{
    tiku_usbd_msc_t m = { 2048u, "RAM Disk", 0u, 0u };
    uint8_t r[TIKU_USBD_MSC_REPLY_MAX];
    tiku_usbd_msc_cmd_t c;
    uint8_t sense[6] = { TIKU_USBD_MSC_REQUEST_SENSE, 0, 0, 0, 18, 0 };
    uint8_t bogus[6] = { 0xDEu, 0, 0, 0, 0, 0 };

    printf("\nREQUEST SENSE and the sense latch\n");

    /* An unsupported opcode must fail AND leave a reason behind. */
    run(&m, bogus, 6u, 0u, 0, r, &c);
    eq(c.status, 1u, "an unknown opcode fails the command");
    eq(c.action, TIKU_USBD_MSC_ACT_NONE, "...with no data phase");
    eq(m.sense_key, TIKU_USBD_MSC_SENSE_ILLEGAL, "sense key ILLEGAL REQUEST");
    eq(m.sense_asc, TIKU_USBD_MSC_ASC_OPCODE, "ASC: invalid opcode");

    /* The host now asks why. */
    memset(r, 0xEE, sizeof(r));
    run(&m, sense, 6u, 18u, 1, r, &c);
    eq(c.len, 18u, "sense data is 18 bytes");
    eq(r[0], 0x70u, "current errors, fixed format");
    eq(r[2], TIKU_USBD_MSC_SENSE_ILLEGAL, "the latched key is reported");
    eq(r[7], 10u, "additional sense length 10");
    eq(r[12], TIKU_USBD_MSC_ASC_OPCODE, "the latched ASC is reported");

    /* And reading it must clear it, or the next command inherits the fault. */
    eq(m.sense_key, TIKU_USBD_MSC_SENSE_NONE, "reading the sense clears it");
    eq(m.sense_asc, 0u, "...including the ASC");

    memset(r, 0xEE, sizeof(r));
    run(&m, sense, 6u, 18u, 1, r, &c);
    eq(r[2], TIKU_USBD_MSC_SENSE_NONE, "a second read reports no sense");
}

static void test_capacity(void)
{
    tiku_usbd_msc_t m = { 2048u, "RAM Disk", 0u, 0u };
    uint8_t r[TIKU_USBD_MSC_REPLY_MAX];
    tiku_usbd_msc_cmd_t c;
    uint8_t cdb[10] = { TIKU_USBD_MSC_READ_CAPACITY10 };

    printf("\nREAD CAPACITY(10)\n");

    run(&m, cdb, 10u, 8u, 1, r, &c);
    eq(c.len, 8u, "eight bytes");

    /*
     * LAST LBA, NOT THE COUNT.  Off by one here gives the host one block
     * more than exists; it finds out by reading past the end, typically
     * while laying down a filesystem.
     */
    eq(((uint32_t)r[0] << 24) | ((uint32_t)r[1] << 16) |
       ((uint32_t)r[2] << 8) | r[3], 2047u,
       "reports the LAST LBA (2047), not the block count (2048)");
    eq(((uint32_t)r[4] << 24) | ((uint32_t)r[5] << 16) |
       ((uint32_t)r[6] << 8) | r[7], 512u,
       "block length 512, big-endian");
}

static void test_modes(void)
{
    tiku_usbd_msc_t m = { 2048u, "RAM Disk", 0u, 0u };
    uint8_t r[TIKU_USBD_MSC_REPLY_MAX];
    tiku_usbd_msc_cmd_t c;
    uint8_t ms6[6]  = { TIKU_USBD_MSC_MODE_SENSE6, 0, 0, 0, 4, 0 };
    uint8_t ms10[10] = { TIKU_USBD_MSC_MODE_SENSE10 };
    uint8_t tur[6]  = { TIKU_USBD_MSC_TEST_UNIT_READY };
    uint8_t ss[6]   = { TIKU_USBD_MSC_START_STOP };
    uint8_t pa[6]   = { TIKU_USBD_MSC_PREVENT_ALLOW };
    uint8_t sc[10]  = { TIKU_USBD_MSC_SYNC_CACHE10 };

    printf("\nMODE SENSE and the no-data commands\n");

    run(&m, ms6, 6u, 4u, 1, r, &c);
    eq(c.len, 4u, "MODE SENSE(6) is four bytes");
    eq(r[0], 3u, "...whose first byte is the mode data length");

    run(&m, ms10, 10u, 8u, 1, r, &c);
    eq(c.len, 8u, "MODE SENSE(10) is eight bytes");
    eq(r[1], 6u, "...with the length in the second byte");

    run(&m, tur, 6u, 0u, 0, r, &c);
    eq(c.action, TIKU_USBD_MSC_ACT_NONE, "TEST UNIT READY has no data phase");
    eq(c.status, 0u, "...and succeeds");
    run(&m, ss, 6u, 0u, 0, r, &c);
    eq(c.status, 0u, "START STOP UNIT succeeds");
    run(&m, pa, 6u, 0u, 0, r, &c);
    eq(c.status, 0u, "PREVENT ALLOW MEDIUM REMOVAL succeeds");
    run(&m, sc, 10u, 0u, 0, r, &c);
    eq(c.status, 0u, "SYNCHRONIZE CACHE succeeds");
}

/*---------------------------------------------------------------------------*/
/* 5. READ/WRITE decode                                                      */
/*---------------------------------------------------------------------------*/

static void test_rw(void)
{
    tiku_usbd_msc_t m = { 2048u, "RAM Disk", 0u, 0u };
    uint8_t r[TIKU_USBD_MSC_REPLY_MAX];
    tiku_usbd_msc_cmd_t c;
    uint8_t cdb[10];

    printf("\nREAD(10) / WRITE(10)\n");

    /* Read 8 blocks starting at LBA 0x40. */
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = TIKU_USBD_MSC_READ10;
    cdb[2] = 0x00; cdb[3] = 0x00; cdb[4] = 0x00; cdb[5] = 0x40;
    cdb[7] = 0x00; cdb[8] = 0x08;
    run(&m, cdb, 10u, 8u * 512u, 1, r, &c);

    eq(c.action, TIKU_USBD_MSC_ACT_READ, "READ(10) asks for a read");
    eq(c.lba, 0x40u, "LBA is big-endian at offset 2");
    eq(c.nblk, 8u, "block count is big-endian at offset 7");
    eq(c.bytes, 8u * 512u, "byte count is blocks x 512");
    eq(c.residue, 0u, "no residue when the host asked for exactly that");
    eq(c.status, 0u, "and it succeeds");

    cdb[0] = TIKU_USBD_MSC_WRITE10;
    run(&m, cdb, 10u, 8u * 512u, 0, r, &c);
    eq(c.action, TIKU_USBD_MSC_ACT_WRITE, "WRITE(10) asks for a write");
    eq(c.lba, 0x40u, "...with the same field layout");

    /* A host that asks for less than the command implies caps the phase. */
    cdb[0] = TIKU_USBD_MSC_READ10;
    run(&m, cdb, 10u, 1024u, 1, r, &c);
    eq(c.bytes, 1024u, "the data phase is clamped to the host's length");
    eq(c.residue, 0u, "residue is zero once clamped to the ask");

    /* Out of range must fail, report the full residue, and set sense. */
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = TIKU_USBD_MSC_READ10;
    cdb[2] = 0x00; cdb[3] = 0x00; cdb[4] = 0x08; cdb[5] = 0x00;  /* 2048 */
    cdb[8] = 0x01;
    run(&m, cdb, 10u, 512u, 1, r, &c);
    eq(c.status, 1u, "reading the first block past the end fails");
    eq(c.action, TIKU_USBD_MSC_ACT_NONE, "...with no data phase");
    eq(c.residue, 512u, "...and the whole transfer as residue");
    eq(m.sense_key, TIKU_USBD_MSC_SENSE_ILLEGAL, "sense key ILLEGAL REQUEST");
    eq(m.sense_asc, TIKU_USBD_MSC_ASC_LBA_RANGE, "ASC: LBA out of range");

    /* The wrap case again, this time through the real decode path. */
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = TIKU_USBD_MSC_WRITE10;
    cdb[2] = 0xFFu; cdb[3] = 0xFFu; cdb[4] = 0xFFu; cdb[5] = 0x00u;
    cdb[7] = 0x02u; cdb[8] = 0x00u;                   /* nblk = 512 */
    run(&m, cdb, 10u, 512u * 512u, 0, r, &c);
    eq(c.status, 1u, "the wrapping range is refused by decode too");
    eq(c.action, TIKU_USBD_MSC_ACT_NONE,
       "...so no pointer is ever handed out for it");
}

int main(void)
{
    printf("tiku_usbd_msc host test\n");

    test_cbw();
    test_csw();
    test_lba();
    test_inquiry();
    test_sense();
    test_capacity();
    test_modes();
    test_rw();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
