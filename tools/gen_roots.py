#!/usr/bin/env python3
"""
Tiku Operating System v0.06
Simple. Ubiquitous. Intelligence, Everywhere.
http://tiku-os.org

gen_roots.py - build the HTTPS trust store as a /data file instead of .rodata.

WHY.  The trust store is 120 CA root certificates, 128,820 bytes of DER, and it
shipped compiled into .rodata via a generated 677 KB C file.  That made it the
single largest thing in the cert-TLS image (307,637 B of 256 KB window) and it
made trust un-updatable: roots expire and get revoked, and the only way to
change one was a firmware respin.  Certificates are DATA, so they belong in the
one self-describing store, reachable by name.

THE MIGRATION GATE.  This tool packs from the .inl that ships TODAY, and can
regenerate that .inl from the packed file.  Round-tripping and diffing proves
the file carries exactly the bytes the firmware already trusts -- no
re-derivation from an upstream bundle, no chance of a silently different root
set.  `--self-test` does exactly that and is the thing to run before believing
any of this.

Refreshing the root set from upstream is a separate mode (--from-pem) and a
separate decision; it is deliberately not part of the migration.

FILE FORMAT (little-endian, the byte order of every supported MCU):

    offset  size  field
    0       4     magic       'TRST'
    4       4     version     1
    8       4     count       number of roots
    12      4     der_off     start of the DER blob (28)
    16      4     der_len     total DER bytes
    20      4     table_off   start of the descriptor table (4-aligned)
    24      4     crc32       over [der_off, end of file)
    ...           DER blob    every root's certificate, concatenated
    ...           table       count x { der_off, der_len, subj_off, subj_len }

Table offsets are relative to the DER blob, not the file, so the loader adds one
mapped base pointer and is done.  The subject DN is a slice of the cert it
belongs to -- that is what lets anchor matching compare a DN without parsing
every root -- so subj_off always lies inside its own entry's range, which
--verify checks.

SPDX-License-Identifier: Apache-2.0
"""

import argparse
import re
import struct
import sys
import zlib

MAGIC = 0x54535254          # 'TRST' little-endian
VERSION = 1
HDR_FMT = "<7I"
HDR_LEN = struct.calcsize(HDR_FMT)
assert HDR_LEN == 28

INL_ARRAY = "tiku_https_roots_der"
INL_TABLE = "tiku_https_roots"
INL_COUNT = "TIKU_HTTPS_NROOTS"


# --------------------------------------------------------------------------
# reading the shipping .inl
# --------------------------------------------------------------------------

def parse_inl(path):
    """Extract (der_bytes, [(der_off, der_len, subj_off, subj_len)]) from the
    generated C the firmware compiles today."""
    text = open(path, "r").read()
    try:
        body = text.split(INL_ARRAY + "[] = {")[1].split("};")[0]
    except IndexError:
        raise SystemExit("%s: no %s[] array found" % (path, INL_ARRAY))
    der = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", body))

    entries = [tuple(int(g) for g in m.groups()) for m in re.finditer(
        r"\{\s*" + INL_ARRAY + r"\+(\d+),\s*(\d+),\s*"
        + INL_ARRAY + r"\+(\d+),\s*(\d+)\s*\}", text)]

    declared = re.search(r"#define\s+" + INL_COUNT + r"\s+(\d+)", text)
    if declared and int(declared.group(1)) != len(entries):
        raise SystemExit("%s: %s says %s but %d entries parsed"
                         % (path, INL_COUNT, declared.group(1), len(entries)))
    return der, entries


# --------------------------------------------------------------------------
# reading an upstream Mozilla bundle (root-set REFRESH, not migration)
# --------------------------------------------------------------------------

# The verify kit supports RSA with SHA-256/384/512 and ECDSA on P-256/P-384.
# A root outside that set cannot be used to validate anything, so shipping it
# would only cost bytes -- but dropping one silently would shrink the trust set
# without anyone noticing, which is why --from-pem reports what it skipped.
OID_RSA_PK = bytes.fromhex("2a864886f70d010101")
OID_EC_PK = bytes.fromhex("2a8648ce3d0201")
OID_P256 = bytes.fromhex("2a8648ce3d030107")
OID_P384 = bytes.fromhex("2b81040022")


def _asn1_len(buf, i):
    """Return (length, index-of-first-content-byte) for a DER length at i."""
    n = buf[i]
    if n < 0x80:
        return n, i + 1
    k = n & 0x7F
    return int.from_bytes(buf[i + 1:i + 1 + k], "big"), i + 1 + k


def _asn1_children(buf, i):
    """Yield (tag, header_start, content_start, content_len) of a SEQUENCE's
    children, given the index of the SEQUENCE's tag byte."""
    if buf[i] not in (0x30, 0x31):
        return
    total, start = _asn1_len(buf, i + 1)
    end = start + total
    j = start
    while j < end:
        tag = buf[j]
        ln, cs = _asn1_len(buf, j + 1)
        yield tag, j, cs, ln
        j = cs + ln


