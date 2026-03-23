#!/usr/bin/env python3
"""
TikuOS HTTPS PSK-TLS Gateway

Bridges the MSP430's TLS 1.3 PSK-encrypted HTTPS request to a
real HTTPS endpoint on the internet.

Architecture:
  MSP430 <--SLIP/UART--> slip_bridge.sh <--sl0--> This script <--urllib--> Internet

The script provides two services on the SLIP gateway IP:
  1. DNS responder (UDP 53): returns gateway IP for ALL queries
  2. TLS 1.3 PSK server (TCP 443): decrypts HTTP request, fetches
     real page, sends encrypted response back

Usage:
    # Terminal 1: SLIP bridge
    sudo tools/slip_bridge.sh /dev/ttyUSB0

    # Terminal 2: this gateway (requires root for ports 53, 443)
    sudo python3 tools/https_psk_gateway.py

    # Then press RESET on MSP430

Authors: Ambuj Varshney <ambuj@tiku-os.org>
SPDX-License-Identifier: Apache-2.0
"""

import argparse
import hashlib
import hmac
import os
import socket
import struct
import sys
import threading
import urllib.request
import urllib.error

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

# ---------------------------------------------------------------------------
# Configuration (must match MSP430 firmware)
# ---------------------------------------------------------------------------

HOST_IP = "172.16.7.1"
PSK_KEY = bytes([0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10])
PSK_IDENTITY = b"tikuos_client"

# TLS 1.3 constants
TLS_VERSION = 0x0304
TLS_LEGACY = 0x0303
CIPHER_SUITE = 0x1301  # TLS_AES_128_GCM_SHA256
HASH_LEN = 32          # SHA-256
KEY_LEN = 16           # AES-128
IV_LEN = 12            # GCM nonce
TAG_LEN = 16           # GCM tag

# Content types
CT_CCS = 20
CT_ALERT = 21
CT_HANDSHAKE = 22
CT_APP_DATA = 23

# Handshake types
HT_CLIENT_HELLO = 1
HT_SERVER_HELLO = 2
HT_ENCRYPTED_EXT = 8
HT_FINISHED = 20

# Extension types
EXT_SUPPORTED_VERSIONS = 43
EXT_PSK_KEY_EXCHANGE = 45
EXT_PRE_SHARED_KEY = 41

# ---------------------------------------------------------------------------
# HKDF (RFC 5869) with SHA-256
# ---------------------------------------------------------------------------

def hkdf_extract(salt, ikm):
    """HKDF-Extract: PRK = HMAC-SHA256(salt, IKM)."""
    if salt is None or len(salt) == 0:
        salt = b'\x00' * HASH_LEN
    return hmac.new(salt, ikm, hashlib.sha256).digest()


def hkdf_expand(prk, info, length):
    """HKDF-Expand: OKM = T(1) || T(2) || ... truncated to length."""
    n = (length + HASH_LEN - 1) // HASH_LEN
    okm = b''
    t = b''
    for i in range(1, n + 1):
        t = hmac.new(prk, t + info + bytes([i]),
                     hashlib.sha256).digest()
        okm += t
    return okm[:length]


def hkdf_expand_label(secret, label, context, length):
    """TLS 1.3 HKDF-Expand-Label."""
    full_label = b"tls13 " + label.encode() if isinstance(label, str) else b"tls13 " + label
    hkdf_label = struct.pack("!H", length)
    hkdf_label += bytes([len(full_label)]) + full_label
    hkdf_label += bytes([len(context)]) + context
    return hkdf_expand(secret, hkdf_label, length)


def derive_secret(secret, label, messages_hash):
    """TLS 1.3 Derive-Secret."""
    return hkdf_expand_label(secret, label, messages_hash, HASH_LEN)

# ---------------------------------------------------------------------------
# TLS 1.3 Record Layer
# ---------------------------------------------------------------------------

def make_nonce(iv, seq):
    """Construct per-record nonce: IV XOR padded sequence number."""
    padded = seq.to_bytes(12, 'big')
    return bytes(a ^ b for a, b in zip(iv, padded))


def encrypt_record(key, iv, seq, content_type, plaintext):
    """Encrypt a TLS 1.3 record (type 23 outer, real type appended)."""
    inner = plaintext + bytes([content_type])
    nonce = make_nonce(iv, seq)
    # Build header first (needed as AAD)
    enc_len = len(inner) + TAG_LEN
    header = struct.pack("!BHH", CT_APP_DATA, TLS_LEGACY, enc_len)
    gcm = AESGCM(key)
    ct = gcm.encrypt(nonce, inner, header)
    return header + ct


