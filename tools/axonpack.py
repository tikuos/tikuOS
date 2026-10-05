#!/usr/bin/env python3
"""
axonpack.py - pack a compiled Axon NN model into a store-resident .axm file.

Tiku Operating System v0.06
Simple. Ubiquitous. Intelligence, Everywhere.
http://tiku-os.org

Authors: Ambuj Varshney <ambuj@tiku-os.org>

Copies one model's sections out of a compiled object into a file the loader
maps from the store, with a table of every word the loader patches.  --kat
packs the full-model test vectors; --verify-elf checks the table on a link.

SPDX-License-Identifier: Apache-2.0
"""

import argparse
import re
import struct
import subprocess
import sys
import zlib

# The .axm format.  Nordic's Axon compiler emits a model as const C arrays:
# the weights (axon_model_const_<name>), the command buffer the NPU runs and a
# descriptor (model_<name>) with its labels.  The command buffer and the
# descriptor hold link-time addresses (R_ARM_ABS32) of weight members, of the
# string pool and of firmware symbols.  The SDK reads no field that would
# repoint the weights (model_const_ptr is unused), so the loader patches every
# such word.
#
# ARM relocations are REL: the addend is stored in place, so a site's word in
# the object file is its offset within its target, and the loader writes
#
#     site_word = target_runtime_address + stored_addend
#
# The file carries the sections unrelocated, a site table of (section id,
# symbol index, offset) entries, and the symbol names.  A name starting with
# '@' is one of the model's own sections (@weights, @cmd, @labels, @strings)
# or a buffer the caller supplies (@packed_out); any other name is a firmware
# symbol the loader resolves by name.  Only the full-model path is packed:
# --list reports the layer-by-layer sections that also reference the weights.
#
# Layout: a 96-byte header of 23 little-endian words; the weights, command
# buffer, descriptor, labels and string pool, each 4-byte aligned; the site
# table, 8 bytes per site; the NUL-separated names.  Header words: magic
# 'AXM1', version, header bytes, offset and length of the weights, command
# buffer, descriptor, labels and strings, site-table offset and count, names
# offset and count, packed-output buffer size, label count, and CRC-32s of the
# weights, the command buffer, the descriptor through the strings, and the
# site table through the names.
#
#     python3 tools/axonpack.py --obj <compiled .o> --model tinyml_vww \
#             --out temp/vww.axm [--verify-elf main.elf]

AXM_MAGIC = 0x314D5841          # 'AXM1' little-endian
AXM_VERSION = 3                 # v3 adds the descriptor, labels and strings
AXM_HDR_BYTES = 96              # 23 header words, and a multiple of 16 so the
                                # weights stay 16-byte aligned within the file

# Section ids for the site table's sect field: the sections the loader
# patches.  The weights and the string pool are used as mapped.
SECT_CMD, SECT_DESC, SECT_LABELS = 0, 1, 2

# Relocation targets that name a part of the model, not a firmware symbol.
# The loader resolves these itself; they never reach the symbol registry.
SYM_WEIGHTS = "@weights"
SYM_CMD = "@cmd"
SYM_LABELS = "@labels"
SYM_STRINGS = "@strings"
SYM_PACKED_OUT = "@packed_out"

OBJDUMP = "arm-none-eabi-objdump"
OBJCOPY = "arm-none-eabi-objcopy"
NM = "arm-none-eabi-nm"


def run(*cmd):
    """Run a tool and return its stdout; exit with its stderr on failure."""
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("axonpack: %s failed:\n%s" % (cmd[0], r.stderr.strip()))
    return r.stdout


def section_sizes(obj):
    """name -> byte length, from objdump -h."""
    out = {}
    for line in run(OBJDUMP, "-h", obj).splitlines():
        f = line.split()
        if len(f) >= 6 and f[0].isdigit():
            out[f[1]] = int(f[2], 16)
    return out


