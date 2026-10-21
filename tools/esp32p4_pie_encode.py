#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32p4_pie_encode.py - derive the ESP32-P4 PIE instruction encodings
# from Espressif's assembler into a macro file and a JSON table.
#
# SPDX-License-Identifier: Apache-2.0
"""Derive the ESP32-P4 PIE instruction encodings from Espressif's assembler.

The PIE (Espressif's vector extension, instruction-set version xespv2p1 on
v1.x silicon) is not known to upstream binutils, so the xPack toolchain
that builds TikuOS cannot assemble its mnemonics.  This script runs once,
offline, with Espressif's riscv32-esp-elf assembler as the oracle: for
every mnemonic of the inventory it assembles operand permutations, solves
each operand's field from the encodings and writes

  arch/esp32p4/pie/tiku_pie.inc     assembler macros (one a mnemonic, dots
                                    as underscores) that expand to .word
  tools/esp32p4_pie_table.json      the base words, the fields and oracle
                                    samples, which the host test re-encodes

Usage:
  tools/esp32p4_pie_encode.py --as <riscv32-esp-elf-as-xespv2p1> \
      --objdump <riscv32-esp-elf-objdump-xespv2p1> --inventory <xesppie.S>

The inventory is ESP-IDF's esp_gdbstub rv_decode/xesppie.S: one
"mnemonic operands" line an instruction, with example operands (q0, x8,
an immediate).  A field is taken to be linear: each value bit sets its own
word bits; every sample the oracle gives is checked against that.
"""
import argparse
import json
import os
import random
import re
import subprocess
import sys
import tempfile

MARCH = "rv32imafc_zicsr_zifencei_xespv2p1"
XREGS = ["zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0",
         "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5",
         "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"]