def find_subject_dn(der):
    """Locate the subject DN inside a certificate, as (offset, length) covering
    the whole RDNSequence including its tag and length bytes.

    Certificate ::= SEQUENCE { tbsCertificate, signatureAlgorithm, signature }
    TBSCertificate ::= SEQUENCE { [0] version OPTIONAL, serialNumber,
                                  signature, issuer, validity, subject, ... }
    so the subject is the second Name -- the 6th field with an explicit version
    present, the 5th without.
    """
    kids = list(_asn1_children(der, 0))
    if not kids:
        raise ValueError("not a SEQUENCE")
    tbs_hdr = kids[0][1]
    fields = list(_asn1_children(der, tbs_hdr))
    if not fields:
        raise ValueError("empty TBSCertificate")
    # [0] EXPLICIT version is context-specific constructed 0xA0.
    base = 1 if fields[0][0] == 0xA0 else 0
    #        version?  serial  sigalg  issuer  validity  subject
    idx = base + 4
    if idx >= len(fields):
        raise ValueError("no subject field")
    tag, hdr, _cs, _ln = fields[idx]
    if tag != 0x30:
        raise ValueError("subject is not a SEQUENCE (tag 0x%02X)" % tag)
    nxt = fields[idx + 1][1] if idx + 1 < len(fields) else None
    if nxt is None:
        raise ValueError("subject not delimited")
    return hdr, nxt - hdr


def key_supported(der):
    """True when the root's public key is one the verify kit can use."""
    return (OID_RSA_PK in der) or \
           (OID_EC_PK in der and (OID_P256 in der or OID_P384 in der))


def parse_pem(path):
    import base64
    text = open(path, "r").read()
    blocks = re.findall(
        r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----",
        text, re.S)
    der_blob = bytearray()
    entries = []
    skipped_alg = 0
    skipped_parse = 0
    for b64 in blocks:
        der = base64.b64decode("".join(b64.split()))
        if not key_supported(der):
            skipped_alg += 1
            continue
        try:
            so, sl = find_subject_dn(der)
        except ValueError:
            skipped_parse += 1
            continue
        off = len(der_blob)
        der_blob += der
        entries.append((off, len(der), off + so, sl))
    print("from-pem: %d certificates, %d packed, %d unsupported algorithm, "
          "%d unparsable" % (len(blocks), len(entries), skipped_alg,
                             skipped_parse), file=sys.stderr)
    return bytes(der_blob), entries


# --------------------------------------------------------------------------
# packing / unpacking
# --------------------------------------------------------------------------

def pack(der, entries):
    der_off = HDR_LEN
    pad = (-len(der)) % 4                      # keep the table 4-aligned
    table_off = der_off + len(der) + pad
    table = b"".join(struct.pack("<4I", *e) for e in entries)
    payload = der + (b"\x00" * pad) + table
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    hdr = struct.pack(HDR_FMT, MAGIC, VERSION, len(entries), der_off,
                      len(der), table_off, crc)
    return hdr + payload


def unpack(blob):
    if len(blob) < HDR_LEN:
        raise SystemExit("file shorter than a header")
    magic, ver, count, der_off, der_len, table_off, crc = \
        struct.unpack_from(HDR_FMT, blob, 0)
    if magic != MAGIC:
        raise SystemExit("bad magic 0x%08X (want 0x%08X)" % (magic, MAGIC))
    if ver != VERSION:
        raise SystemExit("unsupported version %d" % ver)
    if der_off != HDR_LEN:
        raise SystemExit("der_off %d is not %d" % (der_off, HDR_LEN))
    if table_off + count * 16 != len(blob):
        raise SystemExit("table does not end at EOF (declared %d, file %d)"
                         % (table_off + count * 16, len(blob)))
    if zlib.crc32(blob[der_off:]) & 0xFFFFFFFF != crc:
        raise SystemExit("CRC mismatch")
    der = blob[der_off:der_off + der_len]
    entries = [struct.unpack_from("<4I", blob, table_off + 16 * i)
               for i in range(count)]
    return der, entries


def verify(der, entries):
    """Everything the loader is entitled to assume, checked here so it does not
    have to be discovered on a board."""
    problems = []
    for i, (do, dl, so, sl) in enumerate(entries):
        if do + dl > len(der):
            problems.append("entry %d: cert runs past the blob" % i)
        if not (do <= so and so + sl <= do + dl):
            problems.append("entry %d: subject DN outside its own cert" % i)
        if dl == 0 or sl == 0:
            problems.append("entry %d: zero-length field" % i)
        if do < len(der) and der[do] != 0x30:
            problems.append("entry %d: cert does not start with SEQUENCE" % i)
    covered = sum(dl for _do, dl, _so, _sl in entries)
    if covered != len(der):
        problems.append("entries cover %d bytes of a %d-byte blob"
                        % (covered, len(der)))
    return problems


# --------------------------------------------------------------------------
# regenerating the .inl (the round-trip half of the gate)
# --------------------------------------------------------------------------

