#!/usr/bin/env python3
"""
TikuOS HTTP Fetch Gateway

Bridges the MSP430's TCP-over-SLIP connection to a real HTTP
request on the internet.  Companion to Example 17 (http_fetch).

Architecture:
  MSP430  <--SLIP/UART-->  This script  <--urllib-->  Internet

The script handles SLIP framing, IPv4, and a minimal TCP
responder (enough for one HTTP exchange).  When the MSP430
sends an HTTP GET, the script extracts the Host header, fetches
the real URL with Python urllib, and returns the response.

Usage:
    python3 tools/http_fetch_gateway.py
    python3 tools/http_fetch_gateway.py --port /dev/ttyUSB0
    python3 tools/http_fetch_gateway.py --url http://example.com/

Authors: Ambuj Varshney <ambuj@tiku-os.org>
SPDX-License-Identifier: Apache-2.0
"""

import argparse
import glob
import os
import struct
import sys
import time
import urllib.request
import urllib.error

import serial

# ---------------------------------------------------------------------------
# Network constants (must match TikuOS configuration)
# ---------------------------------------------------------------------------

HOST_IP = bytes([172, 16, 7, 1])     # Us (the gateway)
DEVICE_IP = bytes([172, 16, 7, 2])   # MSP430
LISTEN_PORT = 80                      # Port we listen on
DEVICE_MSS = 88                       # MSP430 TCP MSS
GATEWAY_MSS = 512                     # Our advertised MSS
INITIAL_WINDOW = 4096                 # Our advertised window

# TCP flags
TCP_FIN = 0x01
TCP_SYN = 0x02
TCP_RST = 0x04
TCP_PSH = 0x08
TCP_ACK = 0x10

# ---------------------------------------------------------------------------
# SLIP codec
# ---------------------------------------------------------------------------

SLIP_END = 0xC0
SLIP_ESC = 0xDB
SLIP_ESC_END = 0xDC
SLIP_ESC_ESC = 0xDD
SLIP_ESC_NUL = 0xDE   # MSP430 escapes 0x00 (eZ-FET workaround)


def slip_encode(pkt):
    """SLIP-encode an IP packet (with NUL escaping for MSP430)."""
    out = bytearray([SLIP_END])
    for b in pkt:
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


def read_slip_frame(ser, timeout=30.0):
    """Read one complete SLIP frame from serial."""
    frame = bytearray()
    escape = False
    deadline = time.time() + timeout

    while time.time() < deadline:
        if ser.in_waiting == 0:
            time.sleep(0.001)
            continue
        raw = ser.read(ser.in_waiting)
        for b in raw:
            if escape:
                if b == SLIP_ESC_END:
                    frame.append(SLIP_END)
                elif b == SLIP_ESC_ESC:
                    frame.append(SLIP_ESC)
                elif b == SLIP_ESC_NUL:
                    frame.append(0x00)
                else:
                    frame.append(b)
                escape = False
            elif b == SLIP_ESC:
                escape = True
            elif b == SLIP_END:
                if len(frame) > 0:
                    return bytes(frame)
            else:
                frame.append(b)
    return None

# ---------------------------------------------------------------------------
# Checksum
# ---------------------------------------------------------------------------


def inet_checksum(data):
    """RFC 1071 internet checksum."""
    if len(data) % 2:
        data = data + b'\x00'
    s = 0
    for i in range(0, len(data), 2):
        s += (data[i] << 8) | data[i + 1]
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF

# ---------------------------------------------------------------------------
# IPv4 + TCP packet builder
# ---------------------------------------------------------------------------


