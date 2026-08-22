/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_conduct.h - the conductor channel: driving a desktop from
 * outside it.
 *
 * A scripted driver compiled INTO the shell can see everything and so is
 * tempting to grow without limit -- and then it ships, or it does not ship
 * and half the product's behaviour can only be proven by a binary nobody
 * runs.  This is the other arrangement: the desktop offers a small, stated
 * automation surface, and the driver is an ordinary process on the far end
 * of a stream.
 *
 * The surface is deliberately thin, and it is the whole of it:
 *
 *   INJECT   one input event, exactly as the display would have delivered it
 *   QUERY    one pixel, or one NAMED string the shell already knows about
 *
 * Everything a script wants -- that a window is titled so, that a menu
 * offers such a row, that this pixel is the dusk amber -- is composed by
 * the DRIVER out of those two.  Nothing about scripts, waits, expectations
 * or files exists on this side of the wire; that vocabulary lives in the
 * driver, out of the product entirely.
 *
 * The channel is off unless the shell was asked for it (-conduct), because
 * a device that can be puppeted by anything that reaches its socket is a
 * device with a hole in it.  The framing is the session's: little-endian
 * [u32 type][u32 length][payload] over any stream -- a local socket now, a
 * serial link to a real board later, which is the point of putting the
 * driver outside in the first place.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CONDUCT_H_
#define TIKU_CONDUCT_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_event.h"

#define TIKU_CONDUCT_VERSION 1
#define TIKU_CONDUCT_ARG     64
#define TIKU_CONDUCT_TEXT    512
/* How long a driver waits for the one answer before calling the question
 * failed.  Generous next to a loop turn, short next to a person. */
#define TIKU_CONDUCT_ANSWER_MS 4000

/* Driver to shell. */
#define TIKU_CMSG_HELLO   1u   /* u32 version                        */
#define TIKU_CMSG_INJECT  2u   /* tiku_event_t                  */
#define TIKU_CMSG_QUERY   3u   /* u32 what, i32 a, i32 b, arg[64]    */
/* Shell to driver. */
#define TIKU_CMSG_ANSWER  16u  /* i32 ok, i32 num, text[512]         */

/** @brief What a QUERY asks for. */
#define TIKU_CQ_PIXEL     1u   /* a, b are x, y; the answer is num   */
#define TIKU_CQ_TEXT      2u   /* arg names it; the answer is text   */

/**
 * @brief What the shell answers a named text query with.
 *
 * The shell supplies this; the channel neither knows nor invents the
 * names.  A name it does not know must be answered with 0, so a driver
 * asking for something this build cannot say is told, not guessed at.
 *
 * @return nonzero when @p name was known and @p out was filled.
 */
typedef int (*tiku_conduct_text_fn)(void *ctx, const char *name,
                                         char *out, size_t max);

/** @brief What the shell reads a pixel with. */
typedef unsigned (*tiku_conduct_pixel_fn)(void *ctx, int x, int y);

/** @brief Where the shell delivers an injected event. */
typedef void (*tiku_conduct_inject_fn)(void *ctx,
                                            const tiku_event_t *event);

typedef struct {
    int                         fd;         /* listening, or -1           */
    int                         peer;       /* the one driver, or -1      */
    char                        path[108];
    void                       *ctx;
    tiku_conduct_text_fn   text;
    tiku_conduct_pixel_fn  pixel;
    tiku_conduct_inject_fn inject;
    /* The stream has no peek; the header accumulates like the session's. */
    unsigned char               hbuf[8];
    size_t                      hgot;
    unsigned char              *buf;
    size_t                      got, want;
    uint32_t                    cur_type;
} tiku_conduct_t;

/**
 * @brief Listen for a driver on @p path.
 *
 * @return 0, or -1.  A shell that was not asked to be driven never calls
 *         this, and then nothing below it can happen at all.
 */
int tiku_conduct_listen(tiku_conduct_t *c, const char *path,
                             void *ctx, tiku_conduct_inject_fn inject,
                             tiku_conduct_pixel_fn pixel,
                             tiku_conduct_text_fn text);

/**
 * @brief Speak the channel over an fd already in hand: a serial port, a
 *        pty, a pipe -- the far end of a device's own line.
 *
 * There is no listening and no accepting here, because a cable is already
 * connected or it is not.  This is the arrangement a real board is driven
 * in: the shell on the device answers on its console line, and the driver
 * is on the other end of the wire, wherever that is.
 */
int tiku_conduct_adopt(tiku_conduct_t *c, int fd, void *ctx,
                            tiku_conduct_inject_fn inject,
                            tiku_conduct_pixel_fn pixel,
                            tiku_conduct_text_fn text);

/**
 * @brief Open @p path as a raw serial line at @p baud.
 *
 * Raw, because the channel's bytes are not text and a line discipline
 * that helpfully translates a carriage return corrupts a frame.  @p baud
 * of 0 leaves the port's own speed alone, which is what a pty wants.
 *
 * @return the fd, or -1.
 */
int tiku_conduct_open_tty(const char *path, int baud);

void tiku_conduct_shutdown(tiku_conduct_t *c);

/**
 * @brief Accept and serve whatever is ready, without blocking.
 *
 * Injected events reach the shell through the inject callback during this
 * call, so they land where the shell's own events do.
 *
 * @return nonzero when anything was injected and a repaint may be owed.
 */
int tiku_conduct_poll(tiku_conduct_t *c);

/*---------------------------------------------------------------------------*/
/* The driver's side.                                                        */
/*---------------------------------------------------------------------------*/

typedef struct {
    int           fd;
    unsigned char hbuf[8];
    size_t        hgot;
} tiku_conduct_client_t;

/** @brief Connect to a shell offering the channel, retrying briefly. */
int tiku_conduct_connect(tiku_conduct_client_t *c,
                              const char *path, int wait_ms);

/** @brief Drive over an fd already in hand -- the same cable, other end. */
int tiku_conduct_connect_fd(tiku_conduct_client_t *c, int fd);

void tiku_conduct_disconnect(tiku_conduct_client_t *c);

/** @brief Deliver one input event to the shell. */
int tiku_conduct_inject(tiku_conduct_client_t *c,
                             const tiku_event_t *event);

/**
 * @brief Ask, and wait for the one answer.
 *
 * Blocking on purpose: a driver has nothing to do until the shell has
 * answered, and an asynchronous answer would only be a queue the driver
 * then has to drain in order anyway.
 *
 * @return nonzero when the shell answered and knew the question.
 */
int tiku_conduct_query(tiku_conduct_client_t *c, uint32_t what,
                            int a, int b, const char *arg,
                            int *num, char *text, size_t max);

#endif /* TIKU_CONDUCT_H_ */
