#!/usr/bin/env python3
"""
TikuOS SLIP Ping

Diagnostic tool that sends ICMP echo requests to TikuOS over a direct serial
SLIP link, bypassing slattach and the Linux tty/network stack entirely.

Uses the same pyserial transport proven reliable by the UART test suite.
If pings succeed here but fail via slattach, the bug is in tty configuration.
If pings fail here too, the bug is in the firmware net process.

Usage:
    python3 tools/tiku_slip_ping.py
    python3 tools/tiku_slip_ping.py --port /dev/ttyACM4 --count 5
    python3 tools/tiku_slip_ping.py --baud 9600 --size 8 --verbose

Authors: Ambuj Varshney <ambuj@tiku-os.org>
SPDX-License-Identifier: Apache-2.0
"""

import argparse
import glob
import struct
import sys
import time

try:
    import serial
except ImportError:
    print("ERROR: pyserial is required.  Install with: pip install pyserial")
    sys.exit(1)

# ---------------------------------------------------------------------------
# ANSI colors
# ---------------------------------------------------------------------------

class C:
    BOLD   = "\033[1m"
    GREEN  = "\033[92m"
    RED    = "\033[91m"
    YELLOW = "\033[93m"
    CYAN   = "\033[96m"
    DIM    = "\033[2m"
    RESET  = "\033[0m"

def supports_color():
    return hasattr(sys.stdout, "isatty") and sys.stdout.isatty()

if not supports_color():
    for attr in dir(C):
        if not attr.startswith("_"):
            setattr(C, attr, "")

# ---------------------------------------------------------------------------
# SLIP constants (RFC 1055)
# ---------------------------------------------------------------------------

SLIP_END     = 0xC0
SLIP_ESC     = 0xDB
SLIP_ESC_END = 0xDC
SLIP_ESC_ESC = 0xDD
SLIP_ESC_NUL = 0xDE  # eZ-FET workaround: escape 0x00 to avoid reset

# ---------------------------------------------------------------------------
# SLIP codec
# ---------------------------------------------------------------------------

def slip_encode(payload):
    """SLIP-encode a packet: leading END + escaped payload + trailing END."""
    out = bytearray([SLIP_END])
    for b in payload:
        if b == SLIP_END:
            out.append(SLIP_ESC)
            out.append(SLIP_ESC_END)
        elif b == SLIP_ESC:
            out.append(SLIP_ESC)
            out.append(SLIP_ESC_ESC)
        elif b == 0x00:
            # eZ-FET workaround: consecutive 0x00 bytes trigger a
            # target reset on the backchannel UART.  Escape them.
            out.append(SLIP_ESC)
            out.append(SLIP_ESC_NUL)
        else:
            out.append(b)
    out.append(SLIP_END)
    return bytes(out)


def slip_decode(data):
    """SLIP-decode raw bytes, returning a list of decoded frames."""
    frames = []
    buf = bytearray()
    in_esc = False

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
                buf.append(b)  # protocol violation, literal
        elif b == SLIP_END:
            if len(buf) > 0:
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

def inet_checksum(data):
    """Compute the RFC 1071 ones-complement checksum over a byte sequence."""
    if len(data) % 2 == 1:
        data = data + b'\x00'
    s = 0
    for i in range(0, len(data), 2):
        s += (data[i] << 8) | data[i + 1]
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF

# ---------------------------------------------------------------------------
# Packet builders
# ---------------------------------------------------------------------------

# Default addresses: host = 172.16.7.1, device = 172.16.7.2
SRC_IP = bytes([172, 16, 7, 1])
DST_IP = bytes([172, 16, 7, 2])