def section_bytes(obj, name):
    """Raw contents of one section, dumped with objcopy."""
    tmp = "/tmp/axonpack_sect.bin"
    run(OBJCOPY, "-O", "binary", "--only-section=" + name, obj, tmp)
    with open(tmp, "rb") as fh:
        return fh.read()


def relocations(obj):
    """[(containing_section, site_offset, target)] for every R_ARM_ABS32."""
    out = []
    cur = None
    for line in run(OBJDUMP, "-r", obj).splitlines():
        m = re.match(r"RELOCATION RECORDS FOR \[(.+)\]:", line)
        if m:
            cur = m.group(1)
            continue
        if cur is None or "R_ARM_ABS32" not in line:
            continue
        f = line.split()
        if len(f) >= 3:
            out.append((cur, int(f[0], 16), f[2]))
    return out


def sym_name(target):
    """
    The symbol a relocation target names.  objdump names a target by symbol or
    by section: a section target such as '.rodata.axon_model_const_tinyml_vww'
    loses its section prefix, and a plain symbol is returned as it is.
    """
    for pfx in (".rodata.", ".data.", ".bss.", ".text."):
        if target.startswith(pfx):
            return target[len(pfx):]
    return target


def linked_symbols(elf):
    """
    name -> resolved value, from a linked image (used only by --verify-elf).

    A code symbol (nm type T or t) gets bit 0 set: an R_ARM_ABS32 against a
    Thumb function resolves to the Thumb-tagged address, as &function does in C.
    """
    out = {}
    for line in run(NM, elf).splitlines():
        f = line.split()
        if len(f) == 3:
            val = int(f[0], 16)
            if f[1] in ("T", "t"):
                val |= 1
            out[f[2]] = val
    return out


