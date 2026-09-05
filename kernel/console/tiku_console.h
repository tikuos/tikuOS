/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_console.h - the console line as SLIP-framed channels beside the text.
 *
 * One decoder un-escapes every 0xC0-delimited frame on the wire and hands it
 * whole to the channel its first byte names; bytes outside a frame are text.
 * The IP stack and a host desktop's window session are two such channels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CONSOLE_H_
#define TIKU_CONSOLE_H_

#include <stddef.h>
#include <stdint.h>
#include <kernel/timers/tiku_clock.h>

/*---------------------------------------------------------------------------*/
/* THE FRAME ON THE WIRE (RFC 1055, plus the NUL escape)                     */
/*---------------------------------------------------------------------------*/

#define TIKU_CONSOLE_END      0xC0u  /**< frame delimiter */
#define TIKU_CONSOLE_ESC      0xDBu  /**< escape */
#define TIKU_CONSOLE_ESC_END  0xDCu  /**< ESC + this = a literal END */
#define TIKU_CONSOLE_ESC_ESC  0xDDu  /**< ESC + this = a literal ESC */
#define TIKU_CONSOLE_ESC_NUL  0xDEu  /**< ESC + this = a literal NUL */

/** @brief Channels the console can dispatch to at once. */
#ifndef TIKU_CONSOLE_CHANNELS
#define TIKU_CONSOLE_CHANNELS 4
#endif

/**
 * @brief How long one frame may stay open before it is abandoned.
 *
 * A frame whose closing END was lost would otherwise swallow every later
 * keystroke as payload.  The draining pump legitimately pauses for seconds
 * mid-frame during TLS crypto, so the limit dwarfs any real frame.
 */
#ifndef TIKU_CONSOLE_FRAME_TTL
#define TIKU_CONSOLE_FRAME_TTL (30u * TIKU_CLOCK_SECOND)
#endif

/**
 * @brief How often the console's own pump drains the wire, in ticks.
 *
 * A build with a shell reads the wire from the shell's poll loop.  One
 * without runs a process of the console's own at this cadence, for as long
 * as a channel or a text sink is registered.
 */
#ifndef TIKU_CONSOLE_POLL_TICKS
#define TIKU_CONSOLE_POLL_TICKS (TIKU_CLOCK_SECOND / 20)
#endif

/*---------------------------------------------------------------------------*/
/* THE WIRE                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief The physical line: three calls and whether it can carry a NUL.
 *
 * The boot console (the UART, or the USB CDC port on a native-USB build) is
 * installed by default; a test installs a RAM loopback in its place.
 */
typedef struct {
    void    (*putc)(char c);        /**< transmit one raw byte */
    uint8_t (*rx_ready)(void);      /**< non-zero when getc has a byte */
    int     (*getc)(void);          /**< one byte, or -1 when none */
    uint8_t   escape_nul;           /**< send a NUL as ESC ESC_NUL */
} tiku_console_wire_t;

/** @brief Install @p wire, or the boot console again for NULL. */
void tiku_console_set_wire(const tiku_console_wire_t *wire);

/** @brief The wire in use. */
const tiku_console_wire_t *tiku_console_wire(void);

/*---------------------------------------------------------------------------*/
/* CHANNELS                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief A whole frame for one channel, un-escaped, in the channel's own
 *        buffer.  The marker byte is present only for a channel registered
 *        with keep_first.
 */
typedef void (*tiku_console_frame_fn)(void *ctx, uint8_t *buf, size_t len);

/**
 * @brief Route frames whose first byte matches @p value under @p mask to
 *        @p fn, reassembled in @p buf of @p cap bytes.
 *
 * The window session registers (0xF1, 0xFF) with the marker stripped; the IP
 * stack registers (0x40, 0xF0) keeping the first byte, the IPv4 version
 * nibble.  A frame that outgrows @p buf is dropped whole and counted.
 *
 * @param keep_first  non-zero to deliver the first byte as payload
 * @return 0, or -1 when the table is full, @p fn is NULL or @p cap is 0
 */
