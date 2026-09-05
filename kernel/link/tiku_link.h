/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link.h - a link: whole messages to and from a peer, on any medium.
 *
 * The seam between a session (a desktop's window session, later others) and
 * the wire it rides.  A backend per medium earns one promise from its
 * medium: a message arrives whole and intact, or not at all.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_LINK_H_
#define TIKU_LINK_H_

#include <stddef.h>
#include <stdint.h>

typedef struct tiku_link tiku_link_t;

/** @brief One whole message from the peer, in the backend's buffer. */
typedef void (*tiku_link_recv_fn)(void *ctx, uint8_t *msg, size_t len);

/**
 * @brief What a backend supplies.  send takes two parts so a message whose
 *        payload already sits in a caller's buffer needs no second copy;
 *        pump and close may be NULL.
 */
typedef struct {
    int  (*send)(tiku_link_t *l, const void *head, size_t hlen,
                 const void *body, size_t blen);
    void (*pump)(tiku_link_t *l);   /**< service the medium, if it needs it */
    void (*close)(tiku_link_t *l);
} tiku_link_ops_t;

/**
 * @brief A link.  A backend fills ops, ctx and cap; the session installs
 *        recv.  Delivery happens in process context, from whoever pumps
 *        the medium.
 */
struct tiku_link {
    const tiku_link_ops_t *ops;
    void                  *ctx;       /**< the backend's own state */
    uint8_t                cap;       /**< a TIKU_VFS_CAP_* mask it confers */
    tiku_link_recv_fn      recv;
    void                  *recv_ctx;
};

/** @brief Send one message.  @return 0, or -1 when the link refuses. */
int  tiku_link_send(tiku_link_t *l, const void *msg, size_t len);

/** @brief Send one message in two parts: @p head then @p body. */
int  tiku_link_send2(tiku_link_t *l, const void *head, size_t hlen,
                     const void *body, size_t blen);

/** @brief Where received messages go; NULL drops them. */
void tiku_link_on_recv(tiku_link_t *l, tiku_link_recv_fn fn, void *ctx);

/** @brief For a backend: hand a whole message to the receiver, if any. */
void tiku_link_deliver(tiku_link_t *l, uint8_t *msg, size_t len);

/** @brief Service the medium; a no-op for a backend that needs none. */
void tiku_link_pump(tiku_link_t *l);

/** @brief The capability the link confers on what arrives over it. */
uint8_t tiku_link_cap(const tiku_link_t *l);

/** @brief Release the medium; the link delivers nothing after this. */
void tiku_link_close(tiku_link_t *l);

#endif /* TIKU_LINK_H_ */
