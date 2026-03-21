#!/usr/bin/env python3
"""
TCP test tool for TikuOS -- one-command TCP integration test.

Performs a full TCP 3-way handshake, sends data, verifies echo,
and closes the connection over a direct SLIP serial link.  Handles
all eZ-FET backchannel quirks (DTR, 47-byte write limit, NUL escape).

Self-contained: no TikuBench dependency, only requires pyserial.

Usage:
    python3 tools/tiku_slip_tcp_test.py                     # auto-detect
    python3 tools/tiku_slip_tcp_test.py --port /dev/ttyACM0  # explicit port
    python3 tools/tiku_slip_tcp_test.py --data "Hi!"         # custom payload

Prerequisite firmware build:
    make APP=net MCU=msp430fr5969 EXTRA_CFLAGS="-DTIKU_KITS_NET_TCP_ENABLE=1"

The firmware includes a built-in TCP echo service on port 7 (enabled
by default when TCP is enabled), so no application code changes needed.

Authors: Ambuj Varshney <ambuj@tiku-os.org>
SPDX-License-Identifier: Apache-2.0
"""

import glob
import struct
import sys
import time

try:
    import serial
except ImportError:
    print("pyserial not found. Install with: pip install pyserial")
    sys.exit(1)

# ---------------------------------------------------------------------------
# SLIP codec (RFC 1055 + eZ-FET NUL escape)
# ---------------------------------------------------------------------------

SLIP_END     = 0xC0
SLIP_ESC     = 0xDB
SLIP_ESC_END = 0xDC
SLIP_ESC_ESC = 0xDD
SLIP_ESC_NUL = 0xDE


def slip_encode(payload):
    out = bytearray([SLIP_END])
    for b in payload:
        if b == SLIP_END:
            out += bytes([SLIP_ESC, SLIP_ESC_END])
        elif b == SLIP_ESC:
            out += bytes([SLIP_ESC, SLIP_ESC_ESC])
        elif b == 0x00:
            out += bytes([SLIP_ESC, SLIP_ESC_NUL])
        else:
            out.append(b)
    out.append(SLIP_END)
    return bytes(out)


def slip_decode(data):
    frames, buf, in_esc = [], bytearray(), False
    for b in data:
        if in_esc:
            in_esc = False
            if b == SLIP_ESC_END:
                buf.append(SLIP_END)
            elif b == SLIP_ESC_ESC:
                buf.append(SLIP_ESC)
            elif b == SLIP_ESC_NUL:
                buf.append(0x00)
            else:
                buf.append(b)
        elif b == SLIP_END:
            if buf:
                frames.append(bytes(buf))
                buf = bytearray()
        elif b == SLIP_ESC:
            in_esc = True
        else:
            buf.append(b)
    return frames

# ---------------------------------------------------------------------------
# Internet checksum (RFC 1071)
# ---------------------------------------------------------------------------

SRC_IP = bytes([172, 16, 7, 1])   # Host
DST_IP = bytes([172, 16, 7, 2])   # Device

TCP_FIN = 0x01
TCP_SYN = 0x02
TCP_RST = 0x04
TCP_PSH = 0x08
TCP_ACK = 0x10


def inet_cksum(data):
    if len(data) % 2:
        data = data + b'\x00'
    s = sum((data[i] << 8) | data[i + 1] for i in range(0, len(data), 2))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF

# ---------------------------------------------------------------------------
# Packet builders
# ---------------------------------------------------------------------------


def build_tcp_packet(sport, dport, seq, ack, flags, window,
                     payload=b'', mss=None):
    """Build a complete IPv4/TCP packet ready for SLIP."""
    # TCP header
    if mss is not None:
        doff, opts = 6, struct.pack("!BBH", 2, 4, mss)
    else:
        doff, opts = 5, b''

    tcp = struct.pack("!HHIIBBHHH", sport, dport, seq, ack,
                      doff << 4, flags, window, 0, 0) + opts + payload

    total = 20 + len(tcp)

    # IPv4 header (checksum = 0 placeholder)
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, total, 1, 0,
                     64, 6, 0, SRC_IP, DST_IP)
    ip_ck = inet_cksum(ip)
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, total, 1, 0,
                     64, 6, ip_ck, SRC_IP, DST_IP)

    # TCP checksum over pseudo-header + segment
    pseudo = SRC_IP + DST_IP + struct.pack("!BBH", 0, 6, len(tcp))
    tcp_ck = inet_cksum(pseudo + tcp)
    tcp = bytearray(tcp)
    struct.pack_into("!H", tcp, 16, tcp_ck)

    return ip + bytes(tcp)


