# Telnet CLI Session Notes (2026-03-24)

## Goal

Add a TCP listener on port 23 that wires to the existing CLI parser.
CLI reads from tcp_recv, writes to tcp_send. When connection closes,
listen again.

## What was implemented

### New files
- `apps/cli/tiku_cli_io_tcp.h` — TCP backend header
- `apps/cli/tiku_cli_io_tcp.c` — TCP backend: listen on port 23,
  putc/rx_ready/getc wired to tcp_send/tcp_read, IAC filtering,
  \r\n collapsing, TX buffer with MSS-aware chunked flush

### Modified files
- `apps/cli/tiku_cli_config.h` — added `TIKU_CLI_TCP_ENABLE` flag (default 0)
- `apps/cli/tiku_cli.c` — TCP lifecycle in the CLI process:
  init TCP listener, detect connect/disconnect, swap backend,
  print banner on new connection, autostart net process alongside CLI
- `Makefile` — added `tiku_cli_io_tcp.c` to APP=cli section

### Build command
```bash
make clean && make flash APP=cli MCU=msp430fr5969 \
     EXTRA_CFLAGS="-DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_CLI_TCP_ENABLE=1 -DTIKU_KITS_NET_SLIP_ESC_NUL_ENABLE=0"
```

## What worked

- Code compiles and links cleanly (zero warnings from our code)
- Telnet connected successfully once and the banner appeared:
  ```
  --- TikuOS Telnet Shell ---
  Type 'help' for available commands.
  tikuOS>
  ```
- Typing commands echoed correctly (characters appeared)
- The hardware setup (FTDI on /dev/ttyUSB0, J13 wiring, slattach
  bridge) is confirmed working — example 18 (http_direct) works
  with this exact setup

## Issues encountered and fixes applied

### 1. Stale build artifacts
`make` without `make clean` first linked an empty binary.
**Fix:** always `make clean` first.

### 2. SLIP NUL escaping breaks kernel slattach
TikuOS escapes 0x00 as [0xDB, 0xDE] (eZ-FET workaround). Linux
kernel SLIP only knows RFC 1055 and decodes this as literal 0xDE,
corrupting every IP packet.
**Fix:** `-DTIKU_KITS_NET_SLIP_ESC_NUL_ENABLE=0` in build flags.
Documented in `kintsugi/takenote.md`.

### 3. Loose FTDI wire
Caused 100% ping loss. Wasted debugging time analyzing code.
**Fix:** Re-seated the wire. Hardware always first.

### 4. TX pool exhaustion — command output lost
Each `\n` in CLI output triggered a tcp_send(), exhausting the
4-segment TX pool in one poll cycle (before ACKs could free slots).
Commands executed but their output was silently dropped.
**Fix:** Removed per-line flush. tcp_putc() only flushes when
buffer is full. Explicit flush at end of each poll cycle. Flush
now chunks by MSS and preserves unsent data for next cycle.

### 5. CLOSE_WAIT leak — reconnection failed
When the telnet client disconnects (sends FIN), the device enters
CLOSE_WAIT but never sends its own FIN. The connection slot stays
occupied forever, blocking new connections.
**Fix:** `tiku_cli_io_tcp_is_connected()` now checks for
CLOSE_WAIT state and calls tcp_close() to complete the teardown.

## Where we left off — what to debug next

**Ping stopped working after the last flash.** The telnet session
was working earlier in the session (banner appeared, echo worked).
After the CLOSE_WAIT fix was flashed, ping fails again.

### Most likely cause: SLIP bridge desync after flash

mspdebug resets the target on every flash. The SLIP bridge
(slattach) may lose sync if it was running during the flash.
The standard recovery is:

```bash
sudo killall slattach
sudo tools/slip_bridge.sh /dev/ttyUSB0
# Press RESET on LaunchPad
# Wait 3 seconds
ping -c 3 172.16.7.2
```

If this doesn't work, check the FTDI wiring again (a wire came
loose earlier in the session).

### If ping works but telnet doesn't respond

The banner was appearing but command output was lost. The TX pool
fix (flush only on buffer full, chunk by MSS) should resolve this.
If commands still produce no output:
- The TX buffer is 128 bytes, MSS is 88. A flush sends at most
  1-2 segments. The help command output is ~170 bytes = 2 segments.
  This should fit in the 4-segment pool.
- Check that `tiku_cli_io_tcp_flush()` is being called at the end
  of each CLI poll cycle (it is, in tiku_cli.c line 233).

### If telnet connects but can't reconnect after disconnect

The CLOSE_WAIT fix should handle this. After disconnect, wait
~5 seconds (TIME_WAIT expiry) before reconnecting. With 2
connection slots and the echo service disabled, both slots are
available for telnet.

## Key learnings for this codebase + FTDI + slattach

1. **Always use `-DTIKU_KITS_NET_SLIP_ESC_NUL_ENABLE=0`** with
   FTDI + slattach (kernel SLIP). Without it, every 0x00 byte
   in IP headers gets corrupted.

2. **FTDI wires are fragile** — always verify ping before debugging
   code. LED1 toggles on every received SLIP frame (net process).

3. **TCP TX pool is only 4 segments.** All sends in one CLI poll
   cycle share this pool. The net process can only free segments
   (via ACK processing) when the CLI process yields. So a single
   burst of CLI output must fit in 4 segments.

4. **TCP CLOSE_WAIT needs explicit close.** The TCP stack fires
   CLOSED event only after both sides complete the 4-way close.
   If the device doesn't call tcp_close() after peer FIN, the
   connection slot leaks.

5. **mspdebug flash resets the target.** Always restart the SLIP
   bridge after flashing.

## Files to review on resume

- `apps/cli/tiku_cli_io_tcp.c` — the full TCP backend implementation
- `apps/cli/tiku_cli.c` — the CLI process with TCP lifecycle hooks
- `apps/cli/tiku_cli_config.h` — the TIKU_CLI_TCP_ENABLE flag
- `kintsugi/takenote.md` — all eZ-FET/FTDI hardware gotchas