def build(obj, model, list_only=False):
    """Pack one model; return (file bytes, layout), or (None, None) when
    list_only has printed a report instead."""
    sizes = section_sizes(obj)
    w_sec = ".rodata.axon_model_const_%s" % model
    c_sec = ".rodata.cmd_buffer_%s" % model
    d_sec = ".rodata.model_%s" % model             # the descriptor struct
    l_sec = ".data.labels_%s" % model              # optional (ad has none)
    t_sec = ".rodata.str1.1"                       # string pool
    po_sym = "axon_model_%s_packed_output_buf" % model

    for s in (w_sec, c_sec, d_sec):
        if s not in sizes:
            raise SystemExit("axonpack: %s not found in %s" % (s, obj))

    # A relocation target is a part of this model, which the loader places and
    # resolves, or a firmware symbol resolved by name.  The model's sections
    # get the fixed '@' names, so no name in the file embeds the model name.
    canon = {
        sym_name(w_sec): SYM_WEIGHTS,
        sym_name(c_sec): SYM_CMD,
        sym_name(l_sec): SYM_LABELS,
        t_sec: SYM_STRINGS,
        po_sym: SYM_PACKED_OUT,
    }

    def target(tgt):
        n = sym_name(tgt)
        return canon.get(n, canon.get(tgt, n))

    relocs = relocations(obj)
    per_sect = ((SECT_CMD, c_sec), (SECT_DESC, d_sec), (SECT_LABELS, l_sec))

    syms = [SYM_WEIGHTS]                           # index 0 stays the weights
    raw = []                                       # (sect_id, off, sym_name)
    for sid, sec in per_sect:
        for (s, off, tgt) in relocs:
            if s != sec:
                continue
            n = target(tgt)
            if n not in syms:
                syms.append(n)
            raw.append((sid, off, n))
    sites = sorted(((sid, syms.index(n), off) for sid, off, n in raw),
                   key=lambda e: (e[0], e[2]))

    # Every descriptor relocation must name a model section or
    # nrf_axon_interlayer_buffer; for any other target the packer exits,
    # naming it.  Packed as a firmware symbol, such a target could fail to
    # resolve on the device or resolve to the wrong address.
    known = set(canon.values()) | {"nrf_axon_interlayer_buffer"}
    unknown = sorted({n for sid, _, n in raw if sid == SECT_DESC
                      and n not in known})
    if unknown:
        raise SystemExit(
            "axonpack: descriptor of %s relocates against %s, which this packer "
            "does not know how to resolve.  Teach it (see `canon`) rather than "
            "packing a model whose descriptor is only partly described."
            % (model, ", ".join(repr(u) for u in unknown)))

    if list_only:
        print("model            : %s" % model)
        print("weights section  : %-46s %8d B" % (w_sec, sizes[w_sec]))
        print("command buffer   : %-46s %8d B" % (c_sec, sizes[c_sec]))
        print("descriptor       : %-46s %8d B" % (d_sec, sizes[d_sec]))
        print("labels           : %-46s %8d B"
              % (l_sec if l_sec in sizes else "(none)", sizes.get(l_sec, 0)))
        print("string pool      : %-46s %8d B"
              % (t_sec if t_sec in sizes else "(none)", sizes.get(t_sec, 0)))
        names = {SECT_CMD: "cmd", SECT_DESC: "desc", SECT_LABELS: "labels"}
        print("relocation sites : %d, over %d symbols:" % (len(sites), len(syms)))
        for i, s in enumerate(syms):
            n = sum(1 for _, k, _ in sites if k == i)
            kind = "model's own section" if s.startswith("@") else "firmware symbol"
            print("      [%d] %5d  %-38s %s" % (i, n, s, kind))
        for sid, nm in sorted(names.items()):
            n = sum(1 for s, _, _ in sites if s == sid)
            if n:
                print("      in %-8s %5d sites" % (nm, n))
        other = sorted({sec for (sec, _, tgt) in relocs
                        if tgt == w_sec and sec != c_sec and sec != d_sec})
        print("NOT packed (layer-mode sections that also reference weights): %d"
              % len(other))
        return None, None

    weights = section_bytes(obj, w_sec)
    cmd = section_bytes(obj, c_sec)
    desc = section_bytes(obj, d_sec)
    labels = section_bytes(obj, l_sec) if l_sec in sizes else b""
    strings = section_bytes(obj, t_sec) if t_sec in sizes else b""
    if len(weights) != sizes[w_sec] or len(cmd) != sizes[c_sec]:
        raise SystemExit("axonpack: section dump length disagrees with objdump -h")

    # Symbol names as a NUL-separated blob, in site-table index order.
    symtab = b"\0".join(s.encode() for s in syms) + b"\0"

    def align(v, a):
        return v + (-v) % a

    w_off = AXM_HDR_BYTES
    c_off = align(w_off + len(weights), 4)         # command words stay aligned
    d_off = align(c_off + len(cmd), 4)
    l_off = align(d_off + len(desc), 4)
    t_off = align(l_off + len(labels), 4)
    r_off = align(t_off + len(strings), 4)
    s_off = r_off + 8 * len(sites)

    body = bytearray()

    def place(dst_off, blob):
        body.extend(b"\0" * (dst_off - (AXM_HDR_BYTES + len(body))))
        body.extend(blob)

    place(w_off, weights)
    place(c_off, cmd)
    place(d_off, desc)
    place(l_off, labels)
    place(t_off, strings)
    place(r_off, b"".join(struct.pack("<HHI", s, k, o) for s, k, o in sites))
    place(s_off, symtab)

    hdr = bytearray(AXM_HDR_BYTES)
    # The last CRC covers the site table and the symbol names, r_off to the end
    # of the file.  The loader bounds every site to its section, but a corrupt
    # offset that stays in bounds patches the wrong word; this CRC detects it.
    tbl = bytes(body[r_off - AXM_HDR_BYTES:])
    meta = bytes(body[d_off - AXM_HDR_BYTES:r_off - AXM_HDR_BYTES])
    struct.pack_into("<23I", hdr, 0,
                     AXM_MAGIC, AXM_VERSION, AXM_HDR_BYTES,
                     w_off, len(weights),
                     c_off, len(cmd),
                     d_off, len(desc),
                     l_off, len(labels),
                     t_off, len(strings),
                     r_off, len(sites),
                     s_off, len(syms),
                     sizes.get(".bss." + po_sym, 0),
                     len(labels) // 4,
                     zlib.crc32(weights) & 0xFFFFFFFF,
                     zlib.crc32(bytes(cmd)) & 0xFFFFFFFF,
                     zlib.crc32(meta) & 0xFFFFFFFF,
                     zlib.crc32(tbl) & 0xFFFFFFFF)

    return bytes(hdr) + bytes(body), {
        "weights_len": len(weights), "cmd_len": len(cmd),
        "desc_len": len(desc), "labels_len": len(labels),
        "strings_len": len(strings),
        "sites": sites, "syms": syms,
        "w_off": w_off, "c_off": c_off, "d_off": d_off,
        "l_off": l_off, "t_off": t_off, "r_off": r_off, "s_off": s_off,
    }


