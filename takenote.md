# TakeNote

## eZ-FET Backchannel UART Limitations (discovered 2026-03-20)

The eZ-FET debug probe's backchannel UART (used for SLIP-over-serial
communication between the host and MSP430 target) has several
undocumented hardware/firmware limitations that affect integration
testing.  These caused a multi-day debugging session where a working
TCP stack appeared completely broken.

---

### 1. ~130-byte host-to-target buffer limit

The eZ-FET can only forward approximately 130 bytes of host-to-target
data per serial connection cycle.  Writes larger than this are
silently truncated or dropped.

- **NTP responses (~134 SLIP bytes):** work reliably (just under the limit)
- **Standard DHCP replies (~322 SLIP bytes):** never reach the device
- **Compact DHCP replies (~132 SLIP bytes):** work reliably

This is why the DHCP client (`tiku_kits_net_dhcp.c`) accepts a
"compact" DHCP format where the 192-byte sname + file fields are
omitted (magic cookie at offset 44, options at offset 48).  The
compact format keeps OFFER/ACK packets under the eZ-FET's buffer
limit.

### 2. Reduced host-to-target write budget after reopen (~47 SLIP bytes)

The first `open_serial()` connection allows host-to-target writes up
to the ~130-byte buffer limit.  After any close/reopen cycle
(`reopen_serial`), the per-session write budget drops to approximately
**47 SLIP bytes** — enough for CoAP GET requests (no payload) but too
small for CoAP PUT/POST requests with payloads (~49-54 SLIP bytes).

Frames <=47 SLIP bytes are delivered reliably after reopen.  Frames
>=49 SLIP bytes cause the eZ-FET to go completely silent (both
directions) for ~8 seconds.

This means multi-cycle DHCP exchanges (OFFER on connection 1, ACK on
connection 2) cannot both deliver data to the device.  The test
validates the full DORA handshake up to REQUEST but treats ACK
delivery and the post-ACK ping as informational.

### 3. Any host-to-target write throttles target-to-host

Even a 4-byte SLIP END write during `reopen_serial()` degrades the
target-to-host direction severely enough to corrupt large incoming
frames (~500 SLIP bytes for DHCP DISCOVER/REQUEST).  The test uses a
"read-only reopen" (close, sleep, open, reset buffer -- no SLIP END
write) when it needs to capture device frames after a prior write.

### 4. No full-duplex: target TX kills remaining host-to-target data (discovered 2026-03-21)

When the host sends a burst of data (e.g. two SLIP frames back-to-back),
the eZ-FET begins forwarding bytes to the target UART.  If the target
**starts transmitting a reply** (target->host) before the eZ-FET has
finished forwarding all host->target bytes, the eZ-FET **drops the
remaining host->target data** and switches to buffering target->host data.

---

### 5. DTR-induced target reset on port open/close (discovered 2026-03-22)

**THIS IS THE MOST DANGEROUS BUG.  It cost 3 days of debugging.**

Opening or reopening the eZ-FET backchannel serial port (`/dev/ttyACM1`)
**resets the MSP430 target** by toggling the DTR line.  This happens
**even when all of these mitigations are applied:**

- `serial.Serial(dtr=False)` -- pyserial's DTR suppression
- `s.dsrdtr = False` -- disable DSR/DTR hardware flow control
- `stty -F /dev/ttyACM1 -hupcl` -- disable hang-up-on-close
- Setting DTR False before calling `s.open()`

**None of these prevent the reset.**  The Linux kernel and/or pyserial
still toggles DTR during the `open()` syscall on the CDC-ACM device.
The eZ-FET firmware interprets any DTR assertion as a target reset
command.

#### Why this is so dangerous

The reset clears all SRAM (BSS, stack, heap) on the target MSP430,
destroying all runtime state.  But from the host's perspective:

1. The serial port opens successfully
2. The UART starts working (device re-initializes UART during boot)
3. SLIP frames can be sent and received
4. It **looks like the device is still running the same session**

So if a test does SYN -> SYN+ACK -> reopen -> ACK+DATA, the ACK+DATA
goes to a **freshly booted device** with an empty connection table.
The device correctly sends RST (no matching connection).  This looks
exactly like a firmware memory corruption bug:

- Connection table is zeroed
- `tcp_free_conn` was never called
- Guard canaries around the table are intact (BSS init zeroes them too)
- No stack overflow visible

The test appears to prove a firmware bug, but the firmware is correct.
The device simply rebooted between the two exchanges.

#### How we proved it was a DTR reset

1. **FRAM boot counter:** Added a counter in FRAM (`.persistent` section,
   survives resets) that increments on every `tcp_init()` call.  After
   `open_port()`, the counter showed the device had booted TWICE (once
   from flash, once from the port open DTR toggle).

2. **FRAM connection diagnostic:** Wrote connection state to FRAM
   immediately after SYN+ACK send.  FRAM showed `active=1`,
   `state=SYN_RCVD` -- the connection was alive.  But by the time
   ACK+DATA arrived (after reopen), SRAM was zeroed by the reboot.

3. **Guard canaries:** Placed `0xBEEF` and `0xCAFE` in BSS immediately
   adjacent to `conn_table`.  Both were always intact, ruling out
   buffer overflow.  (They were also zeroed by the reboot, but re-set
   by `tcp_init` on the new boot.)

4. **FRAM free_conn counter:** `tcp_free_conn` was never called
   (counter stayed at 0), confirming the connection wasn't freed by
   the retransmission timer or any code path.

5. **The smoking gun:** Sending SYN + flush_serial + ACK+DATA **without
   any port close/reopen** produced a successful ACK response.  The
   TCP handshake worked perfectly.

#### The fix

Replace `reopen()` / `reopen_serial()` with `flush_serial()` in all
test scripts that need multi-exchange protocols (TCP, multi-step CoAP):

```python
# BAD: resets the target, destroys all SRAM state
def reopen(ser):
    ser.close()
    time.sleep(4.5)
    ser.open()          # <-- DTR toggle here resets MSP430!
    time.sleep(2.0)
    ser.reset_input_buffer()

# GOOD: re-syncs eZ-FET without resetting the target
def flush_serial(ser):
    time.sleep(0.3)
    ser.reset_input_buffer()
    ser.write(bytes([0xC0] * 4))   # 4 SLIP END bytes
    time.sleep(0.3)
    ser.reset_input_buffer()
```

The 4 SLIP END bytes (0xC0) serve two purposes:
1. Flush the eZ-FET's internal buffer, unblocking host->target writes
2. Reset the device's SLIP decoder state (consecutive ENDs = no-op frames)

#### Remaining eZ-FET limitations after the fix

Even with `flush_serial`, the eZ-FET has constraints:

- **Without flush:** After device sends a reply (e.g. SYN+ACK), the
  eZ-FET blocks subsequent host->target writes until flushed.
- **With flush:** Host->target writes work.  The TCP handshake succeeds
  and data is accepted.  But **echo/reply data from the device may not
  reach the host** because the eZ-FET's one-directional transfer
  behavior blocks the second device->host frame.
- **Full-duplex sessions:** Still limited.  An external UART adapter
  (FTDI, CP2102) connected directly to MSP430 UART pins (P2.0 TXD,
  P2.1 RXD) bypasses all eZ-FET limitations.

#### Affected files

- `tools/tiku_slip_tcp_test.py` -- replaced `reopen()` with inline flush
- `TikuBench/tikubench/common/serial_port.py` -- `reopen_serial()` and
  `flush_serial()` definitions
- Any future test script that uses close/reopen on the backchannel

#### Warning for mspdebug / JTAG debugger

`mspdebug` (via `ttyACM0`) also resets the target on every connect.
The `MSP430_OpenDevice` call in the JTAG library performs a device
reset.  This means:

- Reading SRAM via `mspdebug "md 0x..."` halts the CPU and reads
  memory correctly, BUT the subsequent `MSP430_Run` resumes the CPU
  after what amounts to a reset.
- The FIRST `mspdebug` read after a test shows valid SRAM state.
  The SECOND read shows post-reset state (all BSS zeroed).
- FRAM (`.persistent`) survives resets, but if `tcp_init` clears
  FRAM diagnostics on boot, the data is lost after the mspdebug-
  induced reset.

