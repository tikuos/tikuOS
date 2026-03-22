# Last Session: TCP Over SLIP Bug — RESOLVED

## Status: FIXED — root cause was DTR-induced target reset in test script

## What Was Done

Implemented a full TCP stack for TikuOS under `tikukits/net/ipv4/`:
- `tiku_kits_net_tcp.h` (620 lines) — header with full API
- `tiku_kits_net_tcp.c` (~1550 lines) — implementation
- FRAM-backed TX retransmission pool + RX ring buffers
- Built-in echo service on port 7
- Integration with IPv4 dispatch, net process init, periodic timer
- TikuBench test suite (6 tests), presentation, examples

## The Bug

TCP handshake (SYN → SYN+ACK) works perfectly over SLIP/eZ-FET.
But the second exchange (DATA+ACK after the handshake) fails:
**the connection table entry is zeroed between the SYN+ACK send
and the DATA+ACK receive, without `tcp_free_conn` ever being
called.**

## Proven Facts

1. **TCP code works on loopback** — the on-device example
   (`examples/kits/net/example_net_tcp.c`) runs handshake, data
   echo, RST, and close correctly.

2. **SYN → SYN+ACK works over SLIP** — 10/10 pass in TikuBench.
   The ISS matches `tcp_gen_iss()` output, proving the connection
   IS created.

3. **The connection EXISTS after SYN+ACK** — sending a second SYN
   gets a retransmitted SYN+ACK with the SAME ISS (not a new
   connection).

4. **The eZ-FET is NOT the root cause** — UART challenge tests
   (`tests/uart/test_uart_ezfet_challenge.c`) prove:
   - 50-byte and 60-byte bursts after 500ms pause: PASS
   - Multi-round-trip: 3/3 PASS
   - Sustained bidirectional: 1000/1000 bytes, 0% loss
   - The eZ-FET CAN handle the TCP pattern at packet sizes

5. **Without `flush_serial`, the DATA+ACK gets no response**
   (eZ-FET blocks host→target writes after receiving SYN+ACK).
   Ping DOES work after SYN+ACK without flush. So ICMP gets
   through but TCP DATA doesn't — possibly size-related.

6. **With `flush_serial`, the DATA+ACK reaches the device**
   (RST proves it: `rst_seq = our_ack_value`). But
   `tcp_find_conn` returns NULL because `conn_table[0].active = 0`
   and `conn_table[0].state = 0 (CLOSED)`.

7. **`tcp_free_conn` was NEVER called** — `dbg_free_reason` stayed
   at 0. The connection struct was zeroed by something other than
   the free function.

8. **Stack overflow was ruled out** — BSS=792B with reduced UART
   buffer, stack=1256B. Didn't fix the issue.

## How to Reproduce

```bash
# Build and flash
make clean
make APP=net MCU=msp430fr5969 \
    EXTRA_CFLAGS="-DTIKU_KITS_NET_TCP_ENABLE=1"
make flash ...

# Run this Python snippet
python3 -c "
import sys, time, struct
sys.path.insert(0, 'TikuBench')
from tikubench.common.slip import slip_encode
from tikubench.common.packet import *
from tikubench.common.serial_port import open_serial, flush_serial, read_slip_frame

ser = open_serial('/dev/ttyACM1')

# SYN -> works, get SYN+ACK
syn = build_ipv4_tcp_packet(55555, 7, 0xA000, 0, TCP_FLAG_SYN, 4096, mss=88)
ser.write(slip_encode(syn))
ser.flush()
f, _ = read_slip_frame(ser, 5.0)
tcp = f[(f[0]&0xF)*4:]
srv_iss = struct.unpack('!I', tcp[4:8])[0]
print(f'SYN -> SYN+ACK  ISS={srv_iss:#x}')

# flush then DATA+ACK -> gets RST (connection zeroed)
flush_serial(ser)
data_ack = build_ipv4_tcp_packet(55555, 7, 0xA001, srv_iss+1,
    TCP_FLAG_ACK|TCP_FLAG_PSH, 4096, payload=b'Hello TCP!')
ser.write(slip_encode(data_ack))
ser.flush()
f2, _ = read_slip_frame(ser, 5.0)
# f2 will be a RST packet (flags=0x04)
ser.close()
"
```