int  tiku_console_add_channel(uint8_t value, uint8_t mask, uint8_t keep_first,
                              tiku_console_frame_fn fn, void *ctx,
                              uint8_t *buf, size_t cap);

/** @brief Forget the channel registered as (@p value, @p mask). */
void tiku_console_remove_channel(uint8_t value, uint8_t mask);

/*---------------------------------------------------------------------------*/
/* OUT                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Send one frame on the channel @p marker names: END, the marker,
 *        @p head then @p body escaped, END.
 *
 * Two parts so a message whose payload already sits in a caller's buffer
 * needs no second copy.  Either part may be NULL with a zero length.
 *
 * @return 0, or -1 when there is no wire
 */
int  tiku_console_send_frame(uint8_t marker, const void *head, size_t hlen,
                             const void *body, size_t blen);

/**
 * @brief Open a frame on the channel @p marker names: END, the marker.
 *
 * For a sender composing a frame from more parts than send_frame takes,
 * such as a header, a message and a trailer.  Close it with frame_end.
 *
 * @return 0, or -1 when there is no wire
 */
int  tiku_console_frame_begin(uint8_t marker);

/** @brief @p len bytes of @p bytes into the open frame, escaped. */
void tiku_console_frame_put(const void *bytes, size_t len);

/** @brief Close the open frame: END. */
void tiku_console_frame_end(void);

/** @brief Write @p len raw bytes of text to the wire. */
void tiku_console_write(const void *bytes, size_t len);

/*---------------------------------------------------------------------------*/
/* IN                                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief The next text byte from the wire, or -1 when there is none.
 *
 * Frame bytes met on the way are consumed and whole frames dispatched to
 * their channels before this returns.  The shell's line editor reads here.
 */
int  tiku_console_getc(void);

/** @brief Text bytes go here when the wire is pumped; return is ignored. */
typedef int (*tiku_console_text_fn)(void *ctx, int ch);

/** @brief Install where pumped text goes; NULL discards it. */
void tiku_console_set_text_sink(tiku_console_text_fn fn, void *ctx);

/**
 * @brief Drain the wire: frames to their channels, text to the sink.
 *
 * For a caller that owns the console but does not read the keyboard, such
 * as a blocking builtin keeping the IP stack fed.  A build without a shell
 * calls this from the console's own process, see tiku_console_pumping.
 */
void tiku_console_pump(void);

/**
 * @brief Whether the console's own pump process is running.
 *
 * In a build without a shell the first channel or text sink registered
 * starts it and the last removed ends it, so a build that registers nothing
 * runs no process.  Always 0 with a shell built, whose loop reads the wire.
 */
uint8_t tiku_console_pumping(void);

/** @brief The pump process, defined in a build without a shell. */
struct tiku_process;
extern struct tiku_process tiku_console_process;

/*---------------------------------------------------------------------------*/
/* OBSERVABILITY                                                             */
/*---------------------------------------------------------------------------*/

typedef struct {
    uint32_t frames[TIKU_CONSOLE_CHANNELS]; /**< delivered, per channel */
    uint32_t stray_end;   /**< an END outside any frame that opened none */
    uint32_t oversize;    /**< frames dropped for outgrowing their buffer */
    uint32_t phantom;     /**< frames abandoned by the age guard */
} tiku_console_stats_t;

/** @brief The counters since boot. */
const tiku_console_stats_t *tiku_console_stats(void);

/**
 * @brief The channel table entry at @p slot, for a listing.
 * @return 0 and fills the outputs, or -1 for an empty slot
 */
int  tiku_console_channel_at(uint8_t slot, uint8_t *value, uint8_t *mask,
                             size_t *cap);

/** @brief Forget every channel, the decoder's state and the counters. */
void tiku_console_reset(void);

#endif /* TIKU_CONSOLE_H_ */
