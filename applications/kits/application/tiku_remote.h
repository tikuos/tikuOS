/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_remote.h - the window session: a process boundary that is a
 * deployment property, not an application property.
 *
 * An application built on the toolkit may run LINKED into the desktop (the
 * device deployment: one heap, one loop) or OUT OF PROCESS over this
 * session (the hosted deployment: its faults are its own).  The contracts
 * are the same either way -- a window is a surface plus an event route,
 * menus travel as the plain tiku_menuset_t they already are, and
 * picks come back as commands.  Nothing here is X11 or POSIX-specific
 * beyond a stream socket, which is what will let the same frames ride the
 * device transport one day.
 *
 * The wire is little machine-endian structs on a local stream:
 *   [u32 type][u32 payload length][payload]
 * with both peers on one host; a version in HELLO guards the rest.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_REMOTE_H_
#define TIKU_REMOTE_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_dl.h"
#include "tiku_event.h"
#include "tiku_msg.h"
#include "tiku_window.h"

#define TIKU_REMOTE_VERSION   1
#define TIKU_REMOTE_SESSIONS  8
/* Windows one out-of-process application may hold at once. */
#define TIKU_REMOTE_WINDOWS   4
#define TIKU_REMOTE_TITLE     64
#define TIKU_REMOTE_MAX_DIM   1024

/*
 * What a peer can do beyond version 1, sent in HELLO.  A shared surface
 * means both ends are on one machine, which a local socket implies and a
 * serial line denies; a peer that cannot share simply never claims it,
 * and its frames keep being copied.
 */
#define TIKU_FEAT_SHARED_SURFACE 0x1u
/*
 * The peer can send what it DREW rather than what it came out as: a
 * stream of drawing commands instead of a frame of pixels.  Claimed by
 * whoever is doing the drawing, and worth claiming exactly when the link
 * is slow -- a 372x302 window is 449,376 bytes as pixels and 350 as
 * commands, which over a serial line is the difference between a tenth
 * of a second and most of a minute.  See tiku_dl.h.
 */
#define TIKU_FEAT_COMMAND_STREAM 0x2u
/*
 * The stream's icon commands are understood: art defined once per list
 * as its HVIF bytes, placements by reference, rasterised at THIS end.
 * Distinct from COMMAND_STREAM because an end can play one without the
 * other -- and a sender whose list carries icons but whose peer never
 * claimed this sends the pixels, exactly as it does for a miss.
 */
/*
 * The peer can play a PICTURE command -- a small bitmap with its own
 * mask.  Separate from the command stream for the same reason icons
 * are: an end may play one without the other, and an end that stepped
 * over a picture would draw a window with a hole where the picture was
 * and nothing would say so.
 */
#define TIKU_FEAT_PICTURE_STREAM 0x8u
#define TIKU_FEAT_ICON_STREAM    0x4u
#define TIKU_REMOTE_BUFFERS      2
#define TIKU_REMOTE_SHM_NAME     64

/**
 * How many messages a session may have said and not yet been asked for.
 *
 * The shell drains these every loop, so eight is a burst rather than a
 * backlog.  Past eight the newest is let go and counted, because a queue
 * that quietly forgets is worse than one that says how much it forgot.
 */
#define TIKU_REMOTE_SAID         8

