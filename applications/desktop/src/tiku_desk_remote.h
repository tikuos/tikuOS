/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_remote.h - the window session: a process boundary that is a
 * deployment property, not an application property.
 *
 * An application built on the toolkit may run LINKED into the desktop (the
 * device deployment: one heap, one loop) or OUT OF PROCESS over this
 * session (the hosted deployment: its faults are its own).  The contracts
 * are the same either way -- a window is a surface plus an event route,
 * menus travel as the plain tiku_desk_menuset_t they already are, and
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
#ifndef TIKU_DESK_REMOTE_H_
#define TIKU_DESK_REMOTE_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_desk_event.h"
#include "tiku_desk_msg.h"
#include "tiku_desk_window.h"

#define TIKU_DESK_REMOTE_VERSION   1
#define TIKU_DESK_REMOTE_SESSIONS  8
#define TIKU_DESK_REMOTE_TITLE     64
#define TIKU_DESK_REMOTE_MAX_DIM   1024

/*
 * What a peer can do beyond version 1, sent in HELLO.  A shared surface
 * means both ends are on one machine, which a local socket implies and a
 * serial line denies; a peer that cannot share simply never claims it,
 * and its frames keep being copied.
 */
#define TIKU_DESK_FEAT_SHARED_SURFACE 0x1u
#define TIKU_DESK_REMOTE_BUFFERS      2
#define TIKU_DESK_REMOTE_SHM_NAME     64

/**
 * How many messages a session may have said and not yet been asked for.
 *
 * The shell drains these every loop, so eight is a burst rather than a
 * backlog.  Past eight the newest is let go and counted, because a queue
 * that quietly forgets is worse than one that says how much it forgot.
 */
#define TIKU_DESK_REMOTE_SAID         8

/* Message types, client to desktop... */
#define TIKU_DESK_RMSG_HELLO   1u   /* u32 version, char name[32]        */
#define TIKU_DESK_RMSG_OPEN    2u   /* u32 id, i32 w, i32 h, title[64]   */
#define TIKU_DESK_RMSG_FRAME   3u   /* u32 id, i32 w, i32 h, u32 px[w*h] */
#define TIKU_DESK_RMSG_MENUS   4u   /* u32 id, tiku_desk_menuset_t       */
#define TIKU_DESK_RMSG_CLOSE   5u   /* u32 id                            */
#define TIKU_DESK_RMSG_SURFACE 6u   /* u32 id, i32 w, i32 h, name[64]    */
#define TIKU_DESK_RMSG_READY   7u   /* u32 id, u32 buffer                */
#define TIKU_DESK_RMSG_SAY     8u   /* a flattened tiku_desk_msg_t       */
/* ...and desktop to client. */
#define TIKU_DESK_RMSG_EVENT   16u  /* u32 id, tiku_desk_event_t         */
#define TIKU_DESK_RMSG_PICK    17u  /* u32 id, i32 command               */
#define TIKU_DESK_RMSG_TELL    19u  /* a flattened tiku_desk_msg_t       */
#define TIKU_DESK_RMSG_CLOSED  18u  /* u32 id: the user closed it        */

/** @brief Where the desktop listens, under this user's HOME. */
int tiku_desk_remote_path(char *out, size_t max);

/*---------------------------------------------------------------------------*/
/* The desktop's side: a listener whose sessions become windows.             */
/*---------------------------------------------------------------------------*/

typedef struct {
    int                  fd;
    int                  used;
    char                 name[32];
    uint32_t             win_id;        /* one window per session, for now */
    struct tiku_desk_window *window;    /* not owned                       */
    uint32_t            *frame;         /* latest pixels, w*h              */
    int                  fw, fh;
    tiku_desk_menuset_t  menus;
    int                  has_menus;
    int                  menus_fresh;   /* set when MENUS arrives          */
    int                  opened;        /* OPEN seen, window wanted        */
    int                  open_w, open_h;
    char                 title[TIKU_DESK_REMOTE_TITLE];
    /* A surface shared with the application: it paints into one buffer
     * while the desktop shows the other, and a frame becomes a message
     * saying which. */
    struct tiku_desk_remote_listener *owner;
    uint32_t            *shared;        /* mapped, or NULL            */
    size_t               shared_bytes;
    int                  shown;         /* buffer index being shown   */
    unsigned             features;      /* what the peer said it can  */
    /* Partial-read state: the wire is a stream and a FRAME is large.
     * The header accumulates too -- a serial link has no peek. */
    unsigned char        hbuf[8];
    size_t               hgot;
    unsigned char       *buf;
    size_t               got, want;
    uint32_t             cur_type;
    /* What this session has said and nobody has taken yet. */
    tiku_desk_msg_t     *said[TIKU_DESK_REMOTE_SAID];
    int                  said_head, said_count, said_lost;
} tiku_desk_remote_session_t;

typedef struct tiku_desk_remote_listener {
    /* Shared surfaces mapped and not yet given back.  A count rather
     * than a flag: a leak is invisible in a freed session, whose fields
     * are cleared either way. */
    int                        mapped;
    int                        fd;      /* listening socket, or -1        */
    char                       path[108];
    tiku_desk_remote_session_t session[TIKU_DESK_REMOTE_SESSIONS];
} tiku_desk_remote_listener_t;

/** @brief Listen on the desk socket.  @return 0, or -1. */
int tiku_desk_remote_listen(tiku_desk_remote_listener_t *listener);