def build_icmp_echo_request(seq, ident=0x5449, data_size=8):
    """Build an ICMP echo request (type 8, code 0) with given seq and data."""
    # ICMP header: type(1) code(1) checksum(2) id(2) seq(2)
    icmp_type = 8
    icmp_code = 0
    icmp_cksum = 0  # placeholder
    # Payload: sequential bytes
    payload = bytes(range(data_size)) if data_size > 0 else b''
    icmp = struct.pack("!BBHHH", icmp_type, icmp_code, icmp_cksum,
                       ident, seq) + payload
    # Compute checksum
    cksum = inet_checksum(icmp)
    icmp = struct.pack("!BBHHH", icmp_type, icmp_code, cksum,
                       ident, seq) + payload
    return icmp


def build_ipv4_packet(icmp_payload):
    """Wrap an ICMP payload in a minimal IPv4 header (no options, IHL=5)."""
    ver_ihl = 0x45
    tos = 0
    total_len = 20 + len(icmp_payload)
    ident = 0x0001
    flags_frag = 0x4000  # DF bit set, no fragment offset
    ttl = 64
    proto = 1  # ICMP
    hdr_cksum = 0  # placeholder

    hdr = struct.pack("!BBHHHBBH4s4s",
                      ver_ihl, tos, total_len, ident, flags_frag,
                      ttl, proto, hdr_cksum, SRC_IP, DST_IP)
    # Compute header checksum
    hdr_cksum = inet_checksum(hdr)
    hdr = struct.pack("!BBHHHBBH4s4s",
                      ver_ihl, tos, total_len, ident, flags_frag,
                      ttl, proto, hdr_cksum, SRC_IP, DST_IP)
    return hdr + icmp_payload

# ---------------------------------------------------------------------------
# Hex dump
# ---------------------------------------------------------------------------

def hexdump(data, prefix="  "):
    """Format data as a hex dump with ASCII sidebar."""
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hx = " ".join(f"{b:02x}" for b in chunk)
        asc = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
        lines.append(f"{prefix}{i:04x}  {hx:<48s}  {asc}")
    return "\n".join(lines)

# ---------------------------------------------------------------------------
# Serial port helpers
# ---------------------------------------------------------------------------

def auto_detect_port():
    """Find a TI LaunchPad serial port (ttyACM*)."""
    ports = sorted(glob.glob("/dev/ttyACM*"))
    if ports:
        return ports[0]
    return None


def read_slip_frame(ser, timeout=2.0):
    """Read bytes from serial until a complete SLIP frame is received.

    Returns the decoded frame bytes, or None on timeout.
    """
    raw = bytearray()
    deadline = time.monotonic() + timeout
    got_data = False

    while time.monotonic() < deadline:
        waiting = ser.in_waiting
        if waiting > 0:
            chunk = ser.read(waiting)
            raw.extend(chunk)
            # Check if we have a complete frame: data followed by END
            # (leading ENDs are just frame delimiters)
            for i, b in enumerate(raw):
                if b == SLIP_END and got_data:
                    # Decode everything up to and including this END
                    frames = slip_decode(raw[:i + 1])
                    if frames:
                        # Return first complete frame and any remaining bytes
                        return frames[0], bytes(raw[i + 1:])
                elif b != SLIP_END:
                    got_data = True
        else:
            time.sleep(0.005)

    return None, bytes(raw)

# ---------------------------------------------------------------------------
# Packet validation
# ---------------------------------------------------------------------------