class Oracle:
    """Assemble lines with Espressif's assembler and return their words."""

    def __init__(self, asm, objdump):
        self.asm, self.objdump = asm, objdump
        self.runs = 0

    def words(self, lines):
        """The 32-bit word of each line, or None where it does not assemble.

        Lines are tried together first; on a failure each is tried alone."""
        self.runs += 1
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "p.s")
            obj = os.path.join(tmp, "p.o")
            with open(src, "w") as f:
                f.write("".join(l + "\n" for l in lines))
            r = subprocess.run([self.asm, "-march=" + MARCH, src, "-o", obj],
                               capture_output=True)
            if r.returncode != 0:
                if len(lines) == 1:
                    return [None]
                return [w for l in lines for w in self.words([l])]
            out = subprocess.run([self.objdump, "-d", obj], capture_output=True,
                                 text=True).stdout
            found = re.findall(r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s", out, re.M)
            if len(found) != len(lines):
                raise RuntimeError("objdump gave %d words for %d lines" %
                                   (len(found), len(lines)))
            return [int(w, 16) for w in found]


def parse_inventory(path):
    """[(mnemonic, [operand examples])] from the inventory file."""
    insns = []
    for line in open(path):
        line = line.strip()
        if not line.startswith("esp."):
            continue
        parts = line.split(None, 1)
        ops = [o.strip() for o in parts[1].split(",")] if len(parts) > 1 else []
        insns.append((parts[0], ops))
    return insns


def classify(example):
    if re.fullmatch(r"q[0-7]", example):
        return "q"
    if re.fullmatch(r"x\d+", example):
        return "x"
    if re.fullmatch(r"-?\d+", example):
        return "imm"
    raise ValueError("operand %r" % example)


def render(mnemonic, kinds, values):
    """The assembler line for @p values (ints) of the operand @p kinds."""
    texts = []
    for kind, v in zip(kinds, values):
        texts.append({"q": "q%d", "x": "x%d", "imm": "%d"}[kind] % v)
    return mnemonic + (" " + ",".join(texts) if texts else "")


def bits_of(diff):
    return [b for b in range(32) if diff >> b & 1]


def probe(oracle, mnemonic, kinds, refs, i, candidates):
    """{value: word} for operand @p i over @p candidates, others at refs."""
    lines = []
    for v in candidates:
        vals = list(refs)
        vals[i] = v
        lines.append(render(mnemonic, kinds, vals))
    return {v: w for v, w in zip(candidates, oracle.words(lines))
            if w is not None}


def solve_field(words, nbits, name):
    """Word bits of each of the @p nbits value bits, from pairs of values
    in @p words that differ in that bit alone."""
    mapping = []
    for k in range(nbits):
        pair = next(((a, a ^ (1 << k)) for a in words if (a ^ (1 << k)) in words),
                    None)
        if pair is None:
            raise RuntimeError("%s: no pair for value bit %d" % (name, k))
        mapping.append(bits_of(words[pair[0]] ^ words[pair[1]]))
    return mapping


def field_bits(mapping, v):
    word = 0
    for k, wordbits in enumerate(mapping):
        if v >> k & 1:
            for b in wordbits:
                word |= 1 << b
    return word


def solve_register(oracle, mnemonic, kinds, refs, i, count):
    """A register operand's field: the accepted registers form an aligned
    range (x0-x31, or x8-x15 for a 3-bit field) whose offset is taken off
    before the bits are mapped."""
    words = probe(oracle, mnemonic, kinds, refs, i, list(range(count)))
    if not words:
        raise RuntimeError(mnemonic + ": operand %d accepts nothing" % i)
    name = "%s operand %d" % (mnemonic, i)
    allowed = sorted(words)
    form = register_form(allowed)
    if form is None:
        raise RuntimeError(name + ": accepts %s" % allowed)
    n = len(allowed).bit_length() - 1
    relative = {allowed.index(v): w for v, w in words.items()}
    mapping = solve_field(relative, n, name)
    base = relative[0]
    for v, w in relative.items():
        if w != base | field_bits(mapping, v):
            raise RuntimeError(name + ": value %d is not linear" % allowed[v])
    return {"kind": kinds[i], "bits": mapping, "form": form,
            "allowed": allowed}, allowed[0]


def register_form(allowed):
    """How a register number becomes its field value: "range" for an
    aligned range (value - first), "split16" for x8-x15 and x24-x31
    ((value & 7) | ((value >> 4) << 3)), None for anything else."""
    n = len(allowed).bit_length() - 1
    if len(allowed) != 1 << n:
        return None
    if allowed == list(range(allowed[0], allowed[0] + (1 << n))) and \
            allowed[0] % (1 << n) == 0:
        return "range"
    if allowed == list(range(8, 16)) + list(range(24, 32)):
        return "split16"
    return None


def register_index(field, v):
    return field["allowed"].index(v)


def solve_immediate(oracle, mnemonic, kinds, refs, i):
    name = "%s immediate %d" % (mnemonic, i)

    def accepts(v):
        return probe(oracle, mnemonic, kinds, refs, i, [v]).get(v)

    scale = next((1 << k for k in range(13) if accepts(1 << k) is not None), None)
    if scale is None:
        if accepts(0) is None:
            raise RuntimeError(name + ": accepts neither 0 nor a power of two")
        return {"kind": "imm", "scale": 0, "min": 0, "max": 0, "width": 0,
                "bits": []}, 0
    hi = scale
    while accepts(hi * 2) is not None:
        hi *= 2
    step = hi
    while step > scale:
        step //= 2
        if accepts(hi + step) is not None:
            hi += step
    lo = 0
    if accepts(-scale) is not None:
        lo = -scale
        while accepts(lo * 2) is not None:
            lo *= 2
        step = -lo
        while step > scale:
            step //= 2
            if accepts(lo - step) is not None:
                lo -= step
    width = (hi // scale).bit_length()
    if lo < 0:
        width = max(width + 1, ((-lo) // scale - 1).bit_length() + 1)
    mask = (1 << width) - 1
    candidates = sorted({0, hi, lo} | {(1 << k) * scale for k in range(width)
                                       if (1 << k) * scale <= hi} |
                        {lo + (1 << k) * scale for k in range(width)
                         if lo + (1 << k) * scale <= hi})
    words = {(v // scale) & mask: w
             for v, w in probe(oracle, mnemonic, kinds, refs, i, candidates).items()}
    mapping = solve_field(words, width, name)
    ref = 0 if 0 in words else min(words)
    for v, w in words.items():
        if w != (words[ref] ^ field_bits(mapping, ref)) | field_bits(mapping, v):
            raise RuntimeError(name + ": field value %d is not linear" % v)
    return {"kind": "imm", "scale": scale, "min": lo, "max": hi,
            "width": width, "bits": mapping}, (0 if lo <= 0 <= hi else lo)


def encode(entry, values):
    """The word @p entry gives for operand @p values, as the macro does."""
    word = entry["base"]
    for field, v in zip(entry["operands"], values):
        if field["kind"] == "imm":
            if field["scale"] == 0:
                continue
            v = (v // field["scale"]) & ((1 << field["width"]) - 1)
        else:
            v = register_index(field, v)
        word |= field_bits(field["bits"], v)
    return word


def macro_name(mnemonic):
    return mnemonic.replace(".", "_")


def emit_inc(table, path):
    lines = [
        "/*",
        " * Tiku Operating System v0.06",
        " * Authors: Ambuj Varshney <ambuj@tiku-os.org>",
        " * tiku_pie.inc - ESP32-P4 PIE instructions (xespv2p1) as assembler",
        " * macros that expand to .word, generated by tools/esp32p4_pie_encode.py",
        " * from Espressif's assembler; the xPack assembler needs nothing else.",
        " * SPDX-License-Identifier: Apache-2.0",
        " */",
        "",
        "/* Register names to numbers for the macros below. */",
        ".macro pie_xnum sym, r",
    ]
    for n, name in enumerate(XREGS):
        lines.append("    .ifc \\r, %s\n        .set \\sym, %d\n    .endif" % (name, n))
        lines.append("    .ifc \\r, x%d\n        .set \\sym, %d\n    .endif" % (n, n))
    lines.append("    .ifc \\r, fp\n        .set \\sym, 8\n    .endif")
    lines.append(".endm")
    lines.append(".macro pie_qnum sym, r")
    for n in range(8):
        lines.append("    .ifc \\r, q%d\n        .set \\sym, %d\n    .endif" % (n, n))
    lines.append(".endm")
    lines.append("")
    for mnemonic in sorted(table):
        entry = table[mnemonic]
        args = ["o%d" % i for i in range(len(entry["operands"]))]
        lines.append("/* %s %s */" % (mnemonic, ",".join(
            {"q": "q", "x": "rs", "imm": "imm"}[f["kind"]] + str(i)
            for i, f in enumerate(entry["operands"]))))
        lines.append(".macro %s %s" % (macro_name(mnemonic), ", ".join(args)))
        terms = ["0x%08x" % entry["base"]]
        for i, field in enumerate(entry["operands"]):
            sym = "_pie_v%d" % i
            if field["kind"] in ("q", "x"):
                lines.append("    pie_%snum %s, \\o%d" % (field["kind"], sym, i))
                lo, hi = field["allowed"][0], field["allowed"][-1]
                lines.append("    .if (%s < %d) || (%s > %d)" % (sym, lo, sym, hi))
                lines.append("        .error \"%s: operand %d takes %s%d-%s%d\""
                             % (mnemonic, i, field["kind"], lo, field["kind"], hi))
                lines.append("    .endif")
                if field["form"] == "range" and lo:
                    lines.append("    .set %s, %s - %d" % (sym, sym, lo))
                elif field["form"] == "split16":
                    lines.append("    .if (%s > 15) && (%s < 24)" % (sym, sym))
                    lines.append("        .error \"%s: operand %d takes x8-x15 or x24-x31\"" % (mnemonic, i))
                    lines.append("    .endif")
                    lines.append("    .set %s, (%s & 7) | ((%s >> 4) << 3)" % (sym, sym, sym))
            else:
                if field["scale"] == 0:
                    continue
                lines.append("    .set %s, ((\\o%d) / %d) & 0x%x" %
                             (sym, i, field["scale"], (1 << field["width"]) - 1))
            for k, wordbits in enumerate(field["bits"]):
                for b in wordbits:
                    terms.append("(((%s >> %d) & 1) << %d)" % (sym, k, b))
        lines.append("    .word " + " | ".join(terms))
        lines.append(".endm")
        lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


def write_table(table, skipped, path):
    """Write the JSON table, one instruction a line, keys sorted."""
    with open(path, "w") as f:
        f.write('{"march": %s,\n "instructions": {\n' % json.dumps(MARCH))
        names = sorted(table)
        for i, name in enumerate(names):
            f.write("  %s: %s%s\n" % (json.dumps(name),
                                      json.dumps(table[name], sort_keys=True,
                                                 separators=(",", ":")),
                                      "," if i + 1 < len(names) else ""))
        f.write(' },\n "skipped": %s}\n' % json.dumps(skipped, indent=1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--as", dest="asm", required=True)
    ap.add_argument("--objdump", required=True)
    ap.add_argument("--inventory", required=True)
    ap.add_argument("--inc", default="arch/esp32p4/pie/tiku_pie.inc")
    ap.add_argument("--json", default="tools/esp32p4_pie_table.json")
    ap.add_argument("--only", help="comma-separated mnemonics (debug)")
    args = ap.parse_args()
    oracle = Oracle(args.asm, args.objdump)
    rng = random.Random(1)
    table = {}
    skipped = []
    for mnemonic, examples in parse_inventory(args.inventory):
        if args.only and mnemonic not in args.only.split(","):
            continue
        if mnemonic.startswith("esp.lp."):
            skipped.append(mnemonic + ": hardware loops take labels, not probed")
            continue
        kinds = [classify(e) for e in examples]
        refs = [0 if k == "q" else int(e[1:]) if k == "x" else int(e)
                for k, e in zip(kinds, examples)]
        try:
            fields = []
            for i, kind in enumerate(kinds):
                if kind == "q":
                    field, ref = solve_register(oracle, mnemonic, kinds, refs, i, 8)
                elif kind == "x":
                    field, ref = solve_register(oracle, mnemonic, kinds, refs, i, 32)
                else:
                    field, ref = solve_immediate(oracle, mnemonic, kinds, refs, i)
                fields.append(field)
                refs[i] = ref
            word = oracle.words([render(mnemonic, kinds, refs)])[0]
            entry = {"base": word, "operands": fields, "samples": []}
            entry["base"] = word ^ (encode(entry, refs) ^ entry["base"])
            # Random samples: the oracle's word against the derived encoding.
            lines, values = [], []
            for _ in range(8):
                vals = []
                for field in fields:
                    if field["kind"] in ("q", "x"):
                        vals.append(rng.choice(field["allowed"]))
                    elif field["scale"] == 0:
                        vals.append(0)
                    else:
                        vals.append(rng.randrange(field["min"] // field["scale"],
                                                  field["max"] // field["scale"] + 1)
                                    * field["scale"])
                values.append(vals)
                lines.append(render(mnemonic, kinds, vals))
            for vals, w in zip(values, oracle.words(lines)):
                if w is None:
                    continue
                if encode(entry, vals) != w:
                    raise RuntimeError("%s %s: derived %08x, oracle %08x" %
                                       (mnemonic, vals, encode(entry, vals), w))
                entry["samples"].append([vals, w])
            table[mnemonic] = entry
            print("%-40s base %08x %s" % (mnemonic, entry["base"], " ".join(
                f["kind"] + "%d" % len(f["bits"]) for f in fields)))
        except RuntimeError as e:
            skipped.append(str(e))
            print("skip " + str(e), file=sys.stderr)
    os.makedirs(os.path.dirname(args.inc) or ".", exist_ok=True)
    emit_inc(table, args.inc)
    write_table(table, skipped, args.json)
    print("%d instructions, %d skipped, %d assembler runs -> %s, %s" %
          (len(table), len(skipped), oracle.runs, args.inc, args.json))


if __name__ == "__main__":
    main()