def decrypt_record(key, iv, seq, header, ciphertext):
    """Decrypt a TLS 1.3 record. Returns (content_type, plaintext)."""
    nonce = make_nonce(iv, seq)
    gcm = AESGCM(key)
    inner = gcm.decrypt(nonce, ciphertext, header)
    # Last byte is real content type, strip trailing zeros
    i = len(inner) - 1
    while i > 0 and inner[i] == 0:
        i -= 1
    ct = inner[i]
    return ct, inner[:i]

# ---------------------------------------------------------------------------
# TLS 1.3 Key Schedule
# ---------------------------------------------------------------------------

SHA256_EMPTY = hashlib.sha256(b'').digest()


def tls_key_schedule(psk, ch_bytes, sh_bytes, ee_bytes, sf_bytes,
                     cf_bytes):
    """
    Derive all TLS 1.3 PSK keys from the handshake transcript.
    Returns dict with all keys/IVs needed.
    """
    ks = {}

    # Step 1: Early Secret
    early_secret = hkdf_extract(b'\x00' * HASH_LEN, psk)
    ks['early_secret'] = early_secret

    # Binder key
    ks['binder_key'] = derive_secret(early_secret, "ext binder",
                                      SHA256_EMPTY)

    # Step 2: Handshake Secret
    derived1 = derive_secret(early_secret, "derived", SHA256_EMPTY)
    hs_secret = hkdf_extract(derived1, b'\x00' * HASH_LEN)
    ks['hs_secret'] = hs_secret

    # Transcript hash after CH + SH
    h_ch_sh = hashlib.sha256(ch_bytes + sh_bytes).digest()

    c_hs = derive_secret(hs_secret, "c hs traffic", h_ch_sh)
    s_hs = derive_secret(hs_secret, "s hs traffic", h_ch_sh)
    ks['c_hs_traffic'] = c_hs
    ks['s_hs_traffic'] = s_hs

    ks['s_hs_key'] = hkdf_expand_label(s_hs, "key", b'', KEY_LEN)
    ks['s_hs_iv'] = hkdf_expand_label(s_hs, "iv", b'', IV_LEN)
    ks['c_hs_key'] = hkdf_expand_label(c_hs, "key", b'', KEY_LEN)
    ks['c_hs_iv'] = hkdf_expand_label(c_hs, "iv", b'', IV_LEN)

    # Step 3: Master Secret (after full handshake)
    derived2 = derive_secret(hs_secret, "derived", SHA256_EMPTY)
    master = hkdf_extract(derived2, b'\x00' * HASH_LEN)
    ks['master_secret'] = master

    h_full = hashlib.sha256(
        ch_bytes + sh_bytes + ee_bytes + sf_bytes + cf_bytes
    ).digest()

    c_app = derive_secret(master, "c ap traffic", h_full)
    s_app = derive_secret(master, "s ap traffic", h_full)
    ks['c_app_traffic'] = c_app
    ks['s_app_traffic'] = s_app

    ks['s_app_key'] = hkdf_expand_label(s_app, "key", b'', KEY_LEN)
    ks['s_app_iv'] = hkdf_expand_label(s_app, "iv", b'', IV_LEN)
    ks['c_app_key'] = hkdf_expand_label(c_app, "key", b'', KEY_LEN)
    ks['c_app_iv'] = hkdf_expand_label(c_app, "iv", b'', IV_LEN)

    return ks

# ---------------------------------------------------------------------------
# ClientHello Parser
# ---------------------------------------------------------------------------