/* Message types, client to desktop... */
#define TIKU_RMSG_HELLO   1u   /* u32 version, char name[32]        */
#define TIKU_RMSG_OPEN    2u   /* u32 id, i32 w, i32 h, title[64]   */
#define TIKU_RMSG_FRAME   3u   /* u32 id, i32 w, i32 h, u32 px[w*h] */
#define TIKU_RMSG_MENUS   4u   /* u32 id, tiku_menuset_t       */
#define TIKU_RMSG_CLOSE   5u   /* u32 id                            */
#define TIKU_RMSG_SURFACE 6u   /* u32 id, i32 w, i32 h, name[64]    */
#define TIKU_RMSG_READY   7u   /* u32 id, u32 buffer                */
#define TIKU_RMSG_SAY     8u   /* a flattened tiku_msg_t       */
#define TIKU_RMSG_DRAW    9u   /* u32 id, i32 w, i32 h, a tiku_dl_t */
/* ...and desktop to client. */
#define TIKU_RMSG_EVENT   16u  /* u32 id, tiku_event_t         */
#define TIKU_RMSG_PICK    17u  /* u32 id, i32 command               */
#define TIKU_RMSG_TELL    19u  /* a flattened tiku_msg_t       */
/*
 * The answer to a HELLO: what THIS end can do.
 *
 * Until this existed the features went one way only -- a client said what
 * it could and the desktop never said anything back -- so a client had no
 * way to find out whether the far end could play a command stream, and
 * sent one anyway.  That was survivable while both ends were built
 * together and stops being survivable the moment the vocabulary grows: an
 * op an older end has never heard of is STEPPED OVER (see tiku_dl.h), so
 * a window would arrive missing whatever that op drew, silently, with the
 * list still claiming to be whole.
 *
 * Sent by the desktop when it takes a HELLO.  A client that predates it
 * reads an unknown type and ignores it, which is what the length prefix
 * is for; a desktop that predates it sends nothing and a client that
 * wanted this learns the far end can do NOTHING beyond version 1, which
 * is the safe answer rather than the optimistic one.
 */
#define TIKU_RMSG_WELCOME 20u  /* u32 version, u32 features         */
#define TIKU_RMSG_CLOSED  18u  /* u32 id: the user closed it        */

/** @brief Where the desktop listens, under this user's HOME. */
int tiku_remote_path(char *out, size_t max);

/*---------------------------------------------------------------------------*/
/* The desktop's side: a listener whose sessions become windows.             */
/*---------------------------------------------------------------------------*/

/*
 * ONE WINDOW of a session.  A session was a window for as long as an
 * application only ever had one; an application with several is asking
 * for the id it is already given on every message to MEAN something,
 * and everything a window has of its own lives here.
 */
typedef struct tiku_remote_window {
    struct tiku_remote_session *session; /* the row's way back        */
    uint32_t             win_id;        /* what the application calls it */
    struct tiku_window *window;    /* not owned                       */
    uint32_t            *frame;         /* latest pixels, w*h              */
    int                  fw, fh;
    tiku_menuset_t  menus;
    int                  has_menus;
    int                  menus_fresh;   /* set when MENUS arrives          */
    int                  opened;        /* OPEN seen, window wanted        */
    int                  open_w, open_h;
    char                 title[TIKU_REMOTE_TITLE];
    /* A surface shared with the application: it paints into one buffer
     * while the desktop shows the other, and a frame becomes a message
     * saying which. */
    uint32_t            *shared;        /* mapped, or NULL            */
    size_t               shared_bytes;
    int                  shown;         /* buffer index being shown   */
    /*
     * WHICH of the two arrivals is the newer one.  A session may send
     * both kinds: pixels (copied into `frame`, or written by the
     * application into `shared` and announced with a READY), and command
     * lists (played into `frame`).  Nonzero means a list was the last
     * thing to arrive, so `frame` is the window and the shared buffer is
     * a stale earlier copy of it.
     *
     * Without this the two sources disagree silently and the shared one
     * always wins: an application whose frames go as lists -- which is
     * every application, the moment the desktop claims the feature --
     * would paint into a buffer nothing looks at and its window would
     * stand at whatever it drew first.
     */
    int                  list_fresh;
    /*
     * The geometry the shared mapping was made at.  fw/fh follow
     * whichever arrival is newest, and a list's are its own -- so the
     * mapping's size has to be kept apart from them, or the stride used
     * to read it comes from a frame that was never in it.
     */
    int                  sw, sh;
} tiku_remote_window_t;

typedef struct tiku_remote_session {
    int                  fd;
    int                  used;
    char                 name[32];
    struct tiku_remote_listener *owner;
    /* The windows this application holds.  Small on purpose: a menuset
     * is carried by value, so a row is ten kilobytes, and an
     * application wanting more than a handful of windows at once is
     * asking for something this desktop has not agreed to yet. */
    tiku_remote_window_t win[TIKU_REMOTE_WINDOWS];
    unsigned             features;      /* what the peer said it can  */
    /* Partial-read state: the wire is a stream and a FRAME is large.
     * The header accumulates too -- a serial link has no peek. */
    unsigned char        hbuf[8];
    size_t               hgot;
    unsigned char       *buf;
    size_t               got, want;
    uint32_t             cur_type;
    /* What this session has said and nobody has taken yet. */
    tiku_msg_t     *said[TIKU_REMOTE_SAID];
    int                  said_head, said_count, said_lost;
} tiku_remote_session_t;