**Workaround:** When debugging with FRAM diagnostics, do NOT clear
FRAM in `tcp_init`.  Let the diagnostic data persist across resets
so it survives mspdebug connections.

---

### Workaround patterns for test scripts

| Operation | Pattern |
|---|---|
| Read device frame | Read-only reopen (skip SLIP END write) |
| Write small packet (<130 B) then read reply | flush_serial, then write+read (like TCP handshake) |
| Write large packet (>130 B) | Use compact format; write on the **first** connection only |
| Multi-step exchange (TCP, DHCP DORA) | Use flush_serial between exchanges; NEVER use reopen |
| Debug with mspdebug mid-test | Use FRAM variables; don't clear FRAM in init code |

### Known eZ-FET firmware bugs (summary)

| Bug | Trigger | Effect | Mitigation |
|---|---|---|---|
| 0x00 triggers target reset | Two consecutive NUL bytes on backchannel | Target MSP430 resets | SLIP NUL escaping (ESC + 0xDE) |
| DTR toggles target reset | Serial port open/close/reopen | Target MSP430 resets, all SRAM lost | Use flush_serial instead of reopen |
| ~130B write budget | Host->target write >130 bytes | Data silently truncated | Keep frames under 130B; use compact formats |
| ~47B post-reopen budget | Host->target write after reopen | eZ-FET goes silent for ~8s | Avoid reopen; use flush_serial |
| Write throttles reads | Any host->target write | Large device->host frames corrupted | Read-only reopen when capturing device frames |
| No full-duplex | Target TX during host->target | Remaining host->target bytes dropped | One exchange per direction; flush between |

### Affected files

- `tikukits/net/ipv4/tiku_kits_net_dhcp.h` -- compact format offset constants
- `tikukits/net/ipv4/tiku_kits_net_dhcp.c` -- `dhcp_recv_cb()` accepts both standard and compact payloads
- `TikuBench/tikubench/common/packet.py` -- `build_compact_dhcp_reply()`, `compact=True` parameter
- `TikuBench/tikubench/net/tests/test_dhcp_exchange.py` -- eZ-FET-aware test flow
- `TikuBench/tikubench/common/serial_port.py` -- `reopen_serial()`, `flush_serial()` definitions
- `tools/tiku_slip_tcp_test.py` -- standalone TCP test script (uses flush_serial)

### TCP test results after fix (2026-03-22)

```
=== TikuOS TCP Test ===
Port: /dev/ttyACM1 @ 9600 baud
Host  172.16.7.1:12345 -> Device 172.16.7.2:7

  [1/4] SYN -->
        <-- SYN+ACK  seq=0x1fa00 ack=0x1001 win=255
        OK    SYN+ACK received

  [2/4] ACK+DATA --> b'Hello TCP!' (10B)
        <-- ACK  ack=0x100b
        OK    data ACK'd (ack = ISS + 1 + payload_len)

  [4/4] FIN -->
        <-- ACK
        PASS
```

The TCP stack is correct.  Handshake, data transfer, and close all
work.  Echo data is not received due to eZ-FET one-directional
transfer limitation (not a firmware issue).

---

## FT232 (SparkFun FTDI Basic) Connection to MSP430FR5969 LaunchPad (2026-03-22)

An external FT232 UART adapter bypasses all eZ-FET backchannel bugs
(0x00 reset, 47-byte limit, DTR reset, no full-duplex).

### Hardware setup

| FT232 Pin | LaunchPad Pin           | Signal                     |
|-----------|-------------------------|----------------------------|
| RX        | J13 TXD (target side)   | MSP430 transmits → FT232   |
| TX        | J13 RXD (target side)   | FT232 transmits → MSP430   |
| GND       | GND                     | Common ground              |

**Do NOT connect FT232 VCC to the LaunchPad.**

### Critical wiring notes

1. **Connect at J13, not the BoosterPack headers.**  P2.0/P2.1
   (eUSCI_A0) are NOT routed to the BoosterPack headers.  The
   BoosterPack has P2.5/P2.6 (eUSCI_A1) — a different UART peripheral.

2. **Remove only TXD and RXD jumpers from J13.**  Leave V+, GND, RST,
   TST in place.  Connect FT232 to the **target side** (closer to
   MSP430) of the removed jumper pins.

