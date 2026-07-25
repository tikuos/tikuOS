/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_https_roots.inl - the HTTPS trust store, loaded from /data.
 *
 * NOT a standalone translation unit.  Included from tiku_basic_https.inl.
 *
 * WHAT CHANGED AND WHY.  This file used to BE the trust store: 8,252 lines of
 * generated C holding 120 CA root certificates, 128,820 bytes of DER, compiled
 * into .rodata.  That turned certificates -- third-party data with expiry dates
 * -- into program text, with two consequences.  It was the single largest thing
 * in the cert-TLS image (307,637 B against a 256 KB code window, 127.7 KB of it
 * this file), and trust could only be changed by reflashing, which is the wrong
 * lifecycle for something that gets revoked.
 *
 * Certificates are DATA, so they are now a named file in the one store:
 * /data/roots.bin, built by tools/gen_roots.py and provisioned over the shell's
 * binary transfer.  Same bytes, same 120 roots -- gen_roots.py --self-test packs
 * this file's former contents and requires the round trip to reproduce every
 * byte, so the migration cannot quietly change what the firmware trusts.
 *
 * NO EMBEDDED FALLBACK, deliberately.  Keeping a .rodata copy "just in case"
 * would mean the blob still sized the code window, which is the entire problem
 * being removed.  A board without roots.bin refuses HTTPS with a named error
 * rather than silently trusting nothing (which reads as a network fault) or
 * anything (which is worse).  Provisioning needs no radio, so a virgin board is
 * always recoverable over the console.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/memory/tiku_nvm_mirror.h"   /* tiku_nvm_crc32 */

/** @brief Store file holding the packed trust store.  Flat name: /data carries
 *  a static VFS node called "basic", so a directory-looking prefix renders as a
 *  phantom folder (the trap prog.bas and mod.bin both hit). */
#define BASIC_HTTPS_ROOTS_FILE   "roots.bin"

#define BASIC_HTTPS_ROOTS_MAGIC  0x54535254u   /* 'TRST' */
#define BASIC_HTTPS_ROOTS_VER    1u
#define BASIC_HTTPS_ROOTS_HDR    28u           /* 7 x u32, see gen_roots.py  */
#define BASIC_HTTPS_ROOTS_ENT    16u           /* 4 x u32 per descriptor     */

/**
 * @brief Descriptor-table capacity.
 *
 * 120 roots ship today; the headroom lets a refreshed bundle grow without a
 * firmware change, which is half the point of moving them out.  Costs
 * BASIC_HTTPS_MAX_ROOTS * sizeof(tiku_kits_crypto_x509_root_t) of .bss
 * (160 * 16 = 2,560 B on a 32-bit part) against 127.7 KB of NVM returned.
 */
#ifndef BASIC_HTTPS_MAX_ROOTS
#define BASIC_HTTPS_MAX_ROOTS    160
#endif

/* Fixed up from the mapped file on every use -- see basic_https_roots_get(). */
static tiku_kits_crypto_x509_root_t basic_https_roots[BASIC_HTTPS_MAX_ROOTS];

/** @brief Read a little-endian u32 without assuming the pointer is aligned. */
static uint32_t
basic_https_rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

/**
 * @brief Map /data/roots.bin, validate it, and fix up the descriptor table.
 *
 * The packed table stores OFFSETS into the DER blob, because the blob's address
 * is only known once the store has mapped it.  Turning those into the pointer
 * pairs the verify kit wants is this function's whole job:
 * tiku_kits_crypto_x509_root_t holds (der, der_len, subject, subject_len) where
 * the subject DN is a slice of the cert it belongs to -- that is what lets
 * anchor matching compare a DN by byte compare without parsing every root.
 *
 * WHY THIS RE-LOADS EVERY TIME rather than caching.  tiku_tfs_map() returns a
 * pointer valid only until the next write or delete of that name, so a table
 * cached across a re-provision would hold pointers into reclaimed slots.
 * Re-mapping costs 120 bounds checks and one CRC against a handshake that does
 * RSA, so the saving was never worth owning that invalidation problem -- and
 * re-provisioned roots now take effect without a reboot.
 *
 * Every field is re-checked even though the CRC passed: a CRC proves the bytes
 * are the ones that were written, not that this packer wrote them.
 *
 * @param out    Receives the fixed-up table (points at static storage).
 * @param nroots Receives the root count.
 * @return 0 on success, -1 if the store, the file, or its contents are unusable.
 */
static int
basic_https_roots_get(const tiku_kits_crypto_x509_root_t **out, int *nroots)
{
    tiku_tfs_t    *fs = tiku_vfs_tree_data_store();
    const void    *p = NULL;
    const uint8_t *img;
    size_t         n = 0u;
    uint32_t       magic, ver, count, der_off, der_len, table_off, crc;
    uint32_t       i;

    if (out == NULL || nroots == NULL) {
        return -1;
    }
    *out = NULL;
    *nroots = 0;

    if (fs == NULL ||
        tiku_tfs_map(fs, BASIC_HTTPS_ROOTS_FILE, &p, &n) != TFS_OK ||
        n < BASIC_HTTPS_ROOTS_HDR) {
        return -1;                          /* no store, or no trust store */
    }
    img       = (const uint8_t *)p;
    magic     = basic_https_rd32(img + 0);
    ver       = basic_https_rd32(img + 4);
    count     = basic_https_rd32(img + 8);
    der_off   = basic_https_rd32(img + 12);
    der_len   = basic_https_rd32(img + 16);
    table_off = basic_https_rd32(img + 20);
    crc       = basic_https_rd32(img + 24);

    if (magic != BASIC_HTTPS_ROOTS_MAGIC || ver != BASIC_HTTPS_ROOTS_VER) {
        return -1;
    }
    if (count == 0u || count > (uint32_t)BASIC_HTTPS_MAX_ROOTS) {
        return -1;                        /* empty, or more than we can hold */
    }
    /* Geometry: header, blob, table, ending exactly at EOF.  Phrased as
     * subtractions throughout so no bound can wrap at any integer width. */
    if (der_off != BASIC_HTTPS_ROOTS_HDR ||
        der_len > (uint32_t)n - der_off ||
        table_off < der_off + der_len ||
        table_off > (uint32_t)n ||
        count > ((uint32_t)n - table_off) / BASIC_HTTPS_ROOTS_ENT ||
        table_off + count * BASIC_HTTPS_ROOTS_ENT != (uint32_t)n) {
        return -1;
    }
    if (tiku_nvm_crc32(img + der_off, n - der_off) != crc) {
        return -1;                          /* bit rot or a torn provision */
    }

    for (i = 0u; i < count; i++) {
        const uint8_t *e = img + table_off + i * BASIC_HTTPS_ROOTS_ENT;
        uint32_t do_ = basic_https_rd32(e + 0);
        uint32_t dl  = basic_https_rd32(e + 4);
        uint32_t so  = basic_https_rd32(e + 8);
        uint32_t sl  = basic_https_rd32(e + 12);

        /* The cert must lie inside the blob and its subject DN inside the cert;
         * the verify kit dereferences both without re-checking. */
        if (dl == 0u || sl == 0u ||
            do_ > der_len || dl > der_len - do_ ||
            so < do_ || so - do_ > dl || sl > dl - (so - do_)) {
            return -1;
        }
        basic_https_roots[i].der         = img + der_off + do_;
        basic_https_roots[i].der_len     = (size_t)dl;
        basic_https_roots[i].subject     = img + der_off + so;
        basic_https_roots[i].subject_len = (size_t)sl;
    }

    *out = basic_https_roots;
    *nroots = (int)count;
    return 0;
}
