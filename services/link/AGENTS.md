# services/link/

`tiku_link.h` is the contract a session rides on any medium: whole
messages, intact or not at all.  Three backends carry it.

## Console (`tiku_link_console.c`)

One marked channel (`0xF1` for the window session) on the console line
(`services/console/AGENTS.md`).  Inside the frame a 4-byte envelope
(`TIKU_LINK_CONSOLE_OVERHEAD`): a control byte (kind DATA / ACK / SYN, a
WANT_ACK bit), a sequence number, the message, a CRC-16 (CCITT-FALSE, high
byte first).  Rules that hold on every side, mirrored by the desktop's
`applications/kits/device/tiku_link_serial.c` and TikuBench's
`tikubench/core/frames.py`:

- A short or wrong-CRC frame is dropped whole and counted `bad`; nothing
  is delivered.
- A WANT_ACK frame is delivered once, in order, then acknowledged; a
  repeat up to `DUP_SPAN` (8) behind is answered again and not delivered;
  a stranger is rejected unanswered.
- A SYN restarts the receiver's own numbering at 0.
- An unpaced frame is delivered as it comes; a skipped sequence counts
  one `gap`.

The board never waits or paces: its frames go out unpaced.  The desktop
paces a window of 2 and retransmits (500 ms, 4 rounds), then reports a
fault so the channel re-greets.  The window session's channel takes
`TIKU_LINK_CONSOLE_SESSION_MTU` (256) bytes of message; a link's receive
buffer is message plus overhead.  `console` prints the `link:` counters.

## IP (`tikukits/net/link/tiku_kits_net_link_ip.c`)

A kit, because the kernel must not reach into `tikukits/`.  The session
rides a TCP connection the board dials; the frame is a 32-bit
little-endian length and the message, with no CRC, numbering or pacing,
since TCP delivers whole and in order.  Two rules:

- What the transport will not take now waits in the link's outbox for a
  flush timer.  Aborting a short write resets every greeting, because the
  TCP TX pool holds each segment until it is acknowledged.
- What a board sends (a window's display list) is far larger than what it
  takes (its receive buffer); one number cannot bound both.

`tiku_kits_net_tcp_init()` is idempotent and the link calls it; without
that call the segment pool does not exist and every send is refused.  The
TX pool's backing array is sized from `TX_SEG_BLOCK`, the descriptor's real
size rounded to the pool's alignment, because `tiku_pool_create()` strides
at the rounded size.

## BLE (`tiku_link_ble.c`)

The same session over the Nordic UART Service byte pipe, each message
behind its length.

## The shell on a link

The board's shell rides the same link as its windows (`SHELL_IN` 21 /
`SHELL_OUT` 22 in the session).  `applications/board/gui/tiku_draw_shell.c`
injects the text into the console (`tiku_console_inject()`) and steers each
command's output to whoever spoke last through a RAM `tiku_shell_io_t` in
front of the cable's, re-syncing the caller capability; replies go out as
messages of at most 200 bytes, the prompt flushed by a shell pump, and
nothing pumps the medium from `putc`.  A large node is read in pages
(`shell/AGENTS.md`, "Large replies"); the desk's `tiku_ns_load` walks
`/sys/vfs/manifest` that way.  On the desk the channel carries the shell
on a second socket pair; `boards_accept` mounts a dialled board under
`/devices/<name>` and the mount goes when the link reports a loss.

## Proof

On-target `link` category.  Host suites `link-console`, `link-fault`
(corrupt, truncate, drop, duplicate, flood, hold), `link-soak`, `link-ip`
(`namespace-paged` reads the manifest in pages) and `console`.  No suite
needs root: `tikubench/core/tcp_peer.py` ends the board's TCP in user space
over the SLIP wire and relays it to a real socket.  On the desk side,
`applications` `boardnet.script` has a real TikuDesktop started with
`-listen` take in the board that dials it.
