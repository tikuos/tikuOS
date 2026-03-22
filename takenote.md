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
