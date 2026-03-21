# TakeNote

## eZ-FET Backchannel UART Limitations (discovered 2026-03-20)

The eZ-FET debug probe's backchannel UART (used for SLIP-over-serial
communication between the host and MSP430 target) has three
undocumented hardware/firmware limitations that affect integration
testing:

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

Frames ≤47 SLIP bytes are delivered reliably after reopen.  Frames
≥49 SLIP bytes cause the eZ-FET to go completely silent (both
directions) for ~8 seconds.

This means multi-cycle DHCP exchanges (OFFER on connection 1, ACK on
connection 2) cannot both deliver data to the device.  The test
validates the full DORA handshake up to REQUEST but treats ACK
delivery and the post-ACK ping as informational.

### 3. Any host-to-target write throttles target-to-host

Even a 4-byte SLIP END write during `reopen_serial()` degrades the
target-to-host direction severely enough to corrupt large incoming
frames (~500 SLIP bytes for DHCP DISCOVER/REQUEST).  The test uses a
"read-only reopen" (close, sleep, open, reset buffer — no SLIP END
write) when it needs to capture device frames after a prior write.

### Workaround patterns for test scripts

| Operation | Pattern |
|---|---|
| Read device frame | Read-only reopen (skip SLIP END write) |
| Write small packet (<130 B) then read reply | Standard reopen, write+read in one cycle (like NTP ping) |
| Write large packet (>130 B) | Use compact format; write on the **first** connection only |
| Multi-step exchange (DHCP DORA) | OFFER on first connection, read-only reopen for REQUEST, ACK is best-effort |

### Affected files

- `tikukits/net/ipv4/tiku_kits_net_dhcp.h` — compact format offset constants
- `tikukits/net/ipv4/tiku_kits_net_dhcp.c` — `dhcp_recv_cb()` accepts both standard and compact payloads
- `TikuBench/tikubench/common/packet.py` — `build_compact_dhcp_reply()`, `compact=True` parameter
- `TikuBench/tikubench/net/tests/test_dhcp_exchange.py` — eZ-FET-aware test flow
- `TikuBench/tikubench/common/serial_port.py` — `reopen_serial()` (standard reopen with SLIP END write)

### 4. No full-duplex: target TX kills remaining host-to-target data (discovered 2026-03-21)

When the host sends a burst of data (e.g. two SLIP frames back-to-back),
the eZ-FET begins forwarding bytes to the target UART.  If the target
**starts transmitting a reply** (target→host) before the eZ-FET has
finished forwarding all host→target bytes, the eZ-FET **drops the
remaining host→target data** and switches to buffering target→host data.

This makes **TCP unusable over the eZ-FET backchannel** for anything
beyond a single exchange:

1. Host sends SYN (~57 SLIP bytes) → eZ-FET forwards to target ✓
2. Target processes SYN, starts sending SYN+ACK → eZ-FET switches direction
3. Any remaining host→target bytes (e.g. DATA+ACK) are **dropped**
4. Host receives SYN+ACK but device never gets the DATA+ACK

**Attempted workarounds that failed:**

| Approach | Result |
|---|---|
| Send ACK+DATA after reopen | Post-reopen budget is ~47 SLIP bytes; TCP packets are ~55+ bytes (0x00 escaping). eZ-FET goes silent for 8s. |
| `flush_serial` (no reopen) between exchanges | Partially works — packet reaches device but connection lookup fails (RST returned). Likely corrupted SLIP framing. |
| `reopen_serial_full` (4.5s close gap) | Same 47-byte budget limit applies. |
| Combined SYN+DATA in one `ser.write()` | eZ-FET drops DATA portion once target sends SYN+ACK. |
| Predicted ISS blind burst | Same as above — target starts TX before host burst completes. |

**Consequence:** TCP integration tests over eZ-FET are limited to
single-exchange patterns:

- **Works:** SYN → SYN+ACK (handshake verification, MSS option,
  checksum validation)
- **Works:** SYN to unbound port → RST+ACK
- **Works:** Stale ACK → RST
- **Works:** RST → silence (correctly ignored)
- **Does NOT work:** Full TCP session (handshake + data + close)

**Solution:** Use an external **FTDI or CP2102 USB-UART adapter**
connected directly to MSP430 UART pins (P2.0 TXD, P2.1 RXD),
bypassing the eZ-FET backchannel entirely.  External adapters
support true full-duplex UART with no write budget or direction-
switching limitations.

### Affected files (TCP)

- `TikuBench/tikubench/net/tcp.py` — TCP-over-SLIP helper with eZ-FET workaround code
- `TikuBench/tikubench/net/tests/test_tcp_echo.py` — gracefully skips data exchange on eZ-FET
- `TikuBench/tikubench/net/tests/test_tcp_handshake.py` — single-exchange, works on eZ-FET
- `TikuBench/tikubench/net/tests/test_tcp_rst_*.py` — single-exchange RST tests
- `tools/tiku_slip_tcp_test.py` — standalone TCP test script

### Known eZ-FET firmware bugs (prior knowledge)

- **0x00 triggers target reset:** Two consecutive NUL bytes on the
  backchannel UART cause the eZ-FET to reset the target.  Mitigated by
  SLIP NUL escaping (ESC + 0xDE for every 0x00 byte).
- **Port must reopen between exchanges:** The backchannel stops
  forwarding after one complete read-then-write exchange.