def validate_echo_reply(reply, expected_seq, expected_ident):
    """Validate that a decoded frame is a valid ICMP echo reply.

    Returns (ok, description) tuple.
    """
    if len(reply) < 28:  # 20 IP + 8 ICMP minimum
        return False, f"too short ({len(reply)} bytes)"

    # Check IPv4 version
    if (reply[0] >> 4) != 4:
        return False, f"not IPv4 (ver={reply[0] >> 4})"

    ihl = (reply[0] & 0x0F) * 4
    if ihl < 20:
        return False, f"bad IHL ({ihl})"

    # Verify IPv4 header checksum
    hdr_cksum = inet_checksum(reply[:ihl])
    if hdr_cksum != 0:
        return False, f"bad IPv4 checksum (residual=0x{hdr_cksum:04x})"

    # Check protocol is ICMP
    proto = reply[9]
    if proto != 1:
        return False, f"not ICMP (proto={proto})"

    # Check source IP is the device
    src_ip = reply[12:16]
    if src_ip != DST_IP:
        return False, (f"unexpected source "
                       f"{src_ip[0]}.{src_ip[1]}.{src_ip[2]}.{src_ip[3]}")

    # ICMP validation
    icmp = reply[ihl:]
    if len(icmp) < 8:
        return False, f"ICMP too short ({len(icmp)} bytes)"

    icmp_type = icmp[0]
    if icmp_type != 0:
        return False, f"not echo reply (type={icmp_type})"

    icmp_cksum = inet_checksum(icmp)
    if icmp_cksum != 0:
        return False, f"bad ICMP checksum (residual=0x{icmp_cksum:04x})"

    # Check id and seq
    ident = (icmp[4] << 8) | icmp[5]
    seq = (icmp[6] << 8) | icmp[7]
    if ident != expected_ident:
        return False, f"wrong id (got=0x{ident:04x}, expected=0x{expected_ident:04x})"
    if seq != expected_seq:
        return False, f"wrong seq (got={seq}, expected={expected_seq})"

    return True, "ok"

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="SLIP ping tool for TikuOS (bypasses slattach)")
    parser.add_argument("--port", "-p", default=None,
                        help="Serial port (default: auto-detect ttyACM*)")
    parser.add_argument("--baud", "-b", type=int, default=9600,
                        help="Baud rate (default: 9600)")
    parser.add_argument("--count", "-c", type=int, default=3,
                        help="Number of pings (default: 3)")
    parser.add_argument("--size", "-s", type=int, default=8,
                        help="ICMP data bytes (default: 8)")
    parser.add_argument("--timeout", "-t", type=float, default=2.0,
                        help="Reply timeout in seconds (default: 2.0)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Show hex dumps of sent/received frames")
    parser.add_argument("--src-ip", default="172.16.7.1",
                        help="Source IP address (default: 172.16.7.1)")
    parser.add_argument("--dst-ip", default="172.16.7.2",
                        help="Destination IP address (default: 172.16.7.2)")
    parser.add_argument("--sniff", type=float, default=0, metavar="SECS",
                        help="Sniff mode: send 1 ping, log every RX byte "
                             "with timestamps for SECS seconds")
    parser.add_argument("--listen", type=float, default=0, metavar="SECS",
                        help="Listen-only mode: no TX, just log every RX "
                             "byte for SECS seconds (test heartbeat)")
    args = parser.parse_args()

    # Parse IP addresses
    global SRC_IP, DST_IP
    SRC_IP = bytes(int(x) for x in args.src_ip.split("."))
    DST_IP = bytes(int(x) for x in args.dst_ip.split("."))

    # Resolve port
    port = args.port or auto_detect_port()
    if port is None:
        print(f"{C.RED}ERROR: No serial port found. "
              f"Specify with --port{C.RESET}")
        sys.exit(1)

    dst_str = f"{DST_IP[0]}.{DST_IP[1]}.{DST_IP[2]}.{DST_IP[3]}"
    pkt_size = 20 + 8 + args.size  # IP header + ICMP header + data

    print(f"{C.BOLD}SLIP ping {dst_str}: "
          f"{pkt_size} bytes via {port} @ {args.baud}{C.RESET}")

    # Open serial port without toggling DTR (which resets the target
    # on eZ-FET LaunchPads).  We construct the Serial object in two
    # steps: set dtr=False BEFORE open() so the kernel never asserts
    # DTR on the CDC ACM device.
    try:
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = args.baud
        ser.bytesize = serial.EIGHTBITS
        ser.parity = serial.PARITY_NONE
        ser.stopbits = serial.STOPBITS_ONE
        ser.timeout = 0.1
        ser.xonxoff = False
        ser.rtscts = False
        ser.dsrdtr = False
        ser.dtr = False
        ser.open()
    except serial.SerialException as e:
        print(f"{C.RED}ERROR: Cannot open {port}: {e}{C.RESET}")
        sys.exit(1)

    # The eZ-FET resets the target when the CDC ACM port is opened
    # (DTR assertion).  Wait for the target to fully boot, then
    # drain all boot-glitch bytes before sending any SLIP frames.
    time.sleep(2.0)
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    # Send SLIP END bytes to reset the device's SLIP decoder state,
    # then drain any response (boot-glitch bytes, empty-frame echos)
    ser.write(bytes([SLIP_END, SLIP_END, SLIP_END, SLIP_END]))
    time.sleep(0.5)
    ser.reset_input_buffer()

    # ------------------------------------------------------------------
    # Sniff mode: send 1 ping, log every byte with timestamps
    # ------------------------------------------------------------------
    if args.sniff > 0:
        icmp = build_icmp_echo_request(1, 0x5449, args.size)
        pkt = build_ipv4_packet(icmp)
        frame = slip_encode(pkt)

        print(f"{C.CYAN}--- sniff mode: sending 1 ping, "
              f"recording for {args.sniff:.1f}s ---{C.RESET}")
        print(f"{C.DIM}TX SLIP frame ({len(frame)} bytes):{C.RESET}")
        print(hexdump(frame))

        t0 = time.monotonic()
        ser.write(frame)
        ser.flush()
        print(f"\n{C.BOLD}{'time_ms':>10s}  {'byte':>4s}  "
              f"{'hex':>4s}  note{C.RESET}")
        print("-" * 40)

        rx_bytes = bytearray()
        deadline = t0 + args.sniff
        while time.monotonic() < deadline:
            if ser.in_waiting > 0:
                b = ser.read(1)
                t_now = (time.monotonic() - t0) * 1000.0
                bval = b[0]
                rx_bytes.append(bval)
                note = ""
                if bval == SLIP_END:
                    note = "SLIP END"
                elif bval == SLIP_ESC:
                    note = "SLIP ESC"
                elif bval == 0x45:
                    note = "IPv4 ver+IHL"
                asc = chr(bval) if 0x20 <= bval < 0x7F else "."
                print(f"{t_now:10.1f}  0x{bval:02x}  {asc:>4s}  {note}")
            else:
                time.sleep(0.001)

        print("-" * 40)
        print(f"Total RX: {len(rx_bytes)} bytes in {args.sniff:.1f}s")
        if rx_bytes:
            print(f"{C.DIM}Raw hex:{C.RESET}")
            print(hexdump(rx_bytes))
            frames = slip_decode(rx_bytes)
            if frames:
                print(f"\n{C.GREEN}Decoded {len(frames)} SLIP frame(s):"
                      f"{C.RESET}")
                for i, f in enumerate(frames):
                    print(f"  Frame {i}: {len(f)} bytes")
                    print(hexdump(f, prefix="    "))
            else:
                print(f"{C.RED}No complete SLIP frames decoded{C.RESET}")
        ser.close()
        sys.exit(0)

    # ------------------------------------------------------------------
    # Listen-only mode: no TX, just capture RX (test heartbeat)
    # ------------------------------------------------------------------
    if args.listen > 0:
        print(f"{C.CYAN}--- listen mode: no TX, recording for "
              f"{args.listen:.1f}s ---{C.RESET}")
        print(f"  (firmware heartbeat sends '.' every ~1s)")
        print(f"\n{C.BOLD}{'time_ms':>10s}  {'byte':>4s}  "
              f"{'hex':>4s}  note{C.RESET}")
        print("-" * 40)

        rx_bytes = bytearray()
        t0 = time.monotonic()
        deadline = t0 + args.listen
        while time.monotonic() < deadline:
            if ser.in_waiting > 0:
                b = ser.read(1)
                t_now = (time.monotonic() - t0) * 1000.0
                bval = b[0]
                rx_bytes.append(bval)
                note = ""
                if bval == 0x2E:
                    note = "heartbeat"
                elif bval == SLIP_END:
                    note = "SLIP END"
                asc = chr(bval) if 0x20 <= bval < 0x7F else "."
                print(f"{t_now:10.1f}  0x{bval:02x}  {asc:>4s}  {note}")
            else:
                time.sleep(0.001)

        print("-" * 40)
        print(f"Total RX: {len(rx_bytes)} bytes in {args.listen:.1f}s")
        ser.close()
        sys.exit(0)

    # ------------------------------------------------------------------
    # Normal ping mode
    # ------------------------------------------------------------------
    ident = 0x5449  # "TI" in ASCII
    sent = 0
    received = 0
    rtt_list = []

    try:
        for seq in range(1, args.count + 1):
            # Build packet
            icmp = build_icmp_echo_request(seq, ident, args.size)
            pkt = build_ipv4_packet(icmp)
            frame = slip_encode(pkt)

            if args.verbose:
                print(f"\n{C.DIM}--- TX packet ({len(pkt)} bytes) ---{C.RESET}")
                print(hexdump(pkt))
                print(f"{C.DIM}--- TX SLIP frame ({len(frame)} bytes) ---{C.RESET}")
                print(hexdump(frame))

            # Send
            t_start = time.monotonic()
            ser.write(frame)
            ser.flush()
            sent += 1

            # Wait for reply
            reply, leftover = read_slip_frame(ser, timeout=args.timeout)
            t_end = time.monotonic()
            rtt_ms = (t_end - t_start) * 1000.0

            if reply is None:
                print(f"{C.RED}seq={seq}: timeout "
                      f"({args.timeout:.1f}s){C.RESET}")
                if args.verbose and leftover:
                    print(f"{C.DIM}--- Raw RX ({len(leftover)} bytes) ---"
                          f"{C.RESET}")
                    print(hexdump(leftover))
            else:
                if args.verbose:
                    print(f"{C.DIM}--- RX decoded ({len(reply)} bytes) ---"
                          f"{C.RESET}")
                    print(hexdump(reply))

                ok, desc = validate_echo_reply(reply, seq, ident)
                if ok:
                    received += 1
                    rtt_list.append(rtt_ms)
                    print(f"{C.GREEN}Reply from {dst_str}: "
                          f"seq={seq} time={rtt_ms:.1f}ms{C.RESET}")
                else:
                    print(f"{C.RED}seq={seq}: invalid reply: "
                          f"{desc}{C.RESET}")
                    if args.verbose:
                        print(hexdump(reply))

            # The eZ-FET backchannel enters a bad state after each
            # reply exchange.  Close and reopen the port to reset it.
            if seq < args.count:
                ser.close()
                time.sleep(0.5)
                ser.open()
                time.sleep(2.0)
                ser.reset_input_buffer()
                ser.write(bytes([SLIP_END, SLIP_END, SLIP_END, SLIP_END]))
                time.sleep(0.5)
                ser.reset_input_buffer()

    except KeyboardInterrupt:
        print()
    finally:
        ser.close()

    # Summary
    loss = 100.0 * (sent - received) / sent if sent > 0 else 0.0
    print(f"\n{C.BOLD}--- {dst_str} SLIP ping statistics ---{C.RESET}")
    print(f"{sent} sent, {received} received, "
          f"{C.RED if loss > 0 else C.GREEN}{loss:.0f}% loss{C.RESET}")
    if rtt_list:
        avg = sum(rtt_list) / len(rtt_list)
        mn = min(rtt_list)
        mx = max(rtt_list)
        print(f"rtt min/avg/max = {mn:.1f}/{avg:.1f}/{mx:.1f} ms")

    sys.exit(0 if received == sent else 1)


if __name__ == "__main__":
    main()
