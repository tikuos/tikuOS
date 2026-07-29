#!/usr/bin/env python3
"""
axonpack.py - pack a compiled Axon NN model into a store-resident .axm file.

WHY THIS EXISTS.  Nordic's Axon compiler emits a model as C arrays: the weights
(`axon_model_const_<name>`, 208 KB for tinyml_vww) plus command buffers that the
NPU executes.  Being `const`, they land in .rodata -- i.e. inside the code
window -- which is the sole reason the Nordic code budget is 800 KB while real
firmware is ~130 KB.  Moving them out is blocked by the fact that the command
buffers contain ABSOLUTE ADDRESSES of individual weight members, baked at link
time (proven: 247 R_ARM_ABS32 relocations against the weights section, none in
.text).

The vendor offers no indirection -- the descriptor's model_const_ptr /
model_const_size fields exist but are read nowhere in the SDK -- so the weights
cannot simply be repointed.  They CAN be relocated, though, because the
toolchain already records every site: this tool reads those relocations out of
the compiled object and ships them alongside the weights, so the loader can
patch the command buffer for wherever the store put the weights.

ARM uses REL, not RELA: the addend is stored IN PLACE at the relocation site.
So a site's word in the object file IS its offset within the weights blob, and
patching is exactly:

    site_word = weights_runtime_address + stored_addend

which is why the .axm carries the command buffer UNRELOCATED -- the addends are
already in it, and the loader only adds a base.

EVERY site must be patched, not just the weight ones.  A first cut of this tool
assumed the command buffer's EXTERNAL references -- 172 sites pointing at the
firmware's nrf_axon_interlayer_buffer RAM symbol, one at a driver function, one
at driver data -- could keep "the address the firmware linked".  The
reconstruction gate below disproved that in one run: the bytes come from the
OBJECT, where those sites still hold bare addends too, and once the buffer lives
in a file rather than being linked, nothing applies their relocations either.
So the .axm carries a TYPED relocation table (site -> symbol index) plus the
symbol names, and the loader resolves each name against the firmware.  Only
three external symbols are involved, and naming them in the file means a
mismatch is detected rather than silently mispatched.

WHAT v3 ADDED, AND WHY.  v2 packed the weights and the command buffer, which is
enough only while the firmware still has a BAKED model to borrow a descriptor
from -- the struct that says how big the input is, how it is quantized, how much
interlayer buffer the model needs.  A model-free image has no such donor, so a
model that carries only weights and code cannot be run at all.

v3 therefore packs the DESCRIPTOR too, along with the two things it points at
that are not already in the file (its label strings and the string pool).  Those
are relocatable in exactly the same sense the command buffer is -- the
descriptor's inputs[].ptr and output_ptr are link-time addresses inside
nrf_axon_interlayer_buffer, its labels[] entries are addresses inside the string
pool -- so rather than inventing a second mechanism, the site table gained a
SECTION field and the same (site -> symbol) machinery now patches all three.

Symbols whose names begin with '@' are the model's OWN sections, resolved by the
loader to wherever it mapped or copied them (@weights, @cmd, @desc, @labels,
@strings) or to a buffer the caller supplies (@packed_out).  Everything else is
a firmware symbol resolved by name.  This is what lets one file describe a model
completely, with no part of it left implicit in the firmware that loads it.

SCOPE OF THIS INCREMENT.  The full-model path is packed: weights, command
buffer, descriptor, labels.  Layer-by-layer KAT buffers are not (see --list);
the full-model path is what a product runs.  The KAT's test VECTORS are packed
separately by --kat, because they are test data rather than part of the model.

    python3 tools/axonpack.py --obj <compiled .o> --model tinyml_vww \\
            --out temp/vww.axm [--verify-elf main.elf]

SPDX-License-Identifier: Apache-2.0
"""

import argparse
import re
import struct
import subprocess
import sys
import zlib

AXM_MAGIC = 0x314D5841          # 'AXM1' little-endian
AXM_VERSION = 3                 # v3 adds the descriptor, labels and strings
AXM_HDR_BYTES = 96              # 23 header words, and a multiple of 16 so the
                                # weights stay 16-byte aligned within the file

# Section ids used by the site table's `sect` field.  Only sections that need
# PATCHING appear here: the weights and the string pool are used as mapped.
SECT_CMD, SECT_DESC, SECT_LABELS = 0, 1, 2

# Relocation targets that name a part of the model itself rather than a firmware
# symbol.  The loader resolves these; they never reach the symbol registry.
SYM_WEIGHTS = "@weights"
SYM_CMD = "@cmd"
SYM_LABELS = "@labels"
SYM_STRINGS = "@strings"
SYM_PACKED_OUT = "@packed_out"

OBJDUMP = "arm-none-eabi-objdump"
OBJCOPY = "arm-none-eabi-objcopy"
NM = "arm-none-eabi-nm"


