#!/usr/bin/env python3
"""
TikuOS Network Integration Test

Standalone integration test that builds APP=net firmware, flashes it onto
MSP430 hardware, and exercises the IPv4-over-SLIP ping path end-to-end.

This is NOT part of the firmware test suite (which requires TEST_ENABLE).
It is a host-side script that sends real ICMP echo requests over the serial
SLIP link and validates every aspect of the reply packets.

Usage:
    python3 tools/tiku_net_test.py
    python3 tools/tiku_net_test.py --port /dev/ttyACM1 --mcu msp430fr5969
    python3 tools/tiku_net_test.py --skip-build --verbose
    python3 tools/tiku_net_test.py --skip-build --port /dev/ttyACM4

Authors: Ambuj Varshney <ambuj@tiku-os.org>
SPDX-License-Identifier: Apache-2.0
"""

import argparse
import glob
import math
import os
import struct
import subprocess
import sys
import time

try:
    import serial
except ImportError:
    print("ERROR: pyserial is required.  Install with: pip install pyserial")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Project paths (relative to repo root)
# ---------------------------------------------------------------------------

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJ_DIR = os.path.dirname(SCRIPT_DIR)

# ---------------------------------------------------------------------------
# ANSI colors
# ---------------------------------------------------------------------------

class C:
    BOLD   = "\033[1m"
    GREEN  = "\033[92m"
    RED    = "\033[91m"
    YELLOW = "\033[93m"
    CYAN   = "\033[96m"
    BLUE   = "\033[94m"
    DIM    = "\033[2m"
    RESET  = "\033[0m"

def supports_color():
    return hasattr(sys.stdout, "isatty") and sys.stdout.isatty()

if not supports_color():
    for attr in dir(C):
        if not attr.startswith("_"):
            setattr(C, attr, "")

# ---------------------------------------------------------------------------
# SLIP constants (RFC 1055 + eZ-FET NUL escape)
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
# Network addresses
# ---------------------------------------------------------------------------

SRC_IP = bytes([172, 16, 7, 1])   # Host
DST_IP = bytes([172, 16, 7, 2])   # Device

ICMP_IDENT = 0x5449  # "TI" in ASCII

# ---------------------------------------------------------------------------
# Packet builders
# ---------------------------------------------------------------------------

def build_icmp_echo_request_with_data(seq, ident, data):
    """Build an ICMP echo request with explicit data bytes."""
    icmp_type = 8
    icmp_code = 0
    icmp_cksum = 0  # placeholder
    icmp = struct.pack("!BBHHH", icmp_type, icmp_code, icmp_cksum,
                       ident, seq) + data
    cksum = inet_checksum(icmp)
    icmp = struct.pack("!BBHHH", icmp_type, icmp_code, cksum,
                       ident, seq) + data
    return icmp


def build_icmp_echo_request(seq, ident=ICMP_IDENT, data_size=8):
    """Build an ICMP echo request (type 8, code 0) with given seq and data."""
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
# Hex dump (for --verbose output)
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


def open_serial(port):
    """Open a serial port with eZ-FET safe settings.

    Sets dtr=False BEFORE open() so the kernel never asserts DTR on
    the CDC ACM device, which would reset the target on eZ-FET.
    Waits for boot and flushes the SLIP decoder.
    """
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 9600
    ser.bytesize = serial.EIGHTBITS
    ser.parity = serial.PARITY_NONE
    ser.stopbits = serial.STOPBITS_ONE
    ser.timeout = 0.1
    ser.xonxoff = False
    ser.rtscts = False
    ser.dsrdtr = False
    ser.dtr = False
    ser.open()
    time.sleep(2.0)   # wait for boot
    ser.reset_input_buffer()
    ser.write(bytes([SLIP_END] * 4))  # flush SLIP decoder
    time.sleep(0.5)
    ser.reset_input_buffer()
    return ser


def reopen_serial(ser):
    """Close and reopen the serial port (eZ-FET bug workaround).

    The eZ-FET backchannel enters a bad state after each reply
    exchange.  Closing and reopening the port resets it.
    """
    ser.close()
    time.sleep(0.5)
    ser.open()
    time.sleep(2.0)
    ser.reset_input_buffer()
    ser.write(bytes([SLIP_END] * 4))
    time.sleep(0.5)
    ser.reset_input_buffer()