3. **Power source jumper:** Ensure the LaunchPad power selection is set
   to eZ-FET (USB) power, not external.  If set to external power and
   no external supply is connected, removing TXD/RXD jumpers kills the
   target (it was being parasitically powered through the UART lines
   via ESD diodes — LED stops blinking, VCC reads 0V).

4. **Voltage level:** Use a **3.3V** FT232 board.  SparkFun makes two
   versions: green = 3.3V (DEV-09873), red = 5V (DEV-09716).  The
   "3.3V" label on the power pin exists on BOTH boards (it's just the
   regulator output).  Check I/O voltage by measuring the TX pin idle
   voltage with a multimeter.  5V I/O will damage the MSP430 (max
   input = VCC + 0.3V = 3.6V).

5. **Serial port:** FT232 enumerates as `/dev/ttyUSB0` (not ttyACM*).
   Needs `dialout` group membership or `chmod 666`.

### Firmware changes

- Reduced example runner delay from 20s to 2s in
  `examples/kits/example_kits_runner.c`.  The 20s delay was an eZ-FET
  workaround (host needed time to open the backchannel after flash).
  The FT232 port can be opened independently of flashing.

### Debugging timeline

The initial FT232 connection attempts failed with 0xFF garbage or no
output.  Root causes identified:

1. **Power source jumper** set to external instead of eZ-FET USB —
   MSP430 had no power with J13 UART jumpers removed
2. **BoosterPack header confusion** — P2.0/P2.1 are only on J13
3. **SparkFun board version** — user's red board turned out to be 3.3V
   despite red color (verified with logic analyzer: TX idle = 3.3V)

---

## MPU-Protected FRAM: Silent Write Failures (discovered 2026-03-23)

Writing to `.persistent` (FRAM) variables without calling
`tiku_mpu_unlock_nvm()` / `tiku_mpu_lock_nvm()` **silently fails**.
The MSP430's Memory Protection Unit blocks the write, fires a silent
NMI (when `MPUSEGIE` is set), and the FRAM retains its previous value.
No crash, no error return — the code keeps running as if the write
succeeded.

This caused the HTTP fetch examples (17 and 18) to show
`magic=0 status=0 body_len=0` in FRAM even though the TCP exchange
completed successfully.  The bug affected two separate write sites:

### Bug 1: HTTP parser body buffer writes

The HTTP parser (`tiku_kits_net_http_parser_feed`) writes body bytes
directly into the caller-supplied buffer via:

```c
p->body_buf[p->body_len++] = b;   /* line 187 of tiku_kits_net_http.c */
```

When `body_buf` points to a `.persistent` FRAM array (`resp_buf`), this
write is blocked by the MPU.  The parser's `body_len` counter (in SRAM)
increments correctly, but the FRAM buffer stays all zeros.

**Symptom:** `body_len=256` but picocom shows blank body between the
header and footer lines.

**Fix:** Wrap `parser_feed()` calls with MPU unlock in the caller:

```c
uint16_t saved = tiku_mpu_unlock_nvm();
tiku_kits_net_http_parser_feed(&parser, chunk, n);
tiku_mpu_lock_nvm(saved);
```

### Bug 2: FRAM metadata save

The example code wrote `resp_status`, `resp_body_len`, and `resp_magic`
directly to FRAM without MPU unlock:

```c
resp_status   = parser.status_code;   /* silently fails */
resp_body_len = parser.body_len;      /* silently fails */
resp_magic    = RESP_MAGIC;           /* silently fails */
```

**Symptom:** `magic=0` on every boot, device never detects a cached
response and always re-fetches.

**Fix:** Wrap with `tiku_mpu_unlock_nvm()` / `tiku_mpu_lock_nvm()`.

### Why this is insidious

1. The TCP stack's `rx_write()` correctly unlocks MPU before writing
   to its FRAM ring buffer — so TCP data reception works fine.
2. The SRAM-resident parser struct (`parser.status_code`,
   `parser.body_len`) is updated correctly — so diagnostic prints
   from SRAM show the right values.
3. Only the FRAM-resident copies (`resp_buf`, `resp_status`, etc.)
   are silently dropped.