def parse_client_hello(data):
    """Parse ClientHello handshake message (without record header)."""
    if data[0] != HT_CLIENT_HELLO:
        raise ValueError("Not a ClientHello")

    msg_len = (data[1] << 16) | (data[2] << 8) | data[3]
    pos = 4

    legacy_ver = (data[pos] << 8) | data[pos + 1]
    pos += 2

    client_random = data[pos:pos + 32]
    pos += 32

    sess_id_len = data[pos]
    pos += 1
    session_id = data[pos:pos + sess_id_len]
    pos += sess_id_len

    cs_len = (data[pos] << 8) | data[pos + 1]
    pos += 2 + cs_len  # skip cipher suites

    comp_len = data[pos]
    pos += 1 + comp_len  # skip compression

    ext_len = (data[pos] << 8) | data[pos + 1]
    pos += 2

    psk_identity = None
    psk_binder = None
    binder_start = None

    ext_end = pos + ext_len
    while pos < ext_end:
        etype = (data[pos] << 8) | data[pos + 1]
        elen = (data[pos + 2] << 8) | data[pos + 3]
        pos += 4

        if etype == EXT_PRE_SHARED_KEY:
            # identities
            id_list_len = (data[pos] << 8) | data[pos + 1]
            id_len = (data[pos + 2] << 8) | data[pos + 3]
            psk_identity = data[pos + 4:pos + 4 + id_len]
            # skip ticket age (4 bytes after identity)
            binders_off = pos + 2 + 2 + id_len + 4
            binders_len = (data[binders_off] << 8) | data[binders_off + 1]
            binder_len = data[binders_off + 2]
            binder_start = binders_off + 3
            psk_binder = data[binder_start:binder_start + binder_len]

        pos += elen

    return {
        'client_random': client_random,
        'session_id': session_id,
        'psk_identity': psk_identity,
        'psk_binder': psk_binder,
        'binder_start': binder_start,
        'raw': data[:4 + msg_len],
    }

# ---------------------------------------------------------------------------
# ServerHello Builder
# ---------------------------------------------------------------------------

def build_server_hello(server_random, session_id):
    """Build ServerHello handshake message bytes."""
    body = b''
    body += struct.pack("!H", TLS_LEGACY)  # legacy version
    body += server_random                   # 32 bytes
    body += bytes([len(session_id)]) + session_id  # echo session ID
    body += struct.pack("!H", CIPHER_SUITE)  # cipher suite
    body += b'\x00'                          # compression

    # Extensions
    ext = b''
    # supported_versions
    ext += struct.pack("!HH", EXT_SUPPORTED_VERSIONS, 2)
    ext += struct.pack("!H", TLS_VERSION)
    # pre_shared_key (selected identity = 0)
    ext += struct.pack("!HH", EXT_PRE_SHARED_KEY, 2)
    ext += struct.pack("!H", 0)

    body += struct.pack("!H", len(ext)) + ext

    # Handshake header
    msg = bytes([HT_SERVER_HELLO])
    msg += struct.pack("!I", len(body))[1:]  # 24-bit length
    msg += body
    return msg


def build_encrypted_extensions():
    """Build EncryptedExtensions (empty extensions)."""
    body = struct.pack("!H", 0)  # no extensions
    msg = bytes([HT_ENCRYPTED_EXT])
    msg += struct.pack("!I", len(body))[1:]
    msg += body
    return msg


def build_finished(hs_traffic_secret, transcript_hash):
    """Build Finished message."""
    fin_key = hkdf_expand_label(hs_traffic_secret, "finished",
                                 b'', HASH_LEN)
    verify_data = hmac.new(fin_key, transcript_hash,
                           hashlib.sha256).digest()
    msg = bytes([HT_FINISHED])
    msg += struct.pack("!I", HASH_LEN)[1:]  # length = 32
    msg += verify_data
    return msg

# ---------------------------------------------------------------------------
# DNS Responder
# ---------------------------------------------------------------------------

def dns_respond(query, gateway_ip_bytes):
    """Build a minimal DNS A-record response."""
    # Copy transaction ID and flags
    txn_id = query[:2]
    # Response flags: QR=1, RD=1, RA=1
    flags = struct.pack("!H", 0x8180)
    # Counts: QD=1, AN=1, NS=0, AR=0
    counts = struct.pack("!HHHH", 1, 1, 0, 0)

    # Echo the question section
    pos = 12
    while pos < len(query) and query[pos] != 0:
        pos += 1 + query[pos]
    pos += 1 + 4  # skip null + QTYPE + QCLASS
    question = query[12:pos]

    # Answer: compressed name pointer + A record
    answer = b'\xc0\x0c'  # pointer to name in question
    answer += struct.pack("!HHI", 1, 1, 300)  # TYPE=A, CLASS=IN, TTL
    answer += struct.pack("!H", 4) + gateway_ip_bytes

    return txn_id + flags + counts + question + answer