def image_sections(elf):
    """[(name, vma, bytes)] for the loadable sections verify() reads from."""
    out = []
    for line in run(OBJDUMP, "-h", elf).splitlines():
        f = line.split()
        if len(f) >= 6 and f[0].isdigit() and f[1] in (".rodata", ".data",
                                                       ".text"):
            tmp = "/tmp/axonpack_sec_%s.bin" % f[1].strip(".")
            run(OBJCOPY, "-O", "binary", "--only-section=" + f[1], elf, tmp)
            with open(tmp, "rb") as fh:
                out.append((f[1], int(f[3], 16), fh.read()))
    return out


def linked_bytes(secs, addr, n):
    """The n bytes linked at addr, or None outside the sections read."""
    for _, base, blob in secs:
        if base <= addr and addr - base + n <= len(blob):
            return blob[addr - base: addr - base + n]
    return None


def read_cstr(secs, addr, limit=256):
    """The NUL-terminated string the linker placed at `addr`, or None."""
    for _, base, blob in secs:
        if base <= addr < base + len(blob):
            i = addr - base
            end = blob.find(b"\0", i)
            if 0 <= end - i < limit:
                return blob[i:end]
    return None


# The .kat format: a 48-byte header of 10 little-endian words (magic 'AKT1',
# version, header bytes, vector count, input offset and stride, expected-output
# offset and stride, CRC-32 of the inputs, CRC-32 of the expected outputs),
# then the inputs and the expected outputs, each packed at its stride.
AKT_MAGIC = 0x31544B41          # 'AKT1' little-endian
AKT_VERSION = 1
AKT_HDR_BYTES = 48


def build_kat(obj, model):
    """
    Pack the full-model test vectors into a .kat file; return (bytes, counts).

    The vectors are the vendor harness's known answers, kept out of the .axm.
    Layer-by-layer vectors are not packed.  Vector order is the order of the
    pointer arrays' relocations, not of the symbol names.
    """
    relocs = relocations(obj)
    sizes = section_sizes(obj)

    def ordered(sec):
        return [t for _, t in sorted((off, tgt) for (s, off, tgt) in relocs
                                     if s == sec)]

    ins = ordered(".data.%s_input_test_vectors" % model)
    exps = ordered(".data.%s_expected_output_vectors" % model)
    if not ins:
        raise SystemExit("axonpack: %s has no input test vectors in %s"
                         % (model, obj))
    if len(ins) != len(exps):
        raise SystemExit("axonpack: %s has %d inputs but %d expected outputs"
                         % (model, len(ins), len(exps)))

    # A pointer array's relocations name symbols; each symbol's bytes are in
    # the section gcc gave it under -fdata-sections.  target_section() finds
    # that section, and exits when no section holds the symbol.
    def target_section(tgt):
        if tgt in sizes:
            return tgt
        for pfx in (".rodata.", ".data.", ".bss."):
            if pfx + tgt in sizes:
                return pfx + tgt
        raise SystemExit("axonpack: no section holds %r in %s" % (tgt, obj))

    in_blobs = [section_bytes(obj, target_section(s)) for s in ins]
    exp_blobs = [section_bytes(obj, target_section(s)) for s in exps]

    # The .kat format stores one stride per array: vectors of different sizes
    # exit with an error, and so do empty vectors, whose equal zero lengths
    # would pass the stride check.
    for what, blobs in (("input", in_blobs), ("expected-output", exp_blobs)):
        if len({len(b) for b in blobs}) != 1:
            raise SystemExit(
                "axonpack: %s's %s vectors are not all the same size (%s) -- "
                "the .kat format stores one stride per array"
                % (model, what, sorted({len(b) for b in blobs})))
        if len(blobs[0]) == 0:
            raise SystemExit("axonpack: %s's %s vectors came back EMPTY -- the "
                             "sections holding them were not found"
                             % (model, what))

    in_stride, exp_stride = len(in_blobs[0]), len(exp_blobs[0])
    in_off = AKT_HDR_BYTES
    exp_off = in_off + in_stride * len(ins)
    body = b"".join(in_blobs) + b"".join(exp_blobs)

    hdr = bytearray(AKT_HDR_BYTES)
    struct.pack_into("<10I", hdr, 0,
                     AKT_MAGIC, AKT_VERSION, AKT_HDR_BYTES, len(ins),
                     in_off, in_stride, exp_off, exp_stride,
                     zlib.crc32(b"".join(in_blobs)) & 0xFFFFFFFF,
                     zlib.crc32(b"".join(exp_blobs)) & 0xFFFFFFFF)

    layer = sum(sizes[s] for s in sizes if "layer" in s)
    return bytes(hdr) + body, {
        "nvec": len(ins), "in_stride": in_stride, "exp_stride": exp_stride,
        "layer_excluded": layer,
    }