This creates a confusing scenario where the TCP exchange succeeds,
the parser reports correct values, but FRAM is empty.

### Rule for future code

**Any write to a `.persistent` variable MUST be wrapped with
`tiku_mpu_unlock_nvm()` / `tiku_mpu_lock_nvm()`.**  The TCP stack
does this correctly (see `rx_write()` in `tiku_kits_net_tcp.c`).
Application code and examples must follow the same pattern.

**Quick grep to audit:**

```bash
# Find .persistent variables
grep -rn '__attribute__.*persistent' --include='*.c' --include='*.h'

# Then verify each one has a corresponding mpu_unlock before writes
```

### Affected files (fixed 2026-03-23)

- `examples/17_http_fetch/http_fetch.c` — added MPU unlock around
  `parser_feed()` calls and FRAM metadata save
- `examples/18_http_direct/http_direct.c` — same fixes

---

## SLIP NUL Escaping Breaks Kernel SLIP Driver (discovered 2026-03-23)

The TikuOS SLIP encoder escapes `0x00` bytes as `[0xDB, 0xDE]` — a
non-standard extension to work around an eZ-FET bug (two consecutive
NUL bytes trigger a target reset).  The Linux kernel's `slattach`
SLIP driver only understands standard RFC 1055 escaping and treats
`[0xDB, 0xDE]` as `0xDE` (literal byte), corrupting every `0x00` in
the IP packet.

**Symptom:** `tcpdump -i sl0` shows `truncated-ip - 56832 bytes
missing!` — the IP total length field `0x003A` (58) became `0xDE3A`
(56890) because both 0x00 bytes were replaced with 0xDE.

**Fix:** Added compile-time flag `TIKU_KITS_NET_SLIP_ESC_NUL_ENABLE`
(default 1 for eZ-FET compatibility).  Example 18 sets it to 0 via
`-DTIKU_KITS_NET_SLIP_ESC_NUL_ENABLE=0`.  The `case 0x00:` in
`slip_send()` is `#if`-guarded.

**Rule:** When using FT232/CP2102 + `slattach` (kernel SLIP), always
disable NUL escaping.  When using eZ-FET backchannel or the Python
gateway (which understands the custom escape), leave it enabled.

### Affected files

- `tikukits/net/tiku_kits_net.h` — new `SLIP_ESC_NUL_ENABLE` config
- `tikukits/net/slip/tiku_kits_net_slip.c` — `#if`-guarded NUL case
- `tools/run_example.py` — Example 18 passes `-DTIKU_KITS_NET_SLIP_ESC_NUL_ENABLE=0`

---

## Internet TCP Requires Larger MTU (discovered 2026-03-23)

The default MTU of 128 bytes is sufficient for local SLIP links (the
Python gateway sends small segments matching the device's MSS).  Real
internet servers (Cloudflare, etc.) may send larger segments:

- Cloudflare ignores MSS=100 and sends 200-byte TCP payloads
- With a 40-byte IP+TCP header, the total packet is 240 bytes
- The SLIP decoder silently drops frames exceeding `net_buf` (MTU)

**Symptom:** `tcpdump` shows the HTTP 200 response arriving at sl0,
but the device never ACKs it.  The server retransmits repeatedly,
then the device RSTs.  FRAM shows `status=0 body_len=0`.

**Fix:** Example 18 uses `-DTIKU_KITS_NET_MTU=300`, providing a
300-byte packet buffer.  This accommodates the 240-byte response
packets from Cloudflare.  Costs 172 extra bytes of SRAM.

**Rule:** For internet-facing examples (Example 18), always use
MTU >= 300.  For local gateway examples (Example 17), MTU=128 is
sufficient.

---

## Protothread Event-Driven Wakeup Pitfall (discovered 2026-03-23)

`TIKU_PROCESS_WAIT_EVENT_UNTIL(condition)` only evaluates `condition`
when the process receives an event.  Setting a flag (like `connected`)
from a callback does NOT wake the process — only timer events, poll
events, or explicitly posted events do.

**Symptom:** TCP handshake completes in <100ms, `connected=1` is set
by the callback, but the process sleeps for 60 seconds (waiting for
a one-shot timer).  The server times out and sends FIN before the
device sends the HTTP GET.

**Fix:** Replace one large timer with a short-interval polling loop:

```c
/* BAD: process sleeps until 60s timer fires */
tiku_timer_set_event(&t, TIKU_CLOCK_SECOND * 60);
TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER || flag);

/* GOOD: process checks flag every 100ms */
for (i = 0; i < 300 && !flag; i++) {
    tiku_timer_set_event(&t, TIKU_CLOCK_MS_TO_TICKS(100));
    TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);
}
```

This is the same pattern the DNS resolver uses for polling.

---

## HTTP Fetch Gateway: TCP Window Overrun (discovered 2026-03-23)

The Python gateway (`tools/http_fetch_gateway.py`) sent TCP data
segments without respecting the device's receive window (255 bytes,
from `TIKU_KITS_NET_TCP_RX_BUF_SIZE - 1`).  This caused two
cascading failures:

### The mechanism

1. Device's TCP RX ring buffer is 255 bytes.
2. Gateway sends 88-byte segments (MSS).  Segments 1-2 fit (176B).
   Segment 3 only partially fits: device accepts 79 of 88 bytes,
   filling the 255-byte buffer.
3. Device's `rcv_nxt` advances by 79 (the accepted portion).
4. Gateway's `our_seq` advances by 88 (the full segment size).
5. **Gap of 9 bytes** between gateway's seq and device's `rcv_nxt`.
6. The device's TCP stack only accepts **in-order segments**
   (`seg_seq == rcv_nxt`, line 810 of `tiku_kits_net_tcp.c`).
   All subsequent segments have `seg_seq != rcv_nxt` and are
   **silently dropped** with a duplicate ACK.

### The stale ACK problem

After fixing the gateway to track partial ACKs and rewind `our_seq`,
a second bug appeared: **stale window-update ACKs** from the device
were misinterpreted as responses to newly sent segments.

When the device's app reads from the TCP buffer, the TCP stack sends
a window-update ACK with the same `ack` number (no new data) but an
updated window size.  The gateway read this stale ACK and interpreted
`acked = peer_ack - sent_seq = 0` as "0 bytes accepted," triggering
unnecessary rewinds and retransmissions.

### The fix (applied to `tools/http_fetch_gateway.py`)

1. **Track actual ACK numbers:** After sending a segment, wait for
   an ACK where `peer_ack > sent_seq` (new data acknowledged).
   Skip ACKs where `peer_ack == sent_seq` (stale window updates).

2. **Rewind on partial accept:** If `acked < expected`, rewind
   `our_seq` and `offset` by the unaccepted portion.  Pause 1.5s
   to let the device's app drain the buffer.

3. **Handle window=0:** If all ACKs are stale and window=0, pause
   for drain.  If window>0 but still no data ACK, abort (timeout).

### Before/after

**Before fix:**
```
Partial ACK: 79/88B accepted, window=0, pausing for drain...
Partial ACK: 0/88B accepted, window=88, pausing for drain...
Partial ACK: 0/88B accepted, window=255, pausing for drain...
[repeats 4 more times, wasting ~9 seconds]
ACK timeout, aborting
```

**After fix:**
```
Partial ACK: 79/88B, window=0
Pausing for drain...
Sent 612 bytes in 8 segments
```

### Key constants

| Constant | Value | Location |
|---|---|---|
| `TIKU_KITS_NET_TCP_RX_BUF_SIZE` | 256 (255 usable) | `tiku_kits_net_tcp.h:268` |
| `TIKU_KITS_NET_TCP_MSS` | 88 | `tiku_kits_net_tcp.h` |
| Device TCP window (advertised) | 255 | `rcv_wnd = RX_BUF_SIZE - 1` |
| Gateway `DEVICE_MSS` | 88 | `http_fetch_gateway.py:43` |

### Affected files

- `tools/http_fetch_gateway.py` — rewrote data send loop with ACK
  tracking and stale ACK filtering

---

## Shared UART: Printf and SLIP Interference (discovered 2026-03-23)

When using the FT232 adapter (`/dev/ttyUSB0`) for SLIP networking,
`tiku_uart_printf()` output shares the **same physical UART**
(eUSCI_A0) as SLIP-encoded TCP/IP frames.  This causes two problems:

### Problem 1: Gateway sees printf as corrupt SLIP frames

The device's boot-time `printf` output (e.g., `[FRAM] magic=0...`)
is raw ASCII text injected into the UART TX stream.  The gateway's
SLIP decoder accumulates these bytes until the next `0xC0` (SLIP END)
delimiter, then delivers them as a "frame."  Since the bytes aren't
a valid IP packet, `parse_ip_tcp()` returns None and the frame is
dropped.

This is **harmless** as long as the printf happens before or after
the TCP exchange (the 2-second delay in both examples ensures this).
But if printf were called **during** a SLIP frame transmission, it
would corrupt the frame by injecting non-SLIP bytes mid-stream.

### Problem 2: Picocom shows garbled binary

When picocom is opened on `/dev/ttyUSB0` (with no gateway running),
the device's SLIP frames appear as garbled binary (`�E����...`).
The `0x45` byte (`E`) is the IPv4 version+IHL header — a recognizable
signature of raw IP packets leaking through.

Readable printf text is interleaved with binary SLIP data, making
the output confusing.

### Implications

- **Cannot use picocom for debugging while SLIP is active.**  The
  UART carries both printf text and SLIP binary; picocom shows both.
- **Gateway must tolerate non-TCP frames** (Frames #1, #2 in every
  run are printf debris).
- **No separate console channel** when using FT232.  The eZ-FET
  backchannel (ttyACM0/1) could theoretically serve as a console,
  but its J13 TXD/RXD jumpers are removed for FT232 operation.

### Workarounds

1. **LED indicators:** Both examples use LED1 = HTTP 200 success,
   LED2 = error.  Use LEDs instead of printf for real-time status.
2. **FRAM-cached results:** After a successful fetch, press RESET
   and open picocom.  The device detects `magic=0xBEEF` in FRAM
   and prints the cached response without doing another TCP exchange
   (no SLIP interference during the print).
3. **Suppress printf during SLIP:** Future work could disable printf
   when the net process is active, or route printf to eUSCI_A1
   (BoosterPack header) via a second UART adapter.

---

## Claude Code Edit Tool: Whitespace Accumulation Bug (2026-03-22)

When Claude Code edits files using the Edit tool (exact string
replacement), each replacement can leave 1-2 extra trailing blank
lines if the `old_string` / `new_string` boundaries don't precisely
match the surrounding whitespace.  Over many edits to the same file,
this causes blank lines to accumulate exponentially.

**Example:** `tests/tiku_test_config.h` grew from ~1,970 lines to
28,847 lines (94% blank) after dozens of edits across sessions.
Each `#define` block had ~133 blank lines after it.

### Why it happens

The Edit tool does exact string matching.  If the replacement text
has a trailing `\n` and the surrounding context also has a `\n`,
the result is `\n\n` (one extra blank line).  Repeat this 50 times
across sessions and each gap grows to 50+ blank lines.

Config headers like `tiku_test_config.h` are especially vulnerable
because they consist of many small `#define` blocks that get edited
individually and frequently (toggling test flags, adding new tests).

### Rules for future Claude instances

1. **After editing a file more than 3 times in one session**, run
   `wc -l` on it and compare to the expected size.  Config headers
   should be hundreds of lines, not thousands.

2. **If blank line bloat is detected**, fix it immediately:
   ```bash
   cat -s file.h > /tmp/clean.h && cp /tmp/clean.h file.h
   ```
   `cat -s` squeezes consecutive blank lines into a single blank.

3. **When writing Edit tool replacements**, include the exact
   surrounding blank lines in both `old_string` and `new_string`.
   Do not assume trailing newlines — match them explicitly.

4. **Files at risk:** Any file that gets frequent small edits:
   - `tests/tiku_test_config.h` (test flags)
   - `tiku.h` (platform config)
   - `apps/cli/tiku_cli_config.h` (CLI command flags)
   - `tikukits/net/tiku_kits_net.h` (network config)

5. **Prefer single-block edits** over multiple small edits to the
   same file.  If you need to change 5 `#define` values, do it in
   one Edit call with a larger `old_string` that spans all 5.