def dns_server(host_ip):
    """Run DNS responder in a thread."""
    ip_bytes = bytes(int(x) for x in host_ip.split('.'))
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host_ip, 53))
    print(f"  [DNS] Listening on {host_ip}:53")

    while True:
        try:
            data, addr = sock.recvfrom(512)
            if len(data) < 12:
                continue
            resp = dns_respond(data, ip_bytes)
            sock.sendto(resp, addr)
            # Extract query name for display
            pos = 12
            labels = []
            while pos < len(data) and data[pos] != 0:
                l = data[pos]
                labels.append(data[pos+1:pos+1+l].decode('ascii',
                              errors='replace'))
                pos += 1 + l
            name = '.'.join(labels)
            print(f"  [DNS] {name} -> {host_ip}")
        except Exception as e:
            print(f"  [DNS] Error: {e}")

# ---------------------------------------------------------------------------
# TLS PSK Handshake
# ---------------------------------------------------------------------------

def recv_record(sock):
    """Receive one TLS record from socket."""
    hdr = b''
    while len(hdr) < 5:
        chunk = sock.recv(5 - len(hdr))
        if not chunk:
            return None, None, None
        hdr += chunk

    ct = hdr[0]
    length = (hdr[3] << 8) | hdr[4]
    payload = b''
    while len(payload) < length:
        chunk = sock.recv(length - len(payload))
        if not chunk:
            return None, None, None
        payload += chunk

    return ct, hdr, payload


def send_plaintext_record(sock, content_type, data):
    """Send a plaintext TLS record."""
    hdr = struct.pack("!BHH", content_type, TLS_LEGACY, len(data))
    sock.sendall(hdr + data)