INL_HEADER = """/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_https_roots.inl - generated trust store
 *                              (RSA-2048/4096 + P-256 + P-384 roots)
 *
 * NOT a standalone translation unit.  Included from tiku_basic_https.inl.
 *
 * Source: Mozilla CA bundle (curl.se/ca/cacert.pem), filtered to the
 * algorithms the tiku verify kit supports.  Regenerate via gen_roots.c.
 * Do not edit by hand.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
"""


def to_inl(der, entries):
    out = [INL_HEADER]
    out.append("static const unsigned char %s[] = {\n" % INL_ARRAY)
    for i in range(0, len(der), 16):
        row = der[i:i + 16]
        out.append("  " + ",".join("0x%02x" % b for b in row) +
                   ("," if i + 16 < len(der) else "") + "\n")
    out.append("};\n\n")
    out.append("static const tiku_kits_crypto_x509_root_t %s[] = {\n"
               % INL_TABLE)
    for do, dl, so, sl in entries:
        out.append("  { %s+%d, %d, %s+%d, %d },\n"
                   % (INL_ARRAY, do, dl, INL_ARRAY, so, sl))
    out.append("};\n")
    out.append("#define %s %d\n" % (INL_COUNT, len(entries)))
    return "".join(out)


# --------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[5])
    ap.add_argument("--from-inl", metavar="FILE",
                    help="pack from the generated C that ships today")
    ap.add_argument("--from-pem", metavar="FILE",
                    help="pack from an upstream Mozilla bundle (root REFRESH)")
    ap.add_argument("--to-inl", metavar="FILE",
                    help="regenerate the .inl from a packed file")
    ap.add_argument("--verify", metavar="FILE",
                    help="check a packed file's header, CRC and table")
    ap.add_argument("--self-test", metavar="INL",
                    help="pack INL, unpack it, and require the round trip to "
                         "reproduce every byte -- the migration gate")
    ap.add_argument("-o", "--out", metavar="FILE", help="output path")
    args = ap.parse_args(argv)

    if args.self_test:
        der, entries = parse_inl(args.self_test)
        problems = verify(der, entries)
        if problems:
            for p in problems:
                print("  " + p, file=sys.stderr)
            raise SystemExit("source .inl is not self-consistent")
        blob = pack(der, entries)
        rt_der, rt_entries = unpack(blob)
        ok = True
        if rt_der != der:
            print("FAIL: DER blob differs after round trip", file=sys.stderr)
            ok = False
        if rt_entries != entries:
            print("FAIL: descriptor table differs after round trip",
                  file=sys.stderr)
            ok = False
        regen = to_inl(rt_der, rt_entries)
        original = open(args.self_test, "r").read()
        if regen != original:
            print("NOTE: regenerated .inl is not textually identical to the "
                  "original (formatting); comparing parsed content instead",
                  file=sys.stderr)
            re_der, re_entries = parse_inl_text(regen)
            if re_der != der or re_entries != entries:
                print("FAIL: regenerated .inl parses to different content",
                      file=sys.stderr)
                ok = False
        print("self-test: %d roots, %d DER bytes, %d packed bytes -- %s"
              % (len(entries), len(der), len(blob),
                 "round trip is byte-exact" if ok else "FAILED"))
        return 0 if ok else 1

    if args.verify:
        der, entries = unpack(open(args.verify, "rb").read())
        problems = verify(der, entries)
        for p in problems:
            print("  " + p, file=sys.stderr)
        print("verify: %d roots, %d DER bytes -- %s"
              % (len(entries), len(der), "ok" if not problems else "PROBLEMS"))
        return 1 if problems else 0

    if args.to_inl:
        der, entries = unpack(open(args.to_inl, "rb").read())
        text = to_inl(der, entries)
        if args.out:
            open(args.out, "w").write(text)
        else:
            sys.stdout.write(text)
        return 0

    if args.from_inl or args.from_pem:
        if args.from_inl:
            der, entries = parse_inl(args.from_inl)
        else:
            der, entries = parse_pem(args.from_pem)
        problems = verify(der, entries)
        if problems:
            for p in problems:
                print("  " + p, file=sys.stderr)
            raise SystemExit("refusing to pack an inconsistent trust store")
        blob = pack(der, entries)
        if not args.out:
            raise SystemExit("--out is required when packing")
        open(args.out, "wb").write(blob)
        print("packed %d roots, %d DER bytes -> %s (%d bytes)"
              % (len(entries), len(der), args.out, len(blob)))
        return 0

    ap.print_help()
    return 2


def parse_inl_text(text):
    """parse_inl() for a string rather than a path (round-trip comparison)."""
    import tempfile
    import os
    fd, path = tempfile.mkstemp(suffix=".inl")
    try:
        with os.fdopen(fd, "w") as fh:
            fh.write(text)
        return parse_inl(path)
    finally:
        os.unlink(path)


if __name__ == "__main__":
    sys.exit(main())
