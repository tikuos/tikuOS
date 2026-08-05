/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bigblob.c - one very large object per slot, on erase-block media.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <kernel/fs/tiku_bigblob.h>
#include <kernel/memory/tiku_nvm_mirror.h>

/*
 * HEADER LAST, AND THAT IS THE WHOLE DURABILITY STORY.
 *
 * Erasing the header first makes the slot read as empty for the entire
 * minutes-long payload write, so a power cut anywhere in the middle leaves
 * "no blob" rather than "a blob that is partly the old one and partly the
 * new".  The magic word going down last is what publishes it, and the CRC
 * beside it is what makes the publication checkable rather than merely
 * present -- the same gate-last discipline the persist cells use, at a
 * different scale.
 */
#define BIGBLOB_MAGIC   0x424C4232UL      /* "BLB2" */

typedef struct {
    uint32_t magic;
    uint32_t len;
    uint32_t crc;
    uint32_t reserved;
    char     name[TIKU_BIGBLOB_NAME_MAX + 1u];
} bigblob_hdr_t;

_Static_assert(sizeof(bigblob_hdr_t) <= TIKU_BIGBLOB_HDR_BYTES,
               "header must fit inside its own erase block");

/** @brief The header as it sits in the mapped medium, or NULL if unusable. */
static const bigblob_hdr_t *hdr_at(tiku_nvm_backend_t *be, uint32_t slot_off)
{
    const bigblob_hdr_t *h;

    if (be == NULL || be->base == NULL) {
        return NULL;
    }
    if ((uint32_t)be->size < slot_off ||
        ((uint32_t)be->size - slot_off) < TIKU_BIGBLOB_HDR_BYTES) {
        return NULL;
    }
    h = (const bigblob_hdr_t *)(const void *)(be->base + slot_off);
    if (h->magic != BIGBLOB_MAGIC) {
        return NULL;
    }
    /* A length that runs off the end means a header from a different layout,
     * not a blob; refusing here keeps every caller's pointer arithmetic
     * inside the medium. */
    if (h->len > ((uint32_t)be->size - slot_off - TIKU_BIGBLOB_HDR_BYTES)) {
        return NULL;
    }
    return h;
}

int tiku_bigblob_write(tiku_nvm_backend_t *be, uint32_t slot_off,
                       const char *name, const void *src, uint32_t len)
{
    bigblob_hdr_t h;
    uint32_t payload = slot_off + TIKU_BIGBLOB_HDR_BYTES;
    size_t n;

    if (be == NULL || be->write == NULL || be->erase == NULL ||
        src == NULL || name == NULL || len == 0U) {
        return TIKU_BIGBLOB_ERR_PARAM;
    }
    n = strlen(name);
    if (n > TIKU_BIGBLOB_NAME_MAX) {
        return TIKU_BIGBLOB_ERR_PARAM;
    }
    if ((uint32_t)be->size < payload ||
        ((uint32_t)be->size - payload) < len) {
        return TIKU_BIGBLOB_ERR_SPACE;
    }

    /* 1. Unpublish.  From here until the last step the slot reads as empty. */
    if (be->erase(be, slot_off, TIKU_BIGBLOB_HDR_BYTES) != 0) {
        return TIKU_BIGBLOB_ERR_IO;
    }

    /* 2. Erase and write the payload. */
    if (be->erase(be, payload, len) != 0) {
        return TIKU_BIGBLOB_ERR_IO;
    }
    if (be->write(be, payload, src, len) != 0) {
        return TIKU_BIGBLOB_ERR_IO;
    }

    /* 3. Publish, with the checksum of what was actually written -- taken
     *    from the MEDIUM, not from the caller's buffer, so a write that
     *    landed wrong is caught here rather than at the next boot. */
    memset(&h, 0, sizeof(h));
    h.magic = BIGBLOB_MAGIC;
    h.len   = len;
    h.crc   = tiku_nvm_crc32(be->base + payload, len);
    memcpy(h.name, name, n);
    if (be->write(be, slot_off, &h, sizeof(h)) != 0) {
        return TIKU_BIGBLOB_ERR_IO;
    }
    return TIKU_BIGBLOB_OK;
}

int tiku_bigblob_info(tiku_nvm_backend_t *be, uint32_t slot_off,
                      tiku_bigblob_info_t *out)
{
    const bigblob_hdr_t *h = hdr_at(be, slot_off);

    if (out == NULL) {
        return TIKU_BIGBLOB_ERR_PARAM;
    }
    if (h == NULL) {
        return TIKU_BIGBLOB_ERR_NOENT;
    }
    out->len = h->len;
    out->crc = h->crc;
    memcpy(out->name, h->name, sizeof(out->name));
    out->name[TIKU_BIGBLOB_NAME_MAX] = '\0';
    return TIKU_BIGBLOB_OK;
}

const void *tiku_bigblob_map(tiku_nvm_backend_t *be, uint32_t slot_off,
                             uint32_t *len)
{
    const bigblob_hdr_t *h = hdr_at(be, slot_off);

    if (h == NULL) {
        return NULL;
    }
    if (len != NULL) {
        *len = h->len;
    }
    return (const void *)(be->base + slot_off + TIKU_BIGBLOB_HDR_BYTES);
}

int tiku_bigblob_verify(tiku_nvm_backend_t *be, uint32_t slot_off)
{
    const bigblob_hdr_t *h = hdr_at(be, slot_off);
    uint32_t got;

    if (h == NULL) {
        return TIKU_BIGBLOB_ERR_NOENT;
    }
    got = tiku_nvm_crc32(be->base + slot_off + TIKU_BIGBLOB_HDR_BYTES,
                         h->len);
    return (got == h->crc) ? TIKU_BIGBLOB_OK : TIKU_BIGBLOB_ERR_CRC;
}
