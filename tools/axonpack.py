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

SCOPE OF THIS INCREMENT.  Only the weights and the full-model command buffer
leave the image.  Layer-by-layer KAT buffers are not packed (see --list); the
full-model path is what a product runs.

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
AXM_VERSION = 1
AXM_HDR_BYTES = 64              # keeps the weights 16-byte aligned in the file

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
    already what we want.
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
    for s in (w_sec, c_sec):
        if s not in sizes:
            raise SystemExit("axonpack: %s not found in %s" % (s, obj))

    relocs = relocations(obj)
    # EVERY relocation inside the full-model command buffer, typed by target.
    # Symbol index 0 is reserved for the weights, so the loader resolves it to
    # wherever the store mapped them; the rest are firmware symbols by name.
    in_cmd = [(off, tgt) for (sec, off, tgt) in relocs if sec == c_sec]
    syms = [sym_name(w_sec)]
    for _, tgt in in_cmd:
        n = sym_name(tgt)
        if n not in syms:
            syms.append(n)
    sites = sorted(((off, syms.index(sym_name(tgt))) for off, tgt in in_cmd),
                   key=lambda e: e[0])

    if list_only:
        print("model            : %s" % model)
        print("weights section  : %-46s %8d B" % (w_sec, sizes[w_sec]))
        print("command buffer   : %-46s %8d B" % (c_sec, sizes[c_sec]))
        print("relocation sites : %d, over %d symbols:" % (len(sites), len(syms)))
        for i, s in enumerate(syms):
            n = sum(1 for _, k in sites if k == i)
            kind = "WEIGHTS (from the store)" if i == 0 else "firmware symbol"
            print("      [%d] %5d  %-38s %s" % (i, n, s, kind))
        other = sorted({sec for (sec, _, tgt) in relocs
                        if tgt == w_sec and sec != c_sec})
        print("NOT packed (layer-mode sections that also reference weights): %d"
              % len(other))
        return None, None

    weights = section_bytes(obj, w_sec)
    cmd = section_bytes(obj, c_sec)
    if len(weights) != sizes[w_sec] or len(cmd) != sizes[c_sec]:
        raise SystemExit("axonpack: section dump length disagrees with objdump -h")

    # Symbol names as a NUL-separated blob; index order matches the reloc codes.
    symtab = b"\0".join(s.encode() for s in syms) + b"\0"

    w_off = AXM_HDR_BYTES
    c_off = w_off + len(weights)
    c_off += (-c_off) % 4                       # keep the command words aligned
    r_off = c_off + len(cmd)
    r_off += (-r_off) % 4
    s_off = r_off + 8 * len(sites)

    body = bytearray()

    def place(dst_off, blob):
        body.extend(b"\0" * (dst_off - (AXM_HDR_BYTES + len(body))))
        body.extend(blob)

    place(w_off, weights)
    place(c_off, cmd)
    place(r_off, b"".join(struct.pack("<II", o, k) for o, k in sites))
    place(s_off, symtab)

    hdr = bytearray(AXM_HDR_BYTES)
    struct.pack_into("<13I", hdr, 0,
                     AXM_MAGIC, AXM_VERSION, AXM_HDR_BYTES,
                     w_off, len(weights),
                     c_off, len(cmd),
                     r_off, len(sites),
                     s_off, len(syms),
                     zlib.crc32(weights) & 0xFFFFFFFF,
                     zlib.crc32(bytes(cmd)) & 0xFFFFFFFF)

    return bytes(hdr) + bytes(body), {
        "weights_len": len(weights), "cmd_len": len(cmd),
        "sites": sites, "syms": syms,
        "w_off": w_off, "c_off": c_off, "r_off": r_off, "s_off": s_off,
    }


def verify(axm, meta, obj, model, elf):
    """
    THE GATE.  Apply the loader's exact patch rule using the address the real
    linker gave the weights, then require the result to equal the linked image's
    command buffer byte for byte.  If that holds, the extracted site list is
    complete (nothing missed) and sound (nothing spurious) -- proven against the
    toolchain's own output rather than argued.
    """
    linked = linked_symbols(elf)
    c_sym = "cmd_buffer_%s" % model
    if c_sym not in linked:
        raise SystemExit("axonpack: %s absent from %s (model not linked in?)"
                         % (c_sym, elf))
    c_addr = linked[c_sym]

    # Resolve every symbol the reloc table names -- exactly what the loader does,
    # except the loader gets symbol 0 from the store instead of the linker.
    addr = []
    for s in meta["syms"]:
        if s not in linked:
            raise SystemExit("axonpack: reloc symbol %r not in %s" % (s, elf))
        addr.append(linked[s])

    cmd = bytearray(axm[meta["c_off"]:meta["c_off"] + meta["cmd_len"]])
    for off, kind in meta["sites"]:
        addend = struct.unpack_from("<I", cmd, off)[0]
        struct.pack_into("<I", cmd, off, (addr[kind] + addend) & 0xFFFFFFFF)

    # Pull the same range out of the linked image.
    tmp = "/tmp/axonpack_ro.bin"
    run(OBJCOPY, "-O", "binary", "--only-section=.rodata", elf, tmp)
    ro = open(tmp, "rb").read()
    ro_base = None
    for line in run(OBJDUMP, "-h", elf).splitlines():
        f = line.split()
        if len(f) >= 6 and f[0].isdigit() and f[1] == ".rodata":
            ro_base = int(f[3], 16)
    if ro_base is None:
        raise SystemExit("axonpack: no .rodata in %s" % elf)
    want = ro[c_addr - ro_base: c_addr - ro_base + meta["cmd_len"]]

    if bytes(cmd) != want:
        bad = [i for i in range(0, len(want), 4)
               if cmd[i:i + 4] != want[i:i + 4]]
        print("axonpack: VERIFY FAILED -- %d words differ" % len(bad),
              file=sys.stderr)
        for i in bad[:8]:
            print("   @0x%05x packed=0x%08x linked=0x%08x" % (
                i, struct.unpack_from("<I", cmd, i)[0],
                struct.unpack_from("<I", want, i)[0]), file=sys.stderr)
        return False
    print("axonpack: VERIFY OK -- patched command buffer is byte-identical to "
          "the linked image (%d sites over %d symbols; weights @0x%08x)"
          % (len(meta["sites"]), len(meta["syms"]), addr[0]))
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--obj", required=True, help="compiled object holding the model")
    ap.add_argument("--model", required=True, help="model name, e.g. tinyml_vww")
    ap.add_argument("--out", help="output .axm path")
    ap.add_argument("--verify-elf", help="linked image to check the patch rule against")
    ap.add_argument("--list", action="store_true", help="report and exit")
    a = ap.parse_args(argv)

    blob, meta = build(a.obj, a.model, list_only=a.list)
    if a.list:
        return 0
    if not a.out:
        raise SystemExit("axonpack: --out is required unless --list")

    with open(a.out, "wb") as fh:
        fh.write(blob)
    print("axonpack: %s  %d B  (weights %d + cmd %d + %d sites)"
          % (a.out, len(blob), meta["weights_len"], meta["cmd_len"],
             len(meta["sites"])))
    print("axonpack: %d sites resolve the weights, %d resolve firmware symbols"
          % (sum(1 for _, k in meta["sites"] if k == 0),
             sum(1 for _, k in meta["sites"] if k != 0)))

    if a.verify_elf and not verify(blob, meta, a.obj, a.model, a.verify_elf):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
