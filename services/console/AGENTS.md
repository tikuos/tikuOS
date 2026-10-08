# services/console/

The console line (the UART, or the USB CDC port on a native-USB build)
carries the shell's text and, between `0xC0` delimiters, SLIP-escaped
frames whose first byte names a channel.  `tiku_console.c` is the one
decoder and encoder: it un-escapes a frame once and hands it whole to the
channel registered for its first byte; bytes outside a frame are text.
Nothing here includes a kit header: a registrant brings its handler and
its own reassembly buffer.

- The SLIP command registers `(0x40, 0xF0, keep first byte)` with an MTU
  buffer while `slip` is on and hands frames to
  `tiku_kits_net_ipv4_input()`.
- The applications overlay registers `(0xF1, 0xFF, strip)` from
  `tiku_draw_init()`, called after a successful assembly by
  `tiku_boot_vfs_init()` (its one mainline hook, behind `TIKU_APPL_GUI`).

The shell reads keystrokes through `tiku_console_getc()`, which dispatches
every frame met on the way; a telnet client's bytes bypass it.
`tiku_console_inject()` feeds text ahead of the wire (the link-carried
shell uses it).

## Without a shell

A build with `TIKU_SHELL_ENABLE=0` runs a `Console` process of the
console's own that calls `tiku_console_pump()` every
`TIKU_CONSOLE_POLL_TICKS` while a channel or a text sink is registered: the
first registrant starts it, the last removal ends it, so an image that
registers nothing runs no process and arms no timer
(`tiku_console_pumping()` reports it).

## Decoder rules that must not change

- An END is a delimiter, never a toggle.
- The byte after an END selects the channel or is text; a stray or doubled
  END costs only itself.
- A frame open longer than `TIKU_CONSOLE_FRAME_TTL` (30 s; TLS pauses the
  pump for seconds mid-frame) is abandoned.
- A frame that outgrows its buffer is dropped whole.

The `console` shell command prints the channel table and the stray,
oversize and phantom counters; `console echo on` arms a loopback link on
`0xF2` for host suites.

## Proof

On-target `console` test category (`TikuBench/tests/kernel/console/`,
over a RAM wire); host suites `console` (text and frames interleaved on one
line) and `console-noshell` (a shell-less drawing-demo image over the port).