def do_tls_handshake(conn):
    """
    Perform TLS 1.3 PSK handshake as server.
    Returns (app_client_key, app_client_iv,
             app_server_key, app_server_iv) or raises on error.
    """
    # 1. Receive ClientHello
    ct, hdr, payload = recv_record(conn)
    if ct is None or ct != CT_HANDSHAKE:
        raise ValueError(f"Expected Handshake, got ct={ct}")

    ch = parse_client_hello(payload)
    print(f"  [TLS] ClientHello: identity="
          f"{ch['psk_identity'].decode('ascii', errors='replace')}")

    if ch['psk_identity'] != PSK_IDENTITY:
        raise ValueError(f"Unknown PSK identity: {ch['psk_identity']}")

    # 2. Verify PSK binder
    early_secret = hkdf_extract(b'\x00' * HASH_LEN, PSK_KEY)
    binder_key = derive_secret(early_secret, "ext binder", SHA256_EMPTY)

    # Truncated CH: everything up to but excluding the binder value
    trunc_ch = payload[:ch['binder_start']]
    trunc_hash = hashlib.sha256(trunc_ch).digest()
    fin_key = hkdf_expand_label(binder_key, "finished", b'', HASH_LEN)
    expected_binder = hmac.new(fin_key, trunc_hash,
                               hashlib.sha256).digest()

    if not hmac.compare_digest(ch['psk_binder'], expected_binder):
        raise ValueError("PSK binder verification failed")
    print("  [TLS] PSK binder verified")

    ch_bytes = ch['raw']  # Full ClientHello for transcript

    # 3. Build and send ServerHello
    server_random = os.urandom(32)
    sh_bytes = build_server_hello(server_random, ch['session_id'])
    send_plaintext_record(conn, CT_HANDSHAKE, sh_bytes)
    print("  [TLS] ServerHello sent")

    # 4. Derive handshake keys
    derived1 = derive_secret(early_secret, "derived", SHA256_EMPTY)
    hs_secret = hkdf_extract(derived1, b'\x00' * HASH_LEN)

    h_ch_sh = hashlib.sha256(ch_bytes + sh_bytes).digest()
    s_hs = derive_secret(hs_secret, "s hs traffic", h_ch_sh)
    c_hs = derive_secret(hs_secret, "c hs traffic", h_ch_sh)

    s_hs_key = hkdf_expand_label(s_hs, "key", b'', KEY_LEN)
    s_hs_iv = hkdf_expand_label(s_hs, "iv", b'', IV_LEN)
    c_hs_key = hkdf_expand_label(c_hs, "key", b'', KEY_LEN)
    c_hs_iv = hkdf_expand_label(c_hs, "iv", b'', IV_LEN)

    s_hs_seq = 0  # Server handshake encryption sequence

    # 5. Send EncryptedExtensions
    ee_bytes = build_encrypted_extensions()
    rec = encrypt_record(s_hs_key, s_hs_iv, s_hs_seq,
                         CT_HANDSHAKE, ee_bytes)
    conn.sendall(rec)
    s_hs_seq += 1
    print("  [TLS] EncryptedExtensions sent")

    # 6. Send Server Finished
    h_ch_sh_ee = hashlib.sha256(
        ch_bytes + sh_bytes + ee_bytes).digest()
    sf_bytes = build_finished(s_hs, h_ch_sh_ee)
    rec = encrypt_record(s_hs_key, s_hs_iv, s_hs_seq,
                         CT_HANDSHAKE, sf_bytes)
    conn.sendall(rec)
    s_hs_seq += 1
    print("  [TLS] Server Finished sent")

    # 7. Receive Client Finished (may be preceded by CCS)
    c_hs_seq = 0
    while True:
        ct, hdr, payload = recv_record(conn)
        if ct is None:
            raise ValueError("Connection closed during handshake")
        if ct == CT_CCS:
            # Ignore ChangeCipherSpec (middlebox compat)
            continue
        if ct == CT_APP_DATA:
            # Encrypted handshake record
            real_ct, inner = decrypt_record(
                c_hs_key, c_hs_iv, c_hs_seq, hdr, payload)
            c_hs_seq += 1
            if real_ct != CT_HANDSHAKE:
                raise ValueError(
                    f"Expected Handshake, got ct={real_ct}")
            if inner[0] != HT_FINISHED:
                raise ValueError(
                    f"Expected Finished, got type={inner[0]}")
            cf_bytes = inner
            break
        else:
            raise ValueError(f"Unexpected record type {ct}")

    # 8. Verify Client Finished
    h_ch_sh_ee_sf = hashlib.sha256(
        ch_bytes + sh_bytes + ee_bytes + sf_bytes).digest()
    c_fin_key = hkdf_expand_label(c_hs, "finished", b'', HASH_LEN)
    expected_cf = hmac.new(c_fin_key, h_ch_sh_ee_sf,
                           hashlib.sha256).digest()
    received_cf = cf_bytes[4:]  # skip 4-byte header
    if not hmac.compare_digest(received_cf, expected_cf):
        raise ValueError("Client Finished verification failed")
    print("  [TLS] Client Finished verified — handshake complete!")

    # 9. Derive application traffic keys
    derived2 = derive_secret(hs_secret, "derived", SHA256_EMPTY)
    master = hkdf_extract(derived2, b'\x00' * HASH_LEN)

    h_full = hashlib.sha256(
        ch_bytes + sh_bytes + ee_bytes + sf_bytes + cf_bytes
    ).digest()

    c_app = derive_secret(master, "c ap traffic", h_full)
    s_app = derive_secret(master, "s ap traffic", h_full)

    return {
        'c_key': hkdf_expand_label(c_app, "key", b'', KEY_LEN),
        'c_iv': hkdf_expand_label(c_app, "iv", b'', IV_LEN),
        's_key': hkdf_expand_label(s_app, "key", b'', KEY_LEN),
        's_iv': hkdf_expand_label(s_app, "iv", b'', IV_LEN),
    }

# ---------------------------------------------------------------------------
# HTTP Proxy
# ---------------------------------------------------------------------------

def extract_http_request(data):
    """Extract method, host, path from HTTP request."""
    text = data.decode('ascii', errors='replace')
    lines = text.split('\r\n')
    if not lines:
        return None, None, None
    parts = lines[0].split(' ')
    method = parts[0] if len(parts) >= 1 else None
    path = parts[1] if len(parts) >= 2 else '/'
    host = None
    for line in lines[1:]:
        if line.lower().startswith('host:'):
            host = line.split(':', 1)[1].strip()
            break
    return method, host, path


def fetch_url(url):
    """Fetch a URL and return (status, response_bytes)."""
    try:
        req = urllib.request.Request(url)
        req.add_header("User-Agent", "TikuOS-Gateway/1.0")
        resp = urllib.request.urlopen(req, timeout=10)
        body = resp.read()
        headers = f"HTTP/1.1 {resp.status} {resp.reason}\r\n"
        ct = resp.headers.get("Content-Type", "text/html")
        headers += f"Content-Type: {ct}\r\n"
        headers += f"Content-Length: {len(body)}\r\n"
        headers += "Connection: close\r\n\r\n"
        return resp.status, headers.encode('ascii') + body
    except urllib.error.HTTPError as e:
        body = e.read() if e.fp else b""
        headers = f"HTTP/1.1 {e.code} {e.reason}\r\n"
        headers += "Connection: close\r\n\r\n"
        return e.code, headers.encode('ascii') + body
    except Exception as e:
        body = str(e).encode('ascii')
        headers = b"HTTP/1.1 502 Bad Gateway\r\n"
        headers += b"Connection: close\r\n\r\n"
        return 502, headers + body