void tiku_desk_remote_shutdown(tiku_desk_remote_listener_t *listener);

/** @brief Adopt an fd (a serial link, a pty) as a session directly. */
int tiku_desk_remote_adopt(tiku_desk_remote_listener_t *listener, int fd);

/**
 * @brief Accept and read everything that is ready, without blocking.
 *
 * @return nonzero when anything changed (a session appeared or died, a
 *         frame or menu set arrived) and a repaint is owed.
 */
int tiku_desk_remote_poll(tiku_desk_remote_listener_t *listener);

/** @brief Send an input event to the session owning @p window. */
void tiku_desk_remote_event(tiku_desk_remote_listener_t *listener,
                            struct tiku_desk_window *window,
                            const tiku_desk_event_t *event);

/** @brief Send a menu pick to the session owning @p window. */
void tiku_desk_remote_pick(tiku_desk_remote_listener_t *listener,
                           struct tiku_desk_window *window, int command);

/** @brief Tell the session its window was closed, and forget it. */
void tiku_desk_remote_window_closed(tiku_desk_remote_listener_t *listener,
                                    struct tiku_desk_window *window);

/**
 * @brief The pixels a session's window should show, or NULL.
 *
 * Shared when the peer could share, copied when it could not; a caller
 * draws the same way either way.
 */
/** @brief Shared surfaces this listener still holds. */
int tiku_desk_remote_mapped(const tiku_desk_remote_listener_t *listener);

const uint32_t *tiku_desk_remote_pixels(
    const tiku_desk_remote_session_t *session);

/*---------------------------------------------------------------------------*/
/* And one message type that is not a struct at all.                         */
/*                                                                           */
/* Everything above is a layout both ends were compiled against, which is    */
/* right for a frame -- it is a million pixels and its shape never changes.  */
/* It is wrong for anything a device might want to say that this build did   */
/* not foresee.  These two carry a tiku_desk_msg_t instead: named, typed     */
/* fields, and a reader that steps over what it does not know.  A device on  */
/* the end of a cable running last month's firmware can be talked to over    */
/* these without either end being rebuilt to match the other.                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Take the next message a session sent, or NULL.
 *
 * @return a message the CALLER frees.  Taking it makes room for another.
 */
tiku_desk_msg_t *tiku_desk_remote_said(tiku_desk_remote_listener_t *listener,
                                       int index);

/** @brief How many a session sent that did not fit, and were let go. */
int tiku_desk_remote_lost(const tiku_desk_remote_listener_t *listener,
                          int index);

/** @brief Send @p m down to a session.  @return 1 when it went. */
int tiku_desk_remote_tell(tiku_desk_remote_listener_t *listener, int index,
                          const tiku_desk_msg_t *m);

/** @brief The session owning @p window, or NULL. */
tiku_desk_remote_session_t *tiku_desk_remote_owner(
    tiku_desk_remote_listener_t *listener, struct tiku_desk_window *window);

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
    char          shm_name[TIKU_DESK_REMOTE_SHM_NAME];
    unsigned      features;
    /* Header accumulation: the line has no peek. */
    unsigned char hbuf[8];
    size_t        hgot;
    /* The message the last read brought up, until it is taken. */
    tiku_desk_msg_t *heard;
} tiku_desk_remote_client_t;

/**
 * @brief Connect to the desktop, retrying briefly.
 *
 * The retry is what frees launch order: a client started a moment before
 * the desktop still finds it.
 *
 * @return 0, or -1 when no desktop answered within @p wait_ms.
 */
int tiku_desk_remote_connect(tiku_desk_remote_client_t *client,
                             const char *name, int wait_ms);

/**
 * @brief Speak the session over an fd already in hand -- a serial port,
 *        a pty, a pipe.  The wire neither knows nor cares; this is how
 *        the same frames will reach a device over its own link.
 */
int tiku_desk_remote_connect_fd(tiku_desk_remote_client_t *client,
                                const char *name, int fd);

void tiku_desk_remote_disconnect(tiku_desk_remote_client_t *client);

/** @brief Ask for a window.  @return its id, or 0. */
uint32_t tiku_desk_remote_open(tiku_desk_remote_client_t *client,
                               const char *title, int w, int h);

/** @brief Send the whole surface as the window's next frame. */
int tiku_desk_remote_frame(tiku_desk_remote_client_t *client, uint32_t id,
                           const uint32_t *px, int w, int h);

/** @brief Publish menus for the window, as the plain data they are. */
int tiku_desk_remote_menus(tiku_desk_remote_client_t *client, uint32_t id,
                           const tiku_desk_menuset_t *menus);

/**
 * @brief Read one message if one is ready.
 *
 * @return the message type, 0 when nothing is ready, or -1 when the
 *         desktop went away (which is the client's cue to exit).
 */
int tiku_desk_remote_read(tiku_desk_remote_client_t *client, uint32_t *id,
                          tiku_desk_event_t *event, int *command);

/** @brief Say @p m to the desktop.  @return 1 when it went. */
int tiku_desk_remote_say(tiku_desk_remote_client_t *client,
                         const tiku_desk_msg_t *m);

/**
 * @brief Take the message the last read brought up, or NULL.
 *
 * Read answers TIKU_DESK_RMSG_TELL when there is one.
 *
 * @return a message the CALLER frees.
 */
tiku_desk_msg_t *tiku_desk_remote_heard(tiku_desk_remote_client_t *client);

#endif /* TIKU_DESK_REMOTE_H_ */
