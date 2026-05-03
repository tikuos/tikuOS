#!/usr/bin/env python3
"""bas_to_c.py -- convert a Tiku BASIC source file into a C string literal.

Usage:
    python3 tools/bas_to_c.py <input.bas> <output.c>

The generated C file looks like:

    /* Generated from blink.bas -- do not edit. */
    #include <stddef.h>
    const char tiku_basic_embedded_src[] =
        "10 PIN 4, 6, 1\n"
        "20 DIGWRITE 4, 6, 2\n"
        "30 DELAY 250\n"
        "40 GOTO 20\n";
    const size_t tiku_basic_embedded_len =
        sizeof(tiku_basic_embedded_src) - 1u;

The generated symbol is referenced from main.c when TIKU_BASIC_EMBEDDED
is set. Re-running the script is safe; the file is fully overwritten.
"""

import sys
import os


def escape_for_c_string(s: str) -> str:
    """Escape a single line for inclusion in a C string literal.

    We escape backslash, double-quote, and the printable controls
    that crop up in BASIC source (tab). Other characters pass through
    -- byte-clean for ASCII / UTF-8.
    """
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20:
            out.append(f"\\x{ord(ch):02x}")
        else:
            out.append(ch)
    return "".join(out)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: bas_to_c.py <input.bas> <output.c>", file=sys.stderr)
        return 1

    src_path, dst_path = sys.argv[1], sys.argv[2]
    if not os.path.isfile(src_path):
        print(f"error: not a file: {src_path}", file=sys.stderr)
        return 1

    with open(src_path, "r", encoding="utf-8") as f:
        text = f.read()

    # Normalise CRLF to LF; strip a trailing blank-only line so the
    # output doesn't carry a stray empty line at the end.
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    lines = text.split("\n")
    while lines and not lines[-1].strip():
        lines.pop()

    src_basename = os.path.basename(src_path)

    os.makedirs(os.path.dirname(os.path.abspath(dst_path)), exist_ok=True)
    with open(dst_path, "w", encoding="utf-8") as f:
        f.write(f"/* Generated from {src_basename} -- do not edit. */\n")
        f.write("#include <stddef.h>\n\n")
        f.write("const char tiku_basic_embedded_src[] =\n")
        if not lines:
            # Empty source: emit a single empty literal so the symbol exists.
            f.write('    "";\n')
        else:
            for line in lines:
                f.write(f'    "{escape_for_c_string(line)}\\n"\n')
            f.write(";\n")
        f.write("\n")
        f.write("const size_t tiku_basic_embedded_len =\n")
        f.write("    sizeof(tiku_basic_embedded_src) - 1u;\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