def verify(axm, meta, obj, model, elf):
    """
    Patch the command buffer, descriptor and labels with the addresses the
    linker chose for elf and compare each with the linked image word for word.
    String-pool sites are compared by text.  Return True when all match.
    """
    linked = linked_symbols(elf)
    secs = image_sections(elf)

    # Where the linker put each patched section.
    sect_sym = {SECT_CMD: "cmd_buffer_%s" % model,
                SECT_DESC: "model_%s" % model,
                SECT_LABELS: "labels_%s" % model}
    sect_span = {SECT_CMD: (meta["c_off"], meta["cmd_len"]),
                 SECT_DESC: (meta["d_off"], meta["desc_len"]),
                 SECT_LABELS: (meta["l_off"], meta["labels_len"])}
    sect_name = {SECT_CMD: "command buffer", SECT_DESC: "descriptor",
                 SECT_LABELS: "labels"}

    # Resolve every symbol the site table names, as the loader does; the loader
    # takes the '@' ones from where it placed the model, this check from the
    # linker.
    at = {SYM_WEIGHTS: "axon_model_const_%s" % model,
          SYM_CMD: "cmd_buffer_%s" % model,
          SYM_LABELS: "labels_%s" % model,
          SYM_PACKED_OUT: "axon_model_%s_packed_output_buf" % model}
    addr = []
    for s in meta["syms"]:
        if s == SYM_STRINGS:
            addr.append(None)             # no symbol: checked by text below
            continue
        nm = at.get(s, s)
        if nm not in linked:
            raise SystemExit("axonpack: reloc symbol %r (%s) not in %s"
                             % (s, nm, elf))
        addr.append(linked[nm])

    # String-pool sites are compared by the text they reach, not by address.
    # '.rodata.str1.1' is a mergeable string section: the linker deduplicates
    # it across translation units and lays it out again, so a reference into
    # the linked pool is not base + addend.  The .axm carries its own copy of
    # the pool, in which base + addend holds.
    strings = axm[meta["t_off"]:meta["t_off"] + meta["strings_len"]]
    si = meta["syms"].index(SYM_STRINGS) if SYM_STRINGS in meta["syms"] else -1

    def packed_str(addend):
        end = strings.find(b"\0", addend)
        return strings[addend:end] if end >= 0 else None

    ok = True
    checked = 0
    text_checked = 0
    for sid in (SECT_CMD, SECT_DESC, SECT_LABELS):
        p_off, n = sect_span[sid]
        if n == 0:
            continue
        if sect_sym[sid] not in linked:
            raise SystemExit("axonpack: %s absent from %s (model not linked in?)"
                             % (sect_sym[sid], elf))
        want = linked_bytes(secs, linked[sect_sym[sid]], n)
        if want is None:
            raise SystemExit("axonpack: the linked %s is not in a section this "
                             "tool reads" % sect_name[sid])
        got = bytearray(axm[p_off:p_off + n])
        skip = set()
        for s, kind, off in meta["sites"]:
            if s != sid:
                continue
            addend = struct.unpack_from("<I", got, off)[0]
            if kind == si:
                # Compare the text the two pointers reach.
                mine = packed_str(addend)
                theirs = read_cstr(secs, struct.unpack_from("<I", want, off)[0])
                if mine is None or mine != theirs:
                    ok = False
                    print("axonpack: VERIFY FAILED -- %s +0x%x points at %r, "
                          "the linked image has %r"
                          % (sect_name[sid], off, mine, theirs), file=sys.stderr)
                else:
                    text_checked += 1
                skip.update(range(off, off + 4))
                continue
            struct.pack_into("<I", got, off, (addr[kind] + addend) & 0xFFFFFFFF)
        checked += 1
        bad = [i for i in range(0, n, 4)
               if i not in skip and got[i:i + 4] != want[i:i + 4]]
        if bad:
            ok = False
            print("axonpack: VERIFY FAILED in the %s -- %d words differ"
                  % (sect_name[sid], len(bad)), file=sys.stderr)
            for i in bad[:8]:
                print("   @0x%05x packed=0x%08x linked=0x%08x" % (
                    i, struct.unpack_from("<I", got, i)[0],
                    struct.unpack_from("<I", want, i)[0]), file=sys.stderr)
    if ok:
        print("axonpack: VERIFY OK -- %d patched sections byte-identical to the "
              "linked image, %d string references identical by text "
              "(%d sites over %d symbols; weights @0x%08x)"
              % (checked, text_checked, len(meta["sites"]), len(meta["syms"]),
                 addr[0]))
    return ok