def run(*cmd):
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
    """Raw contents of one section (objcopy is the only reliable dumper here)."""
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
    objdump names a relocation target either by symbol or by section.  A section
    target like '.rodata.axon_model_const_tinyml_vww' carries its symbol as the
    last dotted component; a plain symbol like 'nrf_axon_interlayer_buffer' is
    already the wanted value.
    """
    for pfx in (".rodata.", ".data.", ".bss.", ".text."):
        if target.startswith(pfx):
            return target[len(pfx):]
    return target


def linked_symbols(elf):
    """
    name -> resolved value, from a linked image (used only by --verify-elf).

    THUMB BIT.  For a code symbol (nm type T/t) an R_ARM_ABS32 resolves with bit
    0 SET, because that is how a Thumb function pointer is represented.  The
    reconstruction gate caught exactly this as a one-word mismatch on the
    softmax-extension site.  The C loader gets it right for free -- taking
    &function in C already yields the Thumb-tagged value -- so this only models
    what the linker did.
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

    # A relocation target is either a part of THIS model -- which the loader
    # resolves, because only it knows where the model ended up -- or a firmware
    # symbol resolved by name.  Canonicalising here (rather than in the loader)
    # keeps the model-specific names, which embed the model name, out of the
    # file format.
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

    # THE DESCRIPTOR'S RELOCATIONS ARE CHECKED, NOT ASSUMED.  Every one of them
    # must name something this packer knows how to make resolvable; an unknown
    # target would be packed as a firmware symbol and then fail to resolve on
    # the device -- or worse, resolve to the wrong thing.  Refusing here turns a
    # future model with an unanticipated field into a build error with a name in
    # it, rather than a silent misload.
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

    # Symbol names as a NUL-separated blob; index order matches the reloc codes.
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
    # The last CRC covers the relocation TABLE and the symbol names -- everything
    # from r_off to the end of the file.  v1 checksummed only the weights and
    # the command buffer, leaving the one part of the file that says "write four
    # bytes at offset N" unprotected.  The loader bounds every entry, so a
    # corrupt table cannot escape its section; but an in-bounds corrupt OFFSET
    # patches the wrong word, and the symptom is wrong inference rather than a
    # fault.  Only a checksum catches that.
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
    """The n bytes the linker placed at `addr`, or None if not in a read section."""
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


AKT_MAGIC = 0x31544B41          # 'AKT1' little-endian
AKT_VERSION = 1
AKT_HDR_BYTES = 48


def build_kat(obj, model):
    """
    Pack the FULL-MODEL test vectors into a .kat file.

    Separate from the .axm on purpose: these are the vendor harness's known
    answers, not part of the model, and a product ships the model without them.
    Keeping them apart is also what makes the numbers honest -- tinyml_vww's
    vectors are 315 KB in the image, of which 232 KB is LAYER data that the
    store path does not run and therefore does not pack.

    Order comes from the pointer arrays' own relocations, not from the symbol
    names: the array says which vector is vector 0, and sorting names would
    quietly reorder a model with ten or more of them.
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

    # A pointer array's relocations name SYMBOLS; the bytes live in the section
    # gcc gave each one under -fdata-sections.  Resolving that here rather than
    # assuming a prefix is what stops a silently empty pack: the first cut fed
    # the bare symbol name to objcopy, got nothing back for every vector, and
    # wrote a 48-byte file whose header cheerfully said "3 vectors".
    def target_section(tgt):
        if tgt in sizes:
            return tgt
        for pfx in (".rodata.", ".data.", ".bss."):
            if pfx + tgt in sizes:
                return pfx + tgt
        raise SystemExit("axonpack: no section holds %r in %s" % (tgt, obj))

    in_blobs = [section_bytes(obj, target_section(s)) for s in ins]
    exp_blobs = [section_bytes(obj, target_section(s)) for s in exps]

    # A single stride per array keeps the file and the reader trivial, so the
    # assumption is CHECKED rather than assumed: a model with ragged vectors is
    # refused here instead of being silently mis-sliced on the device.  Empty is
    # rejected separately -- a set of all-zero lengths is "consistent" and would
    # otherwise sail through the stride check, which is exactly how the empty
    # pack above got a clean bill of health.
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
    THE GATE.  Apply the loader's exact patch rule using the addresses the real
    linker chose, then require every patched section to equal the linked image
    byte for byte.  If that holds, the extracted site list is complete (nothing
    missed) and sound (nothing spurious) -- proven against the toolchain's own
    output rather than argued.

    v3 checks all three patched sections, not just the command buffer.  The
    descriptor is the one that most needed it: it is 132 bytes with seven
    pointer fields, and a single wrong one means the engine reads its input from
    the wrong place and reports a confident wrong answer.
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

    # Resolve every symbol the reloc table names -- exactly what the loader does,
    # except the loader gets the '@' ones from the store instead of the linker.
    at = {SYM_WEIGHTS: "axon_model_const_%s" % model,
          SYM_CMD: "cmd_buffer_%s" % model,
          SYM_LABELS: "labels_%s" % model,
          SYM_PACKED_OUT: "axon_model_%s_packed_output_buf" % model}
    addr = []
    for s in meta["syms"]:
        if s == SYM_STRINGS:
            addr.append(None)             # derived below -- it has no symbol
            continue
        nm = at.get(s, s)
        if nm not in linked:
            raise SystemExit("axonpack: reloc symbol %r (%s) not in %s"
                             % (s, nm, elf))
        addr.append(linked[nm])

    # THE STRING POOL IS CHECKED BY CONTENT, NOT BY ADDRESS, and it has to be.
    # '.rodata.str1.1' is a MERGEABLE string section: the linker deduplicates it
    # across every translation unit and re-lays it out, so a reference into it
    # does NOT survive as base+addend the way a reference into an ordinary
    # section does.  Measured here: the label pointers came out a constant 0x61
    # from the linked values on both kws and ic, because the model-name string
    # and the label strings ended up different distances apart after merging.
    #
    # That is a fact about the LINKED IMAGE, not a defect in the packed file --
    # the .axm carries its own copy of the pool, so base+addend is exactly right
    # inside it.  What must be proven is therefore not "the pointer word
    # matches" but "the pointer reaches the same TEXT", which is also the
    # property anyone actually depends on.  So string sites are excluded from
    # the word-for-word comparison and checked as strings instead.
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
                # Compare the TEXT the two pointers reach.
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