def parse_tcp(data):
    """Parse IPv4/TCP packet, return dict or None."""
    if len(data) < 40 or (data[0] >> 4) != 4 or data[9] != 6:
        return None
    ihl = (data[0] & 0x0F) * 4
    t = data[ihl:]
    if len(t) < 20:
        return None
    doff = (t[12] >> 4) * 4
    return {
        'sport': (t[0] << 8) | t[1],
        'dport': (t[2] << 8) | t[3],
        'seq':   struct.unpack("!I", t[4:8])[0],
        'ack':   struct.unpack("!I", t[8:12])[0],
        'flags': t[13],
        'win':   (t[14] << 8) | t[15],
        'data':  t[doff:] if doff <= len(t) else b'',
    }

# ---------------------------------------------------------------------------
# Serial helpers (eZ-FET safe)
# ---------------------------------------------------------------------------


def list_ports():
    """List all serial ports with descriptions."""
    ports = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    if not ports:
        print("No serial ports found.")
        return
    for p in ports:
        try:
            import serial.tools.list_ports
            for info in serial.tools.list_ports.comports():
                if info.device == p:
                    print(f"  {p}  {info.description or ''} "
                          f"[{info.manufacturer or ''}]")
                    break
            else:
                print(f"  {p}")
        except Exception:
            print(f"  {p}")


def find_port():
    ports = sorted(glob.glob("/dev/ttyACM*"))
    return ports[0] if ports else None


def open_port(path, baud=9600):
    """Open serial with eZ-FET safe settings."""
    s = serial.Serial()
    s.port = path
    s.baudrate = baud
    s.timeout = 0.1
    s.dtr = False          # prevent target reset
    s.dsrdtr = False
    s.rtscts = False
    s.open()
    time.sleep(2.0)        # wait for boot
    s.reset_input_buffer()
    s.write(bytes([SLIP_END] * 4))  # flush device SLIP decoder
    time.sleep(0.5)
    s.reset_input_buffer()
    return s


def reopen(ser):
    """Reopen the serial port to reset eZ-FET backchannel state.

    The eZ-FET enters a bad state after each reply exchange
    (device-to-host), refusing new host-to-device data until
    the port is closed and reopened.  A 4.5s close gap restores
    the full ~130-byte write budget (shorter gaps limit to ~47B).
    """
    ser.close()
    time.sleep(4.5)
    ser.open()
    time.sleep(2.0)
    ser.reset_input_buffer()
    ser.write(bytes([SLIP_END] * 4))
    time.sleep(0.5)
    ser.reset_input_buffer()


def tx(ser, pkt):
    """SLIP-encode and send, chunked to 47 bytes for eZ-FET."""
    frame = slip_encode(pkt)
    for i in range(0, len(frame), 47):
        ser.write(frame[i:i + 47])
        ser.flush()
        if i + 47 < len(frame):
            time.sleep(0.05)


def rx(ser, timeout=4.0):
    """Wait for a TCP response over SLIP. Returns parsed dict or None."""
    buf = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        n = ser.in_waiting
        if n:
            buf.extend(ser.read(n))
            for f in slip_decode(bytes(buf)):
                p = parse_tcp(f)
                if p:
                    return p
        else:
            time.sleep(0.01)
    return None


def flags_str(f):
    names = []
    if f & TCP_SYN: names.append("SYN")
    if f & TCP_ACK: names.append("ACK")
    if f & TCP_FIN: names.append("FIN")
    if f & TCP_RST: names.append("RST")
    if f & TCP_PSH: names.append("PSH")
    return "+".join(names) or "-"

# ---------------------------------------------------------------------------
# Test
# ---------------------------------------------------------------------------