typedef struct tiku_remote_listener {
    /* Shared surfaces mapped and not yet given back.  A count rather
     * than a flag: a leak is invisible in a freed session, whose fields
     * are cleared either way. */
    int                        mapped;
    int                        fd;      /* listening socket, or -1        */
    char                       path[108];
    tiku_remote_session_t session[TIKU_REMOTE_SESSIONS];
} tiku_remote_listener_t;

/** @brief Listen on the desk socket.  @return 0, or -1. */
int tiku_remote_listen(tiku_remote_listener_t *listener);

void tiku_remote_shutdown(tiku_remote_listener_t *listener);

/** @brief Adopt an fd (a serial link, a pty) as a session directly. */
int tiku_remote_adopt(tiku_remote_listener_t *listener, int fd);

/**
 * @brief Accept and read everything that is ready, without blocking.
 *
 * @return nonzero when anything changed (a session appeared or died, a
 *         frame or menu set arrived) and a repaint is owed.
 */
int tiku_remote_poll(tiku_remote_listener_t *listener);

/** @brief Send an input event to the session owning @p window. */
void tiku_remote_event(tiku_remote_listener_t *listener,
                            struct tiku_window *window,
                            const tiku_event_t *event);

/** @brief Send a menu pick to the session owning @p window. */
void tiku_remote_pick(tiku_remote_listener_t *listener,
                           struct tiku_window *window, int command);

/** @brief Tell the session its window was closed, and forget it. */
void tiku_remote_window_closed(tiku_remote_listener_t *listener,
                                    struct tiku_window *window);

/**
 * @brief The pixels a session's window should show, or NULL.
 *
 * Shared when the peer could share, copied when it could not; a caller
 * draws the same way either way.
 */
/** @brief Shared surfaces this listener still holds. */
int tiku_remote_mapped(const tiku_remote_listener_t *listener);

/** @brief The window @p w belongs to, or NULL. */
tiku_remote_window_t *tiku_remote_window_of(
    tiku_remote_listener_t *listener, const struct tiku_window *w);

const uint32_t *tiku_remote_pixels(
    const tiku_remote_window_t *win);

/*---------------------------------------------------------------------------*/
/* And one message type that is not a struct at all.                         */
/*                                                                           */
/* Everything above is a layout both ends were compiled against, which is    */
/* right for a frame -- it is a million pixels and its shape never changes.  */
/* It is wrong for anything a device might want to say that this build did   */
/* not foresee.  These two carry a tiku_msg_t instead: named, typed     */
/* fields, and a reader that steps over what it does not know.  A device on  */
/* the end of a cable running last month's firmware can be talked to over    */
/* these without either end being rebuilt to match the other.                */
/*---------------------------------------------------------------------------*/

/*
 * The two messages an application and its shell exchange over SAY and
 * TELL to get a file picked.  They ride the general road rather than
 * ops of their own: SAY and TELL already carry a self-describing
 * message either way, and a third pattern for one question would be a
 * third thing to keep in step.
 *
 * ASK   what = TIKU_MSG_PICK    int32 "window", int32 "mode",
 *                               string "start", string "name"
 * ANSWER what = TIKU_MSG_PICKED int32 "window", string "path"
 *
 * An old peer that does not know them steps over them, which is what
 * the unknown-op rules above already promise.
 */
#define TIKU_MSG_PICK   0x7069636bu   /* 'pick' */
#define TIKU_MSG_PICKED 0x70696b64u   /* 'pikd' */

/**
 * @brief Take the next message a session sent, or NULL.
 *
 * @return a message the CALLER frees.  Taking it makes room for another.
 */
tiku_msg_t *tiku_remote_said(tiku_remote_listener_t *listener,
                                       int index);

/** @brief How many a session sent that did not fit, and were let go. */
int tiku_remote_lost(const tiku_remote_listener_t *listener,
                          int index);

/** @brief Send @p m down to a session.  @return 1 when it went. */
int tiku_remote_tell(tiku_remote_listener_t *listener, int index,
                          const tiku_msg_t *m);

