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
    if (slot_off % TIKU_BIGBLOB_HDR_BYTES != 0 || be->size < slot_off ||
        (be->size - slot_off) < TIKU_BIGBLOB_HDR_BYTES) {
        return NULL;
    }
    h = (const bigblob_hdr_t *)(const void *)(be->base + slot_off);
    if (h->magic != BIGBLOB_MAGIC) {
        return NULL;
    }
    /* A length that runs off the end means a header from a different layout,
     * not a blob; refusing here keeps every caller's pointer arithmetic
     * inside the medium. */
    if (h->len == 0 ||
        h->len > (be->size - slot_off - TIKU_BIGBLOB_HDR_BYTES) ||
        memchr(h->name, '\0', sizeof(h->name)) == NULL) {
        return NULL;
    }
    return h;
}

/** @brief Payload ground covered per step; see the header for the sizing. */
#define BIGBLOB_STEP  4096u

/** @brief Validate ranges without overflowing the 32-bit cursor arithmetic. */
static int writable(tiku_nvm_backend_t *be, uint32_t off,
                    const char *name, const void *src, uint32_t len)
{
    if (be == NULL || be->base == NULL || be->write == NULL ||
        be->erase == NULL || src == NULL || name == NULL || len == 0 ||
        off % TIKU_BIGBLOB_HDR_BYTES != 0) {
        return TIKU_BIGBLOB_ERR_PARAM;
    }
    if (strlen(name) > TIKU_BIGBLOB_NAME_MAX) return TIKU_BIGBLOB_ERR_PARAM;
    if (off > UINT32_MAX - TIKU_BIGBLOB_HDR_BYTES ||
        len > UINT32_MAX - off - TIKU_BIGBLOB_HDR_BYTES ||
        off > be->size || TIKU_BIGBLOB_HDR_BYTES > be->size - off ||
        len > be->size - off - TIKU_BIGBLOB_HDR_BYTES) {
        return TIKU_BIGBLOB_ERR_SPACE;
    }
    return TIKU_BIGBLOB_OK;
}

/** @brief Verify the payload, then publish metadata followed by its magic. */
static int publish(tiku_nvm_backend_t *be, uint32_t off, const char *name,
                   const uint8_t *src, uint32_t len)
{
    bigblob_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.magic = BIGBLOB_MAGIC;
    h.len = len;
    h.crc = tiku_nvm_crc32(src, len);
    if (h.crc != tiku_nvm_crc32(be->base + off + TIKU_BIGBLOB_HDR_BYTES, len)) {
        return TIKU_BIGBLOB_ERR_CRC;
    }
    memcpy(h.name, name, strlen(name));
    if (be->write(be, off + sizeof(h.magic),
                  (const uint8_t *)&h + sizeof(h.magic),
                  sizeof(h) - sizeof(h.magic)) != 0 ||
        memcmp(be->base + off + sizeof(h.magic),
               (const uint8_t *)&h + sizeof(h.magic),
               sizeof(h) - sizeof(h.magic)) != 0 ||
        be->write(be, off, &h.magic, sizeof(h.magic)) != 0 ||
        memcmp(be->base + off, &h, sizeof(h)) != 0) {
        return TIKU_BIGBLOB_ERR_IO;
    }
    return TIKU_BIGBLOB_OK;
}

int tiku_bigblob_open(tiku_nvm_backend_t *be, uint32_t slot_off,
                      const char *name, const void *src, uint32_t len,
                      tiku_bigblob_wr_t *w)
{
    int rc;
    if (w == NULL) return TIKU_BIGBLOB_ERR_PARAM;
    memset(w, 0, sizeof(*w));
    rc = writable(be, slot_off, name, src, len);
    if (rc != TIKU_BIGBLOB_OK) return rc;
    w->be       = be;
    w->src      = (const uint8_t *)src;
    w->slot_off = slot_off;
    w->len      = len;
    memcpy(w->name, name, strlen(name));

    /* Unpublish first: from here the slot reads as empty, so a power cut
     * during the minutes that follow leaves no blob rather than a splice. */
    if (be->erase(be, slot_off, TIKU_BIGBLOB_HDR_BYTES) != 0) {
        return TIKU_BIGBLOB_ERR_IO;
    }
    w->active = 1U;
    return TIKU_BIGBLOB_OK;
}

int tiku_bigblob_step(tiku_bigblob_wr_t *w, uint32_t *done)
{
    uint32_t payload, n;

    if (w == NULL || !w->active) {
        return TIKU_BIGBLOB_ERR_PARAM;
    }
    payload = w->slot_off + TIKU_BIGBLOB_HDR_BYTES;

    if (w->done < w->len) {
        n = w->len - w->done;
        if (n > BIGBLOB_STEP) {
            n = BIGBLOB_STEP;
        }
        /* Erase and program the same ground in one step, so the medium is
         * never left erased-but-unwritten across a return to the caller. */
        if (w->be->erase(w->be, payload + w->done, n) != 0 ||
            w->be->write(w->be, payload + w->done, &w->src[w->done], n) != 0) {
            w->active = 0U;
            return TIKU_BIGBLOB_ERR_IO;
        }
        w->done += n;
        if (done != NULL) {
            *done = w->done;
        }
        return 1;
    }

    /* Compare with the source: checksumming only the medium would silently
     * accept a misprogrammed payload as the intended object. */
    {
        int rc = publish(w->be, w->slot_off, w->name, w->src, w->len);
        w->active = 0U;
        if (rc != TIKU_BIGBLOB_OK) return rc;
    }
    w->active = 0U;
    if (done != NULL) {
        *done = w->len;
    }
    return 0;
}

int tiku_bigblob_write(tiku_nvm_backend_t *be, uint32_t slot_off,
                       const char *name, const void *src, uint32_t len)
{
    uint32_t payload;
    int rc = writable(be, slot_off, name, src, len);
    if (rc != TIKU_BIGBLOB_OK) return rc;
    payload = slot_off + TIKU_BIGBLOB_HDR_BYTES;

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

    return publish(be, slot_off, name, (const uint8_t *)src, len);
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
