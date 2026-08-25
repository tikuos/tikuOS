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

/* Version 2: the answer's text grew from 512 to 2048 so a whole window's
 * narration fits it.  The framing did not change shape, but a driver
 * built to the old size would misread the new answers, and the HELLO
 * check exists exactly so a mismatched pairing is refused, not half
 * served. */
#define TIKU_CONDUCT_VERSION 2
#define TIKU_CONDUCT_ARG     64
#define TIKU_CONDUCT_TEXT    2048
/* How long a driver waits for the one answer before calling the question
 * failed.  Generous next to a loop turn, short next to a person. */
#define TIKU_CONDUCT_ANSWER_MS 4000

/* Driver to shell. */
#define TIKU_CMSG_HELLO   1u   /* u32 version                        */
#define TIKU_CMSG_INJECT  2u   /* tiku_event_t                  */
#define TIKU_CMSG_QUERY   3u   /* u32 what, i32 a, i32 b, arg[64]    */
/* Shell to driver. */
#define TIKU_CMSG_ANSWER  16u  /* i32 ok, i32 num, text[]            */
#define TIKU_CMSG_TOLD    17u  /* arg[64] names it, text[] says it   */

/** @brief What a QUERY asks for. */
#define TIKU_CQ_PIXEL     1u   /* a, b are x, y; the answer is num   */
#define TIKU_CQ_TEXT      2u   /* arg names it; the answer is text   */

/* How many named facts one driver may watch at once, and how often the
 * shell looks for a change on its behalf.  Small on purpose: a watch
 * costs the shell an evaluation per interval -- for "narrate", a full
 * content draw -- and a driver watching everything is a driver that
 * should have been polling. */
#define TIKU_CONDUCT_SUBS     4
#define TIKU_CONDUCT_TELL_MS  100

/* Identity before reach.  A HELLO may carry a token after its version;
 * when the shell was told to require one (tiku_conduct_require), the
 * token is the peer's name and decides its GRANT -- what this session
 * may do -- before anything else is served.  QUERY-only is a different
 * grant from driving: a reader's credential provably cannot inject. */
#define TIKU_CONDUCT_TOKEN       64
#define TIKU_CONDUCT_GRANT_NONE   0   /* nothing until a named HELLO   */
#define TIKU_CONDUCT_GRANT_QUERY  1   /* ask and watch, never inject   */
#define TIKU_CONDUCT_GRANT_FULL   2   /* the driver's full surface     */

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
    /* What the one driver asked to be told about: QUERY "~name" answers
     * with the fact's current text AND registers it here; a change is
     * then pushed as a TOLD frame on the same stream.  Watches live as
     * long as the peer does and no longer. */
    char                        sub_name[TIKU_CONDUCT_SUBS]
                                        [TIKU_CONDUCT_ARG];
    uint32_t                    sub_sig[TIKU_CONDUCT_SUBS];
    int                         subs;
    int64_t                     told_at_us;
    /* Who the peer is and what it may do.  Unconfigured, any peer that
     * speaks the version drives fully -- the harness's own local lane.
     * Configured, a session is NOTHING until its HELLO names it. */
    int                         required;
    char                        token_full[TIKU_CONDUCT_TOKEN];
    char                        token_query[TIKU_CONDUCT_TOKEN];
    int                         grant;
    char                        principal[TIKU_CONDUCT_TOKEN];
    /* The session's spend.  A quota of 0 is no quota; past a set one
     * the peer is closed -- a runaway driver is cut off rather than
     * rationed, because a driver that has to be rationed is broken. */
    long                        quota;
    long                        spent;
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
 * @brief Require a named HELLO: @p full_token drives, @p query_token
 *        asks and watches but provably cannot inject; either may be
 *        NULL to offer no such grant.
 *
 * Once required, an unnamed or wrongly named peer is closed at HELLO --
 * it gets exactly what a version mismatch gets, nothing -- and a peer
 * already connected is stripped back to nothing until it names itself.
 */
void tiku_conduct_require(tiku_conduct_t *c, const char *full_token,
                               const char *query_token);

/**
 * @brief Cap what one session may do: after @p messages INJECTs and
 *        QUERYs together, the peer is closed.  0 lifts the cap.
 *
 * Beside the counters, not instead of them: the point is that a
 * runaway driver costs a bounded amount of shell, then costs nothing.
 */
void tiku_conduct_quota(tiku_conduct_t *c, long messages);

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

/** @brief Connect AS someone: the HELLO carries @p token as the name
 *         this session offers.  NULL is the unnamed connect above. */
int tiku_conduct_connect_as(tiku_conduct_client_t *c,
                                 const char *path, int wait_ms,
                                 const char *token);
int tiku_conduct_connect_fd_as(tiku_conduct_client_t *c, int fd,
                                    const char *token);

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

/**
 * @brief Wait to be TOLD: block until the shell pushes a watched fact's
 *        new value, or @p wait_ms runs out.
 *
 * The other half of QUERY "~name": the driver stops asking and the shell
 * speaks when the fact changes.  Bounded on purpose -- a driver that can
 * hang is worse than one that fails, because it reports nothing and
 * someone waits on it.
 *
 * @return nonzero when a TOLD arrived; @p name and @p text then say
 *         which fact and what it says now.
 */
int tiku_conduct_told(tiku_conduct_client_t *c,
                           char *name, size_t name_max,
                           char *text, size_t text_max, int wait_ms);

#endif /* TIKU_CONDUCT_H_ */
