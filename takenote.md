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

### 2. Single host-to-target write per session

After the first serial connection is closed and reopened, subsequent
connections cannot reliably deliver host-to-target data regardless of
packet size.  Only the **first** `open_serial()` connection in a test
session can successfully write to the device.

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

### Known eZ-FET firmware bugs (prior knowledge)

- **0x00 triggers target reset:** Two consecutive NUL bytes on the
  backchannel UART cause the eZ-FET to reset the target.  Mitigated by
  SLIP NUL escaping (ESC + 0xDE for every 0x00 byte).
- **Port must reopen between exchanges:** The backchannel stops
  forwarding after one complete read-then-write exchange.