def main(argv=None):
    """Write the .kat and/or .axm and verify; return the exit status."""
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--obj", required=True, help="compiled object holding the model")
    ap.add_argument("--model", required=True, help="model name, e.g. tinyml_vww")
    ap.add_argument("--out", help="output .axm path")
    ap.add_argument("--verify-elf", help="linked image to check the patch rule against")
    ap.add_argument("--list", action="store_true", help="report and exit")
    ap.add_argument("--kat", help="also write the full-model test vectors here")
    a = ap.parse_args(argv)

    if a.kat:
        kat, km = build_kat(a.obj, a.model)
        with open(a.kat, "wb") as fh:
            fh.write(kat)
        print("axonpack: %s  %d B  (%d vectors, input %d B, expected %d B; "
              "%d B of layer vectors NOT packed)"
              % (a.kat, len(kat), km["nvec"], km["in_stride"],
                 km["exp_stride"], km["layer_excluded"]))
        if not a.out:
            return 0

    blob, meta = build(a.obj, a.model, list_only=a.list)
    if a.list:
        return 0
    if not a.out:
        raise SystemExit("axonpack: --out is required unless --list")

    with open(a.out, "wb") as fh:
        fh.write(blob)
    print("axonpack: %s  %d B  (weights %d + cmd %d + desc %d + labels %d + "
          "strings %d, %d sites)"
          % (a.out, len(blob), meta["weights_len"], meta["cmd_len"],
             meta["desc_len"], meta["labels_len"], meta["strings_len"],
             len(meta["sites"])))
    own = sum(1 for _, k, _ in meta["sites"] if meta["syms"][k].startswith("@"))
    print("axonpack: %d sites resolve the model's own sections, %d resolve "
          "firmware symbols" % (own, len(meta["sites"]) - own))

    if a.verify_elf and not verify(blob, meta, a.obj, a.model, a.verify_elf):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