# ---------------------------------------------------------------------------
# TLS Application Data Exchange
# ---------------------------------------------------------------------------

def handle_connection(conn, addr, override_url):
    """Handle one TLS connection from the MSP430."""
    print(f"\n  [TCP] Connection from {addr[0]}:{addr[1]}")

    try:
        # TLS handshake
        keys = do_tls_handshake(conn)

        # Read encrypted HTTP request
        c_seq = 0
        http_data = b''
        while True:
            ct, hdr, payload = recv_record(conn)
            if ct is None:
                break
            if ct == CT_APP_DATA:
                real_ct, inner = decrypt_record(
                    keys['c_key'], keys['c_iv'], c_seq,
                    hdr, payload)
                c_seq += 1
                if real_ct == CT_APP_DATA:
                    http_data += inner
                    if b'\r\n\r\n' in http_data:
                        break
                elif real_ct == CT_ALERT:
                    print("  [TLS] Alert received")
                    break

        if not http_data:
            print("  [TLS] No HTTP data received")
            return

        # Parse HTTP request
        method, host, path = extract_http_request(http_data)
        if override_url:
            url = override_url
        elif host and path:
            url = f"https://{host}{path}"
        else:
            url = "https://example.com/"

        print(f"  [HTTP] {method} {path}")
        print(f"  [HTTP] Host: {host}")
        print(f"  [HTTP] Fetching {url} ...")

        status, response = fetch_url(url)
        print(f"  [HTTP] Response: {status} ({len(response)} bytes)")

        # Preview
        preview = response[:300].decode('utf-8', errors='replace')
        for line in preview.split('\n')[:8]:
            print(f"    {line.rstrip()}")
        if len(response) > 300:
            print(f"    ... ({len(response) - 300} more bytes)")

        # Send encrypted response in fragments
        s_seq = 0
        MAX_FRAG = 200  # stay under TLS max frag
        offset = 0
        while offset < len(response):
            chunk = response[offset:offset + MAX_FRAG]
            rec = encrypt_record(keys['s_key'], keys['s_iv'],
                                 s_seq, CT_APP_DATA, chunk)
            conn.sendall(rec)
            s_seq += 1
            offset += len(chunk)

        print(f"  [TLS] Sent {offset} bytes in {s_seq} records")

        # Send close_notify alert
        alert = bytes([1, 0])  # close_notify (warning, close_notify)
        rec = encrypt_record(keys['s_key'], keys['s_iv'],
                             s_seq, CT_ALERT, alert)
        conn.sendall(rec)
        print("  [TLS] close_notify sent")

    except Exception as e:
        print(f"  [TLS] Error: {e}")
    finally:
        conn.close()

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description="TikuOS HTTPS PSK-TLS Gateway")
    ap.add_argument("--host", default=HOST_IP,
                    help=f"Bind IP (default: {HOST_IP})")
    ap.add_argument("--port", type=int, default=443,
                    help="TLS port (default: 443)")
    ap.add_argument("--url", help="Override URL to fetch")
    args = ap.parse_args()

    print("=" * 50)
    print("  TikuOS HTTPS PSK-TLS Gateway")
    print("=" * 50)
    print(f"  Bind:     {args.host}")
    print(f"  TLS Port: {args.port}")
    print(f"  DNS Port: 53")
    print(f"  PSK ID:   {PSK_IDENTITY.decode()}")
    if args.url:
        print(f"  URL:      {args.url}")
    else:
        print("  URL:      (from Host header)")
    print()

    # Start DNS responder thread
    dns_thread = threading.Thread(target=dns_server,
                                  args=(args.host,),
                                  daemon=True)
    dns_thread.start()

    # TLS server
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(1)
    print(f"  [TLS] Listening on {args.host}:{args.port}")
    print()
    print("  Waiting for MSP430 TLS connection...")
    print()

    try:
        while True:
            conn, addr = srv.accept()
            handle_connection(conn, addr, args.url)
    except KeyboardInterrupt:
        print("\n  Interrupted.")
    finally:
        srv.close()

    print("  Done.")


if __name__ == "__main__":
    main()