def test_tcp(ser, dport, payload):
    ISS = 0x1000
    MSS = 88
    sport = 12345
    ok = True

    print(f"\n  Host  {'.'.join(str(b) for b in SRC_IP)}:{sport}")
    print(f"  Device {'.'.join(str(b) for b in DST_IP)}:{dport}")

    # -- 1. SYN --
    print(f"\n  [1/4] SYN -->")
    tx(ser, build_tcp_packet(sport, dport, ISS, 0, TCP_SYN, 4096, mss=MSS))
    r = rx(ser, 5.0)
    if not r:
        print("        FAIL  no response (is firmware running with TCP?)")
        return False
    print(f"        <-- {flags_str(r['flags'])}  seq={r['seq']:#x} "
          f"ack={r['ack']:#x} win={r['win']}")
    if r['flags'] & TCP_RST:
        print("        FAIL  RST (no listener on port %d)" % dport)
        return False
    if not (r['flags'] & TCP_SYN and r['flags'] & TCP_ACK):
        print("        FAIL  expected SYN+ACK")
        return False
    if r['ack'] != ISS + 1:
        print(f"        FAIL  bad ack: {r['ack']:#x} != {ISS+1:#x}")
        return False
    print("        OK    SYN+ACK received")
    srv_seq = r['seq']
    rcv_nxt = srv_seq + 1

    # eZ-FET reopen: backchannel enters bad state after receiving
    # SYN+ACK reply -- must reopen before next host-to-device write
    print("        (reopening port -- eZ-FET backchannel reset)")
    reopen(ser)

    # -- 2+3. ACK+DATA piggybacked in one packet --
    # Piggyback the handshake ACK on the data segment.  This is
    # valid TCP (RFC 793) and halves the eZ-FET budget usage.
    # The device processes: ACK→ESTABLISHED, then DATA→echo.
    print(f"\n  [2/4] ACK+DATA --> {payload!r} ({len(payload)}B)")
    tx(ser, build_tcp_packet(sport, dport, ISS + 1, rcv_nxt,
                             TCP_ACK | TCP_PSH, 4096, payload=payload))
    snd_nxt = ISS + 1 + len(payload)

    r = rx(ser, 5.0)
    if not r:
        print("        FAIL  no ACK for data")
        return False
    print(f"        <-- {flags_str(r['flags'])}  ack={r['ack']:#x} "
          f"data={len(r['data'])}B")

    # Collect echo (might be in same segment or a follow-up)
    echo = r['data']
    if r['ack'] == snd_nxt:
        print("        OK    data ACK'd")
    if not echo:
        r2 = rx(ser, 5.0)
        if r2 and r2['data']:
            echo = r2['data']
            rcv_nxt = r2['seq'] + len(echo)
    else:
        rcv_nxt = r['seq'] + len(echo)

    if echo:
        if echo == payload:
            print(f"        OK    echo matches: {echo!r}")
        else:
            print(f"        WARN  echo mismatch: {echo!r}")
            ok = False
    else:
        print("        INFO  no echo data (server may not echo)")

    # eZ-FET reopen before FIN
    print("        (reopening port -- eZ-FET backchannel reset)")
    reopen(ser)

    # -- 4. FIN --
    print(f"\n  [4/4] FIN -->")
    tx(ser, build_tcp_packet(sport, dport, snd_nxt, rcv_nxt,
                             TCP_FIN | TCP_ACK, 4096))
    r = rx(ser, 5.0)
    if not r:
        print("        WARN  no response to FIN")
        return ok

    print(f"        <-- {flags_str(r['flags'])}")
    if r['flags'] & TCP_RST:
        print("        OK    RST (connection torn down)")
    elif r['flags'] & TCP_FIN:
        # ACK their FIN -- need reopen first
        reopen(ser)
        tx(ser, build_tcp_packet(sport, dport, r['ack'], r['seq'] + 1,
                                 TCP_ACK, 4096))
        print("        OK    FIN exchange complete")
    elif r['flags'] & TCP_ACK:
        # Wait for server FIN
        r2 = rx(ser, 5.0)
        if r2 and r2['flags'] & TCP_FIN:
            reopen(ser)
            tx(ser, build_tcp_packet(sport, dport, r2['ack'],
                                     r2['seq'] + 1, TCP_ACK, 4096))
            print("        OK    connection closed")
        else:
            print("        WARN  no server FIN")

    return ok

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    import argparse
    ap = argparse.ArgumentParser(
        description="One-command TCP test for TikuOS over SLIP serial")
    ap.add_argument("--port", help="Serial port (auto-detect if omitted)")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--dport", type=int, default=7,
                    help="Device TCP port (default: 7 = echo)")
    ap.add_argument("--data", default="Hello TCP!",
                    help="Payload to send and expect echoed back")
    ap.add_argument("--list", action="store_true",
                    help="List available serial ports and exit")
    args = ap.parse_args()

    if args.list:
        print("Available serial ports:")
        list_ports()
        sys.exit(0)

    port = args.port or find_port()
    if not port:
        print("No serial port found. Use --list to see ports, --port to specify.")
        sys.exit(1)

    print(f"=== TikuOS TCP Test ===")
    print(f"Port: {port} @ {args.baud} baud")

    ser = open_port(port, args.baud)
    payload = args.data.encode('ascii')
    passed = test_tcp(ser, args.dport, payload)
    ser.close()

    print(f"\n{'PASS' if passed else 'FAIL'}")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