/** @brief The session owning @p window, or NULL. */
tiku_remote_session_t *tiku_remote_owner(
    tiku_remote_listener_t *listener, struct tiku_window *window);

/*---------------------------------------------------------------------------*/
/* The application's side: the same contracts, backed by the wire.           */
/*---------------------------------------------------------------------------*/

typedef struct {
    int           fd;
    uint32_t      next_id;
    /* The surface this client shares with the desktop, when the link can
     * carry a descriptor. */
    uint32_t     *shared;
    size_t        shared_bytes;
    int           shared_w, shared_h;
    int           next_buffer;
    char          shm_name[TIKU_REMOTE_SHM_NAME];
    unsigned      features;
    /*
     * What the far end said IT can do, from the WELCOME -- zero until it
     * has answered, and zero for ever against a desktop too old to.  Zero
     * means "assume nothing", so what goes over is what every version has
     * always understood.
     */
    unsigned      peer_features;
    int           peer_version;
    /* Header accumulation: the line has no peek. */
    unsigned char hbuf[8];
    size_t        hgot;
    /* The message the last read brought up, until it is taken. */
    tiku_msg_t *heard;
} tiku_remote_client_t;

/**
 * @brief Connect to the desktop, retrying briefly.
 *
 * The retry is what frees launch order: a client started a moment before
 * the desktop still finds it.
 *
 * @return 0, or -1 when no desktop answered within @p wait_ms.
 */
int tiku_remote_connect(tiku_remote_client_t *client,
                             const char *name, int wait_ms);

/**
 * @brief What the far end has said it can do, or 0 if it has not said.
 *
 * Read it before using anything the far end might not have: zero is not
 * "no features", it is "no answer yet", and the two want the same
 * conservative treatment.
 */
unsigned tiku_remote_peer_features(const tiku_remote_client_t *client);

/**
 * @brief Speak the session over an fd already in hand -- a serial port,
 *        a pty, a pipe.  The wire neither knows nor cares; this is how
 *        the same frames will reach a device over its own link.
 */
int tiku_remote_connect_fd(tiku_remote_client_t *client,
                                const char *name, int fd);

void tiku_remote_disconnect(tiku_remote_client_t *client);

/** @brief Ask for a window.  @return its id, or 0. */
uint32_t tiku_remote_open(tiku_remote_client_t *client,
                               const char *title, int w, int h);

/** @brief Send the whole surface as the window's next frame. */
int tiku_remote_frame(tiku_remote_client_t *client, uint32_t id,
                           const uint32_t *px, int w, int h);

/**
 * @brief Send what was DRAWN, and let the far end draw it.
 *
 * The same window as frame() would send, at a fraction of the bytes,
 * because the commands are semantic: one of them is a whole button.
 * The far end plays it into a surface of @p w by @p h and shows that,
 * so nothing downstream of the session can tell which way it arrived.
 *
 * @return 1 when it went.
 */
int tiku_remote_draw(tiku_remote_client_t *client, uint32_t id,
                          const tiku_dl_t *dl, int w, int h);

/** @brief Publish menus for the window, as the plain data they are. */
/** @brief Give back ONE window; the session stays. */
void tiku_remote_close_window(tiku_remote_client_t *client, uint32_t id);

int tiku_remote_menus(tiku_remote_client_t *client, uint32_t id,
                           const tiku_menuset_t *menus);

/**
 * @brief Read one message if one is ready.
 *
 * @return the message type, 0 when nothing is ready, or -1 when the
 *         desktop went away (which is the client's cue to exit).
 */
int tiku_remote_read(tiku_remote_client_t *client, uint32_t *id,
                          tiku_event_t *event, int *command);

/** @brief Say @p m to the desktop.  @return 1 when it went. */
int tiku_remote_say(tiku_remote_client_t *client,
                         const tiku_msg_t *m);

/**
 * @brief Take the message the last read brought up, or NULL.
 *
 * Read answers TIKU_RMSG_TELL when there is one.
 *
 * @return a message the CALLER frees.
 */
tiku_msg_t *tiku_remote_heard(tiku_remote_client_t *client);

#endif /* TIKU_REMOTE_H_ */