def build_tcp_packet(src_ip, dst_ip, sport, dport,
                     seq, ack, flags, window,
                     payload=b"", mss=0):
    """Build a complete IPv4+TCP packet."""
    # TCP header
    tcp_hdr_len = 24 if mss > 0 else 20
    data_offset = (tcp_hdr_len // 4) << 4

    tcp = struct.pack("!HHIIBBHHH",
                      sport, dport,
                      seq, ack,
                      data_offset, flags,
                      window, 0, 0)

    if mss > 0:
        tcp += struct.pack("!BBH", 2, 4, mss)

    tcp = bytearray(tcp) + payload

    # TCP checksum (pseudo-header)
    tcp_len = len(tcp)
    pseudo = src_ip + dst_ip + struct.pack("!BBH", 0, 6, tcp_len)
    chk = inet_checksum(pseudo + bytes(tcp))
    tcp[16] = (chk >> 8) & 0xFF
    tcp[17] = chk & 0xFF

    # IPv4 header
    total_len = 20 + tcp_len
    ip = bytearray(struct.pack("!BBHHHBBH4s4s",
                                0x45, 0x00, total_len,
                                0x0001, 0x0000,
                                64, 6, 0,
                                src_ip, dst_ip))
    ip_chk = inet_checksum(bytes(ip))
    ip[10] = (ip_chk >> 8) & 0xFF
    ip[11] = ip_chk & 0xFF

    return bytes(ip) + bytes(tcp)


def parse_ip_tcp(pkt):
    """Parse IPv4 + TCP headers from a raw packet."""
    if len(pkt) < 40:
        return None

    ihl = (pkt[0] & 0x0F) * 4
    total_len = (pkt[2] << 8) | pkt[3]
    proto = pkt[9]
    src_ip = pkt[12:16]
    dst_ip = pkt[16:20]

    if proto != 6:
        return None  # Not TCP

    tcp = pkt[ihl:]
    if len(tcp) < 20:
        return None

    sport = (tcp[0] << 8) | tcp[1]
    dport = (tcp[2] << 8) | tcp[3]
    seq = struct.unpack("!I", tcp[4:8])[0]
    ack = struct.unpack("!I", tcp[8:12])[0]
    data_off = ((tcp[12] >> 4) & 0x0F) * 4
    flags = tcp[13]
    window = (tcp[14] << 8) | tcp[15]

    # Parse MSS option from SYN
    peer_mss = 0
    if (flags & TCP_SYN) and data_off > 20:
        opt_offset = 20
        while opt_offset < data_off and opt_offset < len(tcp):
            kind = tcp[opt_offset]
            if kind == 0:
                break
            if kind == 1:
                opt_offset += 1
                continue
            if opt_offset + 1 >= len(tcp):
                break
            opt_len = tcp[opt_offset + 1]
            if kind == 2 and opt_len == 4:
                peer_mss = (tcp[opt_offset + 2] << 8) | \
                           tcp[opt_offset + 3]
            opt_offset += opt_len

    payload = tcp[data_off:]

    return {
        "src_ip": src_ip, "dst_ip": dst_ip,
        "sport": sport, "dport": dport,
        "seq": seq, "ack": ack,
        "flags": flags, "window": window,
        "payload": payload, "peer_mss": peer_mss,
    }

# ---------------------------------------------------------------------------
# Serial helpers
# ---------------------------------------------------------------------------


def send_packet(ser, pkt):
    """SLIP-encode and send a packet, chunked for eZ-FET."""
    encoded = slip_encode(pkt)
    CHUNK = 47
    for i in range(0, len(encoded), CHUNK):
        ser.write(encoded[i:i + CHUNK])
        if i + CHUNK < len(encoded):
            time.sleep(0.005)
    ser.flush()


def find_port():
    """Auto-detect serial port."""
    for pattern in ["/dev/ttyUSB*", "/dev/ttyACM*"]:
        ports = sorted(glob.glob(pattern))
        if ports:
            return ports[0]
    return None

# ---------------------------------------------------------------------------
# HTTP fetch
# ---------------------------------------------------------------------------


def extract_http_request(data):
    """Extract method, host, path from an HTTP request."""
    text = data.decode("ascii", errors="replace")
    lines = text.split("\r\n")
    if not lines:
        return None, None, None

    parts = lines[0].split(" ")
    if len(parts) < 2:
        return None, None, None

    method = parts[0]
    path = parts[1]
    host = None

    for line in lines[1:]:
        if line.lower().startswith("host:"):
            host = line.split(":", 1)[1].strip()
            break

    return method, host, path


def fetch_url(url):
    """Fetch a URL and return (status, headers_str, body)."""
    try:
        req = urllib.request.Request(url)
        req.add_header("User-Agent", "TikuOS-Gateway/1.0")
        resp = urllib.request.urlopen(req, timeout=10)
        body = resp.read()
        status = resp.status
        reason = resp.reason
        # Build minimal HTTP response headers
        headers = f"HTTP/1.1 {status} {reason}\r\n"
        ct = resp.headers.get("Content-Type", "text/html")
        headers += f"Content-Type: {ct}\r\n"
        headers += f"Content-Length: {len(body)}\r\n"
        headers += "Connection: close\r\n"
        headers += "\r\n"
        return status, headers.encode("ascii"), body
    except urllib.error.HTTPError as e:
        body = e.read() if e.fp else b""
        reason = e.reason if hasattr(e, "reason") else "Error"
        headers = f"HTTP/1.1 {e.code} {reason}\r\n"
        headers += "Connection: close\r\n\r\n"
        return e.code, headers.encode("ascii"), body
    except Exception as e:
        body = str(e).encode("ascii")
        headers = b"HTTP/1.1 502 Bad Gateway\r\n"
        headers += b"Connection: close\r\n\r\n"
        return 502, headers, body

# ---------------------------------------------------------------------------
# TCP gateway state machine
# ---------------------------------------------------------------------------


class TcpGateway:
    """Minimal TCP responder for a single HTTP exchange."""

    def __init__(self, ser, override_url=None):
        self.ser = ser
        self.override_url = override_url
        self.state = "LISTEN"
        self.our_seq = 0x1FA000
        self.their_seq = 0
        self.their_sport = 0
        self.their_mss = DEVICE_MSS
        self.http_data = bytearray()
        self.response_sent = 0

    def send_tcp(self, flags, payload=b"", mss=0):
        """Build and send a TCP packet to the device."""
        pkt = build_tcp_packet(
            HOST_IP, DEVICE_IP,
            LISTEN_PORT, self.their_sport,
            self.our_seq, self.their_seq,
            flags, INITIAL_WINDOW,
            payload, mss)
        send_packet(self.ser, pkt)

    def handle(self, parsed):
        """Process one TCP packet from the device."""
        if parsed["dport"] != LISTEN_PORT:
            return
        if parsed["src_ip"] != DEVICE_IP:
            return

        flags = parsed["flags"]
        payload = parsed["payload"]

        if self.state == "LISTEN":
            if flags & TCP_SYN:
                self.their_sport = parsed["sport"]
                self.their_seq = parsed["seq"] + 1
                self.their_mss = parsed["peer_mss"] or DEVICE_MSS

                # SYN+ACK
                self.send_tcp(TCP_SYN | TCP_ACK, mss=GATEWAY_MSS)
                self.our_seq += 1
                self.state = "SYN_RCVD"
                print(f"  [TCP] SYN from :{parsed['sport']}"
                      f" -> SYN+ACK sent")

        elif self.state == "SYN_RCVD":
            if flags & TCP_ACK:
                self.state = "ESTABLISHED"
                print("  [TCP] 3-way handshake complete")
                # ACK may carry piggybacked data (GET request)
                if len(payload) > 0:
                    self.their_seq = (parsed["seq"]
                                      + len(payload))
                    self.send_tcp(TCP_ACK)
                    self.http_data.extend(payload)
                    print(f"  [TCP] {len(payload)} bytes "
                          f"piggybacked on ACK")
                    if b"\r\n\r\n" in self.http_data:
                        self._proxy_request()

        elif self.state == "ESTABLISHED":
            if len(payload) > 0:
                # ACK the data
                self.their_seq = parsed["seq"] + len(payload)
                self.send_tcp(TCP_ACK)
                self.http_data.extend(payload)
                print(f"  [TCP] Received {len(payload)} bytes"
                      f" (total: {len(self.http_data)})")

                # Check if we have the full HTTP request
                # (ends with \r\n\r\n)
                if b"\r\n\r\n" in self.http_data:
                    self._proxy_request()

            elif flags & TCP_FIN:
                self.their_seq = parsed["seq"] + 1
                self.send_tcp(TCP_ACK | TCP_FIN)
                self.our_seq += 1
                self.state = "CLOSED"
                print("  [TCP] FIN received, connection closed")

            elif flags & TCP_ACK:
                # Plain ACK (e.g. for our data segments)
                pass

        elif self.state == "FIN_SENT":
            if flags & TCP_RST:
                self.state = "CLOSED"
                print("  [TCP] RST received, "
                      "connection closed")
            elif flags & TCP_ACK:
                if flags & TCP_FIN:
                    self.their_seq = parsed["seq"] + 1
                    self.send_tcp(TCP_ACK)
                    self.state = "CLOSED"
                    print("  [TCP] FIN+ACK received, "
                          "connection closed")
                else:
                    # ACK for our FIN, wait for their FIN
                    pass

        elif self.state == "CLOSED":
            pass

    def _proxy_request(self):
        """Extract HTTP request, fetch URL, send response."""
        method, host, path = extract_http_request(
            bytes(self.http_data))

        if self.override_url:
            url = self.override_url
        elif host and path:
            url = f"http://{host}{path}"
        else:
            url = "http://example.com/"

        print(f"  [HTTP] {method} {path}")
        print(f"  [HTTP] Host: {host}")
        print(f"  [HTTP] Fetching {url} ...")

        status, resp_headers, resp_body = fetch_url(url)
        full_response = resp_headers + resp_body

        print(f"  [HTTP] Response: {status} "
              f"({len(resp_body)} bytes)")

        # Print body preview
        preview = resp_body[:300].decode("utf-8", errors="replace")
        print()
        for line in preview.split("\n")[:12]:
            print(f"    {line.rstrip()}")
        if len(resp_body) > 300:
            print(f"    ... ({len(resp_body) - 300} more bytes)")
        print()

        # Send response in MSS-sized segments.
        #
        # The device has a 256-byte RX buffer (255 usable) and
        # only accepts in-order TCP segments.  If we send more
        # than the buffer can hold, the device accepts a partial
        # segment and advances rcv_nxt by the accepted portion.
        # All subsequent segments then have seq != rcv_nxt and
        # are silently dropped.
        #
        # Strategy: send one segment at a time, wait for the
        # ACK, and track the actual ack number.  If the device
        # ACK'd fewer bytes than we sent (partial accept due to
        # full buffer), rewind our seq to match and pause to let
        # the app drain the buffer before continuing.
        offset = 0
        seg_count = 0
        seg_size = min(self.their_mss, DEVICE_MSS)

        while offset < len(full_response):
            chunk = full_response[offset:offset + seg_size]
            flags = TCP_ACK | TCP_PSH
            self.send_tcp(flags, chunk)
            sent_seq = self.our_seq
            self.our_seq += len(chunk)
            offset += len(chunk)
            seg_count += 1

            # Wait for an ACK that acknowledges NEW data
            # (peer_ack > sent_seq).  ACKs with peer_ack ==
            # sent_seq are stale window updates — skip them.
            ack_deadline = time.time() + 5.0
            got_ack = False
            peer_wnd = 0
            while time.time() < ack_deadline:
                frame = read_slip_frame(self.ser, timeout=2.0)
                if frame is None:
                    break
                p = parse_ip_tcp(frame)
                if not p or not (p["flags"] & TCP_ACK):
                    continue
                peer_ack = p["ack"]
                peer_wnd = p["window"]
                acked = (peer_ack - sent_seq) & 0xFFFFFFFF
                if acked > 0 and acked <= len(chunk):
                    got_ack = True
                    break
                # acked == 0: stale ACK, keep waiting

            if not got_ack:
                # No new data ACK'd — buffer may be full.
                # Rewind and pause for the app to drain.
                self.our_seq = sent_seq
                offset -= len(chunk)
                if peer_wnd == 0:
                    print(f"  [TCP] Window full, pausing "
                          f"for drain...")
                else:
                    print(f"  [TCP] ACK timeout, aborting")
                    break
                time.sleep(1.5)
                continue

            # Check if device accepted all bytes
            expected = len(chunk)

            if acked < expected:
                # Partial accept — rewind unsent portion
                lost = expected - acked
                self.our_seq = peer_ack
                offset -= lost
                print(f"  [TCP] Partial ACK: {acked}/"
                      f"{expected}B, window={peer_wnd}")
                if peer_wnd == 0:
                    print(f"  [TCP] Pausing for drain...")
                    time.sleep(1.5)

        print(f"  [TCP] Sent {offset} bytes in "
              f"{seg_count} segments")

        # Final pause before FIN — let device read last burst
        time.sleep(2.0)

        # Send FIN
        self.send_tcp(TCP_FIN | TCP_ACK)
        self.our_seq += 1
        self.state = "FIN_SENT"
        print("  [TCP] FIN sent")

        body_on_device = min(len(resp_body), 256)
        print(f"\n  Device received up to {body_on_device}"
              f" bytes of body")

    @property
    def done(self):
        return self.state == "CLOSED"

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(
        description="TikuOS HTTP Fetch Gateway")
    ap.add_argument("--port", "-p",
                    help="Serial port (auto-detect if omitted)")
    ap.add_argument("--baud", "-b", type=int, default=9600)
    ap.add_argument("--url", "-u",
                    help="Override URL to fetch "
                         "(default: use Host header from device)")
    args = ap.parse_args()

    port = args.port or find_port()
    if not port:
        print("Error: no serial port found. Use --port.")
        sys.exit(1)

    print("=" * 50)
    print("  TikuOS HTTP Fetch Gateway")
    print("=" * 50)
    print(f"  Port:      {port}")
    print(f"  Baud:      {args.baud}")
    print(f"  Host IP:   {'.'.join(str(b) for b in HOST_IP)}")
    print(f"  Device IP: {'.'.join(str(b) for b in DEVICE_IP)}")
    if args.url:
        print(f"  URL:       {args.url}")
    else:
        print("  URL:       (from device Host header)")
    print()

    ser = serial.Serial(port, args.baud, timeout=0.1)
    ser.dtr = False
    time.sleep(0.5)
    ser.reset_input_buffer()

    print("  Waiting for MSP430 TCP connection...\n")

    gw = TcpGateway(ser, override_url=args.url)

    frame_count = 0
    try:
        while not gw.done:
            frame = read_slip_frame(ser, timeout=60.0)
            if frame is None:
                print("  Timeout waiting for device.")
                break

            frame_count += 1
            parsed = parse_ip_tcp(frame)
            if parsed:
                flags_str = ""
                if parsed["flags"] & TCP_SYN:
                    flags_str += "SYN "
                if parsed["flags"] & TCP_ACK:
                    flags_str += "ACK "
                if parsed["flags"] & TCP_PSH:
                    flags_str += "PSH "
                if parsed["flags"] & TCP_FIN:
                    flags_str += "FIN "
                if parsed["flags"] & TCP_RST:
                    flags_str += "RST "
                print(f"  [DBG] Frame #{frame_count}: "
                      f"{len(frame)}B, "
                      f":{parsed['sport']}->"
                      f":{parsed['dport']} "
                      f"[{flags_str.strip()}] "
                      f"seq={parsed['seq']:#x} "
                      f"ack={parsed['ack']:#x} "
                      f"payload={len(parsed['payload'])}B "
                      f"state={gw.state}")
                gw.handle(parsed)
            else:
                print(f"  [DBG] Frame #{frame_count}: "
                      f"{len(frame)}B (not TCP, dropped)")

    except KeyboardInterrupt:
        print("\n  Interrupted.")
    finally:
        ser.close()

    print("\n  Done.")


if __name__ == "__main__":
    main()