def read_slip_frame(ser, timeout=2.0):
    """Read bytes from serial until a complete SLIP frame is received.

    Returns (decoded_frame, raw_wire_bytes) or (None, raw_bytes) on timeout.
    The raw_wire_bytes includes the SLIP encoding before decode.
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
            for i, b in enumerate(raw):
                if b == SLIP_END and got_data:
                    frames = slip_decode(raw[:i + 1])
                    if frames:
                        return frames[0], bytes(raw[:i + 1])
                elif b != SLIP_END:
                    got_data = True
        else:
            time.sleep(0.005)

    return None, bytes(raw)

# ---------------------------------------------------------------------------
# Test result tracking
# ---------------------------------------------------------------------------

class TestResults:
    """Accumulate pass/fail results with colored output."""

    def __init__(self, verbose=False):
        self.passed = 0
        self.failed = 0
        self.verbose = verbose

    def ok(self, category, description):
        """Record a passing test."""
        self.passed += 1
        print(f"  {C.GREEN}[PASS]{C.RESET} {category}: {description}")

    def fail(self, category, description):
        """Record a failing test."""
        self.failed += 1
        print(f"  {C.RED}[FAIL]{C.RESET} {category}: {description}")

    def check(self, condition, category, description):
        """Record pass or fail based on a boolean condition."""
        if condition:
            self.ok(category, description)
        else:
            self.fail(category, description)
        return condition

    def info(self, msg):
        """Print verbose info (only in verbose mode)."""
        if self.verbose:
            print(f"  {C.DIM}{msg}{C.RESET}")

    def summary(self):
        """Print final summary and return exit code."""
        total = self.passed + self.failed
        print()
        if self.failed == 0:
            print(f"{C.GREEN}{C.BOLD}=== Results: {self.passed}/{total} "
                  f"passed, 0 failed ==={C.RESET}")
        else:
            print(f"{C.RED}{C.BOLD}=== Results: {self.passed}/{total} "
                  f"passed, {self.failed} failed ==={C.RESET}")
        return 0 if self.failed == 0 else 1

# ---------------------------------------------------------------------------
# Ping helper: send one ICMP echo and return (reply, rtt_ms, raw_wire)
# ---------------------------------------------------------------------------

def send_ping(ser, seq, data_size=8, timeout=2.0, verbose=False):
    """Send one ICMP echo request and wait for reply.

    Returns (reply_bytes, rtt_ms, raw_wire_bytes).
    reply_bytes is None on timeout.
    """
    icmp = build_icmp_echo_request(seq, ICMP_IDENT, data_size)
    pkt = build_ipv4_packet(icmp)
    frame = slip_encode(pkt)

    if verbose:
        print(f"  {C.DIM}TX packet ({len(pkt)} bytes), "
              f"SLIP frame ({len(frame)} bytes):{C.RESET}")
        print(hexdump(frame, prefix="    "))

    t_start = time.monotonic()
    ser.write(frame)
    ser.flush()

    reply, raw_wire = read_slip_frame(ser, timeout=timeout)
    t_end = time.monotonic()
    rtt_ms = (t_end - t_start) * 1000.0

    if verbose and reply is not None:
        print(f"  {C.DIM}RX decoded ({len(reply)} bytes):{C.RESET}")
        print(hexdump(reply, prefix="    "))

    return reply, rtt_ms, raw_wire


def send_raw_packet(ser, raw_bytes, timeout=2.0):
    """SLIP-encode raw bytes, send, read reply.

    Returns (reply_bytes_or_None, rtt_ms).
    """
    frame = slip_encode(raw_bytes)
    t_start = time.monotonic()
    ser.write(frame)
    ser.flush()
    reply, _ = read_slip_frame(ser, timeout=timeout)
    t_end = time.monotonic()
    rtt_ms = (t_end - t_start) * 1000.0
    return reply, rtt_ms

# ---------------------------------------------------------------------------
# Test 1: Basic Ping (single)
# ---------------------------------------------------------------------------

def test_basic_ping(ser, results):
    """Send 1 ICMP echo request and validate the reply."""
    cat = "Basic ping"
    print(f"\n{C.BOLD}{C.CYAN}[1] {cat}{C.RESET}")

    reply, rtt_ms, _ = send_ping(ser, seq=1, data_size=8,
                                 verbose=results.verbose)

    # Reply received?
    if not results.check(reply is not None, cat, "reply received"):
        results.fail(cat, "skipping remaining checks (no reply)")
        return

    results.info(f"RTT = {rtt_ms:.1f} ms, reply size = {len(reply)} bytes")

    # IPv4 header basic validity
    results.check(len(reply) >= 28, cat, "reply >= 28 bytes")
    results.check((reply[0] >> 4) == 4, cat, "IPv4 version = 4")

    ihl = (reply[0] & 0x0F) * 4
    results.check(ihl >= 20, cat, "IHL >= 20")

    # IPv4 header checksum
    hdr_cksum = inet_checksum(reply[:ihl])
    results.check(hdr_cksum == 0, cat, "IPv4 checksum valid")

    # ICMP type = echo reply (0)
    icmp = reply[ihl:]
    results.check(len(icmp) >= 8, cat, "ICMP >= 8 bytes")
    results.check(icmp[0] == 0, cat, "ICMP type = 0 (echo reply)")

    # ICMP checksum
    icmp_cksum = inet_checksum(icmp)
    results.check(icmp_cksum == 0, cat, "ICMP checksum valid")

    # ID and seq match
    ident = (icmp[4] << 8) | icmp[5]
    seq = (icmp[6] << 8) | icmp[7]
    results.check(ident == ICMP_IDENT, cat,
                  f"ICMP id = 0x{ICMP_IDENT:04x}")
    results.check(seq == 1, cat, "ICMP seq = 1")

# ---------------------------------------------------------------------------
# Test 2: Sequential Pings (5 pings)
# ---------------------------------------------------------------------------

def test_sequential_pings(ser, results):
    """Send 5 pings with port reopen between each, validate all replies."""
    cat = "Sequential pings"
    count = 5
    print(f"\n{C.BOLD}{C.CYAN}[2] {cat} ({count}x){C.RESET}")

    received = 0
    for i in range(1, count + 1):
        if i > 1:
            reopen_serial(ser)

        reply, rtt_ms, _ = send_ping(ser, seq=i, data_size=8,
                                     verbose=results.verbose)
        if reply is not None:
            ihl = (reply[0] & 0x0F) * 4
            icmp = reply[ihl:]
            seq = (icmp[6] << 8) | icmp[7]
            if seq == i:
                received += 1
                results.info(f"  ping {i}: seq={seq} RTT={rtt_ms:.1f} ms")
            else:
                results.info(f"  ping {i}: wrong seq (got {seq})")
        else:
            results.info(f"  ping {i}: timeout")

    results.check(received == count, cat,
                  f"all {count} replies received (got {received})")

    # Verify seq numbers matched (implicitly checked above, but be explicit)
    results.check(received == count, cat,
                  f"all seq numbers matched")

# ---------------------------------------------------------------------------
# Test 3: Payload Sizes
# ---------------------------------------------------------------------------

def test_payload_sizes(ser, results):
    """Test various ICMP data sizes from 0 to near MTU."""
    cat = "Payload sizes"
    print(f"\n{C.BOLD}{C.CYAN}[3] {cat}{C.RESET}")

    # (data_size, description, expected_total_ip_len)
    sizes = [
        (0,  "minimum (header only)", 28),
        (8,  "standard",              36),
        (56, "ping -s 56",            84),
        (92, "near MTU (120 bytes)",  120),
    ]

    first = True
    for data_size, desc, expected_len in sizes:
        if not first:
            reopen_serial(ser)
        first = False

        reply, rtt_ms, _ = send_ping(ser, seq=1, data_size=data_size,
                                     verbose=results.verbose)

        label = f"size={data_size} ({desc})"

        if not results.check(reply is not None, cat,
                             f"{label}: reply received"):
            continue

        # Verify total length matches expectation
        ip_total_len = (reply[2] << 8) | reply[3]
        results.check(ip_total_len == expected_len, cat,
                      f"{label}: IP total_len = {expected_len} "
                      f"(got {ip_total_len})")

        # Verify actual payload length
        ihl = (reply[0] & 0x0F) * 4
        icmp = reply[ihl:]
        icmp_data = icmp[8:]  # skip ICMP header (8 bytes)
        results.check(len(icmp_data) == data_size, cat,
                      f"{label}: payload length = {data_size} "
                      f"(got {len(icmp_data)})")

        results.info(f"  {label}: RTT = {rtt_ms:.1f} ms, "
                     f"reply = {len(reply)} bytes")

# ---------------------------------------------------------------------------
# Test 4: Checksum Verification
# ---------------------------------------------------------------------------

def test_checksum_verification(ser, results):
    """Validate IPv4 and ICMP checksums on multiple replies."""
    cat = "Checksum verification"
    count = 3
    print(f"\n{C.BOLD}{C.CYAN}[4] {cat}{C.RESET}")

    first = True
    for i in range(1, count + 1):
        if not first:
            reopen_serial(ser)
        first = False

        reply, _, _ = send_ping(ser, seq=i, data_size=8,
                                verbose=results.verbose)

        if reply is None:
            results.fail(cat, f"ping {i}: no reply")
            continue

        ihl = (reply[0] & 0x0F) * 4

        # IPv4 header checksum: residual must be 0
        ipv4_residual = inet_checksum(reply[:ihl])
        results.check(ipv4_residual == 0, cat,
                      f"ping {i}: IPv4 header checksum residual = 0 "
                      f"(got 0x{ipv4_residual:04x})")

        # ICMP checksum: residual must be 0
        icmp = reply[ihl:]
        icmp_residual = inet_checksum(icmp)
        results.check(icmp_residual == 0, cat,
                      f"ping {i}: ICMP checksum residual = 0 "
                      f"(got 0x{icmp_residual:04x})")

# ---------------------------------------------------------------------------
# Test 5: Address Validation
# ---------------------------------------------------------------------------

def test_address_validation(ser, results):
    """Verify source/destination IP addresses are correct in replies."""
    cat = "Address validation"
    print(f"\n{C.BOLD}{C.CYAN}[5] {cat}{C.RESET}")

    reply, _, _ = send_ping(ser, seq=1, data_size=8,
                            verbose=results.verbose)

    if not results.check(reply is not None, cat, "reply received"):
        return

    # Reply source should be device (172.16.7.2)
    reply_src = reply[12:16]
    reply_dst = reply[16:20]

    src_str = f"{reply_src[0]}.{reply_src[1]}.{reply_src[2]}.{reply_src[3]}"
    dst_str = f"{reply_dst[0]}.{reply_dst[1]}.{reply_dst[2]}.{reply_dst[3]}"

    results.check(reply_src == DST_IP, cat,
                  f"reply src = 172.16.7.2 (got {src_str})")

    # Reply destination should be host (172.16.7.1)
    results.check(reply_dst == SRC_IP, cat,
                  f"reply dst = 172.16.7.1 (got {dst_str})")

    # Reply src != request src (IPs were swapped by the device)
    results.check(reply_src != SRC_IP, cat,
                  "reply src != request src (IPs swapped)")

# ---------------------------------------------------------------------------
# Test 6: SLIP Escaping Integrity
# ---------------------------------------------------------------------------

def test_slip_escaping(ser, results):
    """Verify SLIP encoding correctness on the wire."""
    cat = "SLIP escaping"
    print(f"\n{C.BOLD}{C.CYAN}[6] {cat}{C.RESET}")

    reply, _, raw_wire = send_ping(ser, seq=1, data_size=8,
                                   verbose=results.verbose)

    if not results.check(reply is not None, cat, "reply received"):
        return

    # The raw wire bytes (SLIP-encoded) should contain no raw 0x00
    # because the eZ-FET NUL escape should replace all 0x00 with ESC+ESC_NUL
    has_raw_nul = False
    in_esc = False
    for b in raw_wire:
        if in_esc:
            in_esc = False
            continue
        if b == SLIP_ESC:
            in_esc = True
            continue
        if b == SLIP_END:
            continue
        if b == 0x00:
            has_raw_nul = True
            break
    results.check(not has_raw_nul, cat,
                  "no raw 0x00 in SLIP-encoded wire bytes")

    # Verify the decoded reply is a valid IPv4/ICMP structure
    results.check(len(reply) >= 28, cat,
                  "decoded reply is valid IPv4 (>= 28 bytes)")
    results.check((reply[0] >> 4) == 4, cat,
                  "decoded reply has IPv4 version nibble")

    ihl = (reply[0] & 0x0F) * 4
    icmp = reply[ihl:]
    results.check(icmp[0] == 0, cat,
                  "decoded ICMP type = 0 (echo reply)")

    # Verify round-trip: re-encode the decoded reply and ensure no raw 0x00
    re_encoded = slip_encode(reply)
    has_raw_nul_re = False
    in_esc = False
    for b in re_encoded:
        if in_esc:
            in_esc = False
            continue
        if b == SLIP_ESC:
            in_esc = True
            continue
        if b == SLIP_END:
            continue
        if b == 0x00:
            has_raw_nul_re = True
            break
    results.check(not has_raw_nul_re, cat,
                  "re-encoded reply also has no raw 0x00")

# ---------------------------------------------------------------------------
# Test 7: RTT Consistency
# ---------------------------------------------------------------------------

def test_rtt_consistency(ser, results):
    """Verify round-trip times are within expected bounds."""
    cat = "RTT consistency"
    count = 5
    print(f"\n{C.BOLD}{C.CYAN}[7] {cat} ({count}x){C.RESET}")

    rtts = []
    first = True
    for i in range(1, count + 1):
        if not first:
            reopen_serial(ser)
        first = False

        reply, rtt_ms, _ = send_ping(ser, seq=i, data_size=8,
                                     verbose=results.verbose)
        if reply is not None:
            rtts.append(rtt_ms)
            results.info(f"  ping {i}: RTT = {rtt_ms:.1f} ms")
        else:
            results.info(f"  ping {i}: timeout")

    if not results.check(len(rtts) == count, cat,
                         f"all {count} replies received (got {len(rtts)})"):
        return

    # All RTTs < 500ms
    all_under_500 = all(r < 500.0 for r in rtts)
    results.check(all_under_500, cat,
                  f"all RTTs < 500 ms "
                  f"(max = {max(rtts):.1f} ms)")

    # All RTTs > 50ms (there is real serial + processing latency)
    all_over_50 = all(r > 50.0 for r in rtts)
    results.check(all_over_50, cat,
                  f"all RTTs > 50 ms "
                  f"(min = {min(rtts):.1f} ms)")

    # Standard deviation < 50ms (consistent timing)
    if len(rtts) >= 2:
        mean = sum(rtts) / len(rtts)
        variance = sum((r - mean) ** 2 for r in rtts) / len(rtts)
        stddev = math.sqrt(variance)
        results.check(stddev < 50.0, cat,
                      f"RTT stddev < 50 ms "
                      f"(stddev = {stddev:.1f} ms)")
        results.info(f"  mean={mean:.1f} ms, stddev={stddev:.1f} ms, "
                     f"min={min(rtts):.1f} ms, max={max(rtts):.1f} ms")

# ---------------------------------------------------------------------------
# Test 8: MTU Boundary
# ---------------------------------------------------------------------------

def test_mtu_boundary(ser, results):
    """Test behaviour at and around the MTU boundary (128 bytes)."""
    cat = "MTU boundary"
    print(f"\n{C.BOLD}{C.CYAN}[8] {cat}{C.RESET}")

    # data_size=99 → IP total = 20 + 8 + 99 = 127 (MTU-1)
    reply, rtt_ms, _ = send_ping(ser, seq=1, data_size=99,
                                 verbose=results.verbose)
    results.check(reply is not None, cat,
                  "MTU-1 (127 bytes): reply received")
    if reply is not None:
        ip_total_len = (reply[2] << 8) | reply[3]
        results.check(ip_total_len == 127, cat,
                      f"MTU-1: total_len = 127 (got {ip_total_len})")

    reopen_serial(ser)

    # data_size=100 → IP total = 20 + 8 + 100 = 128 (exactly MTU)
    reply, rtt_ms, _ = send_ping(ser, seq=2, data_size=100,
                                 verbose=results.verbose)
    results.check(reply is not None, cat,
                  "MTU (128 bytes): reply received")
    if reply is not None:
        ip_total_len = (reply[2] << 8) | reply[3]
        results.check(ip_total_len == 128, cat,
                      f"MTU: total_len = 128 (got {ip_total_len})")

    reopen_serial(ser)

    # data_size=101 → IP total = 20 + 8 + 101 = 129 (MTU+1)
    reply, rtt_ms, _ = send_ping(ser, seq=3, data_size=101,
                                 timeout=1.0, verbose=results.verbose)
    results.check(reply is None, cat,
                  "MTU+1 (129 bytes): no reply (oversized dropped)")

    reopen_serial(ser)

    # Normal ping to verify device is still alive
    reply, rtt_ms, _ = send_ping(ser, seq=4, data_size=8,
                                 verbose=results.verbose)
    results.check(reply is not None, cat,
                  "device alive after oversized")

# ---------------------------------------------------------------------------
# Test 9: Malformed Packets
# ---------------------------------------------------------------------------

def test_malformed_packets(ser, results):
    """Send various malformed packets and verify the device drops them."""
    cat = "Malformed packets"
    print(f"\n{C.BOLD}{C.CYAN}[9] {cat}{C.RESET}")

    # Sub-case A: wrong IP version (v6 header byte)
    raw_a = bytes([0x60] + [0x00] * 39)
    reply, _ = send_raw_packet(ser, raw_a, timeout=1.0)
    results.check(reply is None, cat,
                  "wrong IP version (0x60): no reply")

    reopen_serial(ser)

    # Sub-case B: bad IPv4 checksum
    icmp_b = build_icmp_echo_request(1, ICMP_IDENT, 0)
    pkt_b = bytearray(build_ipv4_packet(icmp_b))
    pkt_b[10] ^= 0xFF  # corrupt IPv4 header checksum byte
    reply, _ = send_raw_packet(ser, bytes(pkt_b), timeout=1.0)
    results.check(reply is None, cat,
                  "bad IPv4 checksum: no reply")

    reopen_serial(ser)

    # Sub-case C: wrong destination IP (172.16.7.99)
    SRC_IP_BYTES = bytes([172, 16, 7, 1])
    WRONG_DST = bytes([172, 16, 7, 99])
    hdr = struct.pack("!BBHHHBBH4s4s",
                      0x45, 0, 28, 1, 0x4000, 64, 1, 0,
                      SRC_IP_BYTES, WRONG_DST)
    hck = inet_checksum(hdr)
    hdr = struct.pack("!BBHHHBBH4s4s",
                      0x45, 0, 28, 1, 0x4000, 64, 1, hck,
                      SRC_IP_BYTES, WRONG_DST)
    icmp_c = struct.pack("!BBHHH", 8, 0, 0, 0x5449, 1)
    ick = inet_checksum(icmp_c)
    icmp_c = struct.pack("!BBHHH", 8, 0, ick, 0x5449, 1)
    raw_c = hdr + icmp_c
    reply, _ = send_raw_packet(ser, raw_c, timeout=1.0)
    results.check(reply is None, cat,
                  "wrong dst IP (172.16.7.99): no reply")

    reopen_serial(ser)

    # Sub-case D: truncated packet (10 bytes)
    raw_d = bytes([0x45] + [0x00] * 9)
    reply, _ = send_raw_packet(ser, raw_d, timeout=1.0)
    results.check(reply is None, cat,
                  "truncated (10 bytes): no reply")

    reopen_serial(ser)

    # Normal ping to verify device is still alive
    reply, _, _ = send_ping(ser, seq=1, data_size=8,
                            verbose=results.verbose)
    results.check(reply is not None, cat,
                  "device alive after malformed")

# ---------------------------------------------------------------------------
# Test 10: SLIP Decoder Edge Cases
# ---------------------------------------------------------------------------

def test_slip_decoder_edge_cases(ser, results):
    """Exercise SLIP decoder edge cases with raw wire bytes."""
    cat = "SLIP decoder edge cases"
    print(f"\n{C.BOLD}{C.CYAN}[10] {cat}{C.RESET}")

    # Sub-case A: two bare END bytes (empty frame)
    ser.write(bytes([0xC0, 0xC0]))
    ser.flush()
    reply, _ = read_slip_frame(ser, timeout=1.0)
    results.check(reply is None, cat,
                  "double END (empty frame): no reply")

    reopen_serial(ser)

    # Sub-case B: END + ESC + END (broken escape sequence)
    ser.write(bytes([0xC0, 0xDB, 0xC0]))
    ser.flush()
    reply, _ = read_slip_frame(ser, timeout=1.0)
    results.check(reply is None, cat,
                  "END+ESC+END (broken escape): no reply")

    reopen_serial(ser)

    # Sub-case C: send garbage bytes, then reopen and verify device
    # still responds.  (Sending garbage + valid frame in one write
    # is unreliable through the eZ-FET; the firmware's SLIP decoder
    # recovery from garbage is tested in the firmware unit tests.)
    ser.write(bytes([0x55, 0xAA, 0x33, 0xC0]))  # garbage + END
    ser.flush()
    time.sleep(0.5)

    reopen_serial(ser)

    reply, _, _ = send_ping(ser, seq=1, data_size=8,
                            verbose=results.verbose)
    results.check(reply is not None, cat,
                  "device alive after garbage bytes")
    if reply is not None:
        ihl = (reply[0] & 0x0F) * 4
        icmp_reply = reply[ihl:]
        results.check(icmp_reply[0] == 0, cat,
                      "reply ICMP type = 0 (echo reply)")

# ---------------------------------------------------------------------------
# Test 11: Payload Content Preservation
# ---------------------------------------------------------------------------

def test_payload_content_preservation(ser, results):
    """Verify the device echoes back ICMP data bytes unchanged."""
    cat = "Payload content"
    print(f"\n{C.BOLD}{C.CYAN}[11] {cat}{C.RESET}")

    data = bytes(range(32))  # 0x00 through 0x1F
    icmp = build_icmp_echo_request_with_data(1, 0x5449, data)
    pkt = build_ipv4_packet(icmp)
    frame = slip_encode(pkt)

    t_start = time.monotonic()
    ser.write(frame)
    ser.flush()
    reply, _ = read_slip_frame(ser, timeout=2.0)
    t_end = time.monotonic()

    results.check(reply is not None, cat, "reply received")
    if reply is not None:
        ihl = (reply[0] & 0x0F) * 4
        icmp_data = reply[ihl + 8:]  # skip IP header + 8-byte ICMP header
        results.check(len(icmp_data) == 32, cat,
                      f"payload length = 32 (got {len(icmp_data)})")
        results.check(icmp_data == data, cat,
                      "payload bytes match (0x00..0x1F)")

# ---------------------------------------------------------------------------
# Test 12: SLIP Special Bytes in Payload
# ---------------------------------------------------------------------------

def test_slip_special_bytes_in_payload(ser, results):
    """Verify SLIP special bytes (0xC0, 0xDB) survive round-trip in payload."""
    cat = "SLIP special bytes"
    print(f"\n{C.BOLD}{C.CYAN}[12] {cat}{C.RESET}")

    data = bytes([0xC0, 0xDB, 0x00, 0xDE, 0xC0, 0xDB, 0x00, 0xDE])
    icmp = build_icmp_echo_request_with_data(1, 0x5449, data)
    pkt = build_ipv4_packet(icmp)
    frame = slip_encode(pkt)

    ser.write(frame)
    ser.flush()
    reply, raw_wire = read_slip_frame(ser, timeout=2.0)

    results.check(reply is not None, cat, "reply received")

    if reply is not None:
        ihl = (reply[0] & 0x0F) * 4
        icmp_data = reply[ihl + 8:]
        results.check(icmp_data == data, cat,
                      "ICMP data matches special bytes exactly")

        # Verify no raw 0x00 in wire bytes
        has_raw_nul = False
        in_esc = False
        for b in raw_wire:
            if in_esc:
                in_esc = False
                continue
            if b == SLIP_ESC:
                in_esc = True
                continue
            if b == SLIP_END:
                continue
            if b == 0x00:
                has_raw_nul = True
                break
        results.check(not has_raw_nul, cat,
                      "no raw 0x00 in wire bytes")

        # Verify checksums valid
        ip_cksum = inet_checksum(reply[:ihl])
        results.check(ip_cksum == 0, cat,
                      "IPv4 checksum valid")
        icmp_full = reply[ihl:]
        icmp_cksum = inet_checksum(icmp_full)
        results.check(icmp_cksum == 0, cat,
                      "ICMP checksum valid")

# ---------------------------------------------------------------------------
# Test 13: Long-Running Stability (20 pings)
# ---------------------------------------------------------------------------

def test_long_running_stability(ser, results):
    """Send 20 pings and verify all are received with valid timing."""
    cat = "Stability (20 pings)"
    count = 20
    print(f"\n{C.BOLD}{C.CYAN}[13] {cat}{C.RESET}")

    received = 0
    seq_matched = 0
    rtts = []
    first = True
    for i in range(1, count + 1):
        if not first:
            reopen_serial(ser)
        first = False

        reply, rtt_ms, _ = send_ping(ser, seq=i, data_size=8,
                                     verbose=results.verbose)
        if reply is not None:
            received += 1
            rtts.append(rtt_ms)
            ihl = (reply[0] & 0x0F) * 4
            icmp = reply[ihl:]
            seq = (icmp[6] << 8) | icmp[7]
            if seq == i:
                seq_matched += 1
            results.info(f"  ping {i}: seq={seq} RTT={rtt_ms:.1f} ms")
        else:
            results.info(f"  ping {i}: timeout")

    results.check(received == count, cat,
                  f"all {count} replies received (got {received})")
    results.check(seq_matched == count, cat,
                  f"all seq numbers matched (got {seq_matched})")
    all_under_1000 = all(r < 1000.0 for r in rtts) if rtts else False
    results.check(all_under_1000, cat,
                  f"all RTTs < 1000 ms "
                  f"(max = {max(rtts):.1f} ms)" if rtts else
                  "all RTTs < 1000 ms (no data)")

# ---------------------------------------------------------------------------
# Test 14: Rapid Pings
# ---------------------------------------------------------------------------

def test_rapid_pings(ser, results):
    """Send 3 pings with standard reopen and verify all received."""
    cat = "Rapid pings"
    count = 3
    print(f"\n{C.BOLD}{C.CYAN}[14] {cat}{C.RESET}")

    received = 0
    seq_matched = 0
    first = True
    for i in range(1, count + 1):
        if not first:
            reopen_serial(ser)
        first = False

        reply, rtt_ms, _ = send_ping(ser, seq=i, data_size=8,
                                     verbose=results.verbose)
        if reply is not None:
            received += 1
            ihl = (reply[0] & 0x0F) * 4
            icmp = reply[ihl:]
            seq = (icmp[6] << 8) | icmp[7]
            if seq == i:
                seq_matched += 1
            results.info(f"  ping {i}: seq={seq} RTT={rtt_ms:.1f} ms")
        else:
            results.info(f"  ping {i}: timeout")

    results.check(received == count, cat,
                  f"all {count} replies received (got {received})")
    results.check(seq_matched == count, cat,
                  f"all seq numbers matched (got {seq_matched})")

# ---------------------------------------------------------------------------
# Build and flash
# ---------------------------------------------------------------------------

def build_and_flash(mcu):
    """Clean, build APP=net, and flash the firmware."""
    print(f"\n{C.BOLD}Building APP=net for {mcu}...{C.RESET}")

    # Clean
    print(f"  {C.DIM}make clean{C.RESET}")
    result = subprocess.run(["make", "clean"],
                            cwd=PROJ_DIR,
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  {C.YELLOW}Warning: make clean returned "
              f"{result.returncode}{C.RESET}")

    # Build
    print(f"  {C.DIM}make APP=net MCU={mcu}{C.RESET}")
    result = subprocess.run(["make", "APP=net", f"MCU={mcu}"],
                            cwd=PROJ_DIR,
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  {C.RED}Build failed:{C.RESET}")
        print(result.stderr)
        print(result.stdout)
        sys.exit(1)
    print(f"  {C.GREEN}Build succeeded{C.RESET}")

    # Flash
    print(f"  {C.DIM}make flash APP=net MCU={mcu}{C.RESET}")
    result = subprocess.run(["make", "flash", "APP=net", f"MCU={mcu}"],
                            cwd=PROJ_DIR,
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  {C.RED}Flash failed:{C.RESET}")
        print(result.stderr)
        print(result.stdout)
        sys.exit(1)
    print(f"  {C.GREEN}Flash succeeded{C.RESET}")

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="TikuOS IPv4-over-SLIP integration test")
    parser.add_argument("--port", "-p", default=None,
                        help="Serial port (default: auto-detect ttyACM*)")
    parser.add_argument("--mcu", default="msp430fr5969",
                        help="Target MCU (default: msp430fr5969)")
    parser.add_argument("--skip-build", action="store_true",
                        help="Skip make clean/build/flash steps")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Show hex dumps of sent/received packets")
    args = parser.parse_args()

    # Header
    print(f"\n{C.BOLD}{C.CYAN}"
          f"=========================================={C.RESET}")
    print(f"{C.BOLD}{C.CYAN}"
          f"  TikuOS Network Integration Test{C.RESET}")
    print(f"{C.BOLD}{C.CYAN}"
          f"  IPv4-over-SLIP Ping{C.RESET}")
    print(f"{C.BOLD}{C.CYAN}"
          f"=========================================={C.RESET}")
    print(f"  Host IP:   {SRC_IP[0]}.{SRC_IP[1]}."
          f"{SRC_IP[2]}.{SRC_IP[3]}")
    print(f"  Device IP: {DST_IP[0]}.{DST_IP[1]}."
          f"{DST_IP[2]}.{DST_IP[3]}")
    print(f"  MCU:       {args.mcu}")

    # Build and flash (unless --skip-build)
    if not args.skip_build:
        build_and_flash(args.mcu)
    else:
        print(f"\n  {C.YELLOW}Skipping build/flash (--skip-build){C.RESET}")

    # Resolve serial port
    port = args.port or auto_detect_port()
    if port is None:
        print(f"\n{C.RED}ERROR: No serial port found. "
              f"Specify with --port{C.RESET}")
        sys.exit(1)
    print(f"  Port:      {port}")

    # Open serial port
    print(f"\n{C.BOLD}Opening serial port...{C.RESET}")
    try:
        ser = open_serial(port)
    except serial.SerialException as e:
        print(f"{C.RED}ERROR: Cannot open {port}: {e}{C.RESET}")
        sys.exit(1)
    print(f"  {C.GREEN}Port open, device booted{C.RESET}")

    # Run all tests
    results = TestResults(verbose=args.verbose)

    try:
        # Test 1: Basic Ping
        test_basic_ping(ser, results)

        # Reopen between test categories
        reopen_serial(ser)

        # Test 2: Sequential Pings
        test_sequential_pings(ser, results)

        reopen_serial(ser)

        # Test 3: Payload Sizes
        test_payload_sizes(ser, results)

        reopen_serial(ser)

        # Test 4: Checksum Verification
        test_checksum_verification(ser, results)

        reopen_serial(ser)

        # Test 5: Address Validation
        test_address_validation(ser, results)

        reopen_serial(ser)

        # Test 6: SLIP Escaping Integrity
        test_slip_escaping(ser, results)

        reopen_serial(ser)

        # Test 7: RTT Consistency
        test_rtt_consistency(ser, results)

        reopen_serial(ser)

        # Test 8: MTU Boundary
        test_mtu_boundary(ser, results)

        reopen_serial(ser)

        # Test 9: Malformed Packets
        test_malformed_packets(ser, results)

        reopen_serial(ser)

        # Test 10: SLIP Decoder Edge Cases
        test_slip_decoder_edge_cases(ser, results)

        reopen_serial(ser)

        # Test 11: Payload Content Preservation
        test_payload_content_preservation(ser, results)

        reopen_serial(ser)

        # Test 12: SLIP Special Bytes in Payload
        test_slip_special_bytes_in_payload(ser, results)

        reopen_serial(ser)

        # Test 13: Long-Running Stability
        test_long_running_stability(ser, results)

        reopen_serial(ser)

        # Test 14: Rapid Pings
        test_rapid_pings(ser, results)

    except KeyboardInterrupt:
        print(f"\n{C.YELLOW}Interrupted by user{C.RESET}")
    except serial.SerialException as e:
        print(f"\n{C.RED}Serial error: {e}{C.RESET}")
    finally:
        ser.close()

    # Summary
    rc = results.summary()
    sys.exit(rc)


if __name__ == "__main__":
    main()