## What to Debug with JTAG

The LaunchPad has a built-in JTAG debugger. Use Code Composer
Studio (CCS) or GDB:

### Step 1: Find conn_table address
```bash
/home/ambuj/tigcc/bin/msp430-elf-nm main.elf | grep conn_table
# Output: 00001f32 b conn_table
```

### Step 2: Set hardware watchpoint on active field
The `active` field is at offset 9 in the struct.
```
conn_table address:  0x1F32
active field:        0x1F32 + 9 = 0x1F3B
state field:         0x1F32 + 8 = 0x1F3A
```

In CCS: Debug → Breakpoints → Hardware Watchpoint → address
0x1F3B → Write → any value.

In GDB:
```
(gdb) watch *(uint8_t *)0x1F3B
```

### Step 3: Run the test
1. Flash the firmware
2. Set the watchpoint
3. Run the device
4. From the host, send the SYN (Python snippet above)
5. The watchpoint should fire TWICE:
   - First: `tcp_alloc_conn` sets `active = 1` (expected)
   - Second: something sets `active = 0` (THE BUG)
6. On the second fire, check the call stack — it will show
   exactly what code zeroed the connection.

### Step 4: Check what you find
Likely candidates:
- A `memset` overwriting conn_table from a nearby variable
- A pointer bug in the SLIP decoder or pool allocator
  writing to the wrong address
- `ipv4_get_buf` resetting `net_buf_len` causing the SLIP
  decoder to re-deliver a stale frame that triggers an
  unexpected code path
- `flush_serial`'s 4 SLIP END bytes (0xC0) causing the
  device to process a "packet" that somehow resets state

## Key File Locations

| File | Purpose |
|---|---|
| `tikukits/net/ipv4/tiku_kits_net_tcp.c` | TCP implementation (the bug is here) |
| `tikukits/net/ipv4/tiku_kits_net_tcp.h` | TCP header/API |
| `tikukits/net/ipv4/tiku_kits_net_ipv4.c` | IPv4 dispatch + net process (poll loop) |
| `tikukits/net/slip/tiku_kits_net_slip.c` | SLIP encoder/decoder |
| `tikukits/net/tiku_kits_net.h` | Common net config (TCP_ENABLE flag) |
| `apps/net/tiku_app_net.c` | Net app entry (currently only net process, syslog/NTP/CoAP disabled for debugging) |
| `tests/uart/test_uart_ezfet_challenge.c` | UART tests that prove eZ-FET is not the issue |
| `takenote.md` | Full debugging history |
| `examples/kits/net/example_net_tcp.c` | Loopback example (proves TCP code works) |

## Current State of tiku_app_net.c

The autostart was reduced to ONLY `tiku_kits_net_process` for
debugging (syslog/NTP/CoAP disabled). Restore the full list
after the bug is fixed:
```c
TIKU_AUTOSTART_PROCESSES(&tiku_kits_net_process,
                          &tiku_kits_time_ntp_process,
                          &tiku_kits_net_syslog_process,
                          &tiku_kits_net_coap_process);
```

## Current State of tiku_kits_net_tcp.c

- All debug instrumentation (`dbg_*` variables) has been removed
- ISS counter starts at `0x10000` (normal)
- `RTO_INIT` is `200` ticks (~10 seconds, increased from 40 for
  debugging — consider reverting to 40 after fix)
- `tcp_send_rst_reply` has clean window/urgent fields (no debug)
- The TCP echo service is enabled (`TCP_ECHO_ENABLE = 1`)

## Resolution (2026-03-22)

The TCP firmware was correct all along. The "connection table
zeroed" bug was caused by the Python test script's `reopen()`
function, which closed and reopened the serial port. On Linux,
this toggled the DTR line (even with `dtr=False` and `stty
-hupcl`), causing the eZ-FET to reset the MSP430 target and
clear all SRAM including conn_table.

**Fix:** Replaced `reopen()` with inline `flush_serial()` in
`tools/tiku_slip_tcp_test.py`. TCP now works over SLIP:
- SYN → SYN+ACK (handshake)
- ACK+DATA → ACK (data accepted, ack matches ISS+1+payload_len)
- FIN → ACK (graceful close)

Echo data is not received due to eZ-FET one-directional transfer
limitation. An external UART adapter would bypass this.
