#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# check_comment_style.py - keep source comments short and free of process log.
#
# A file header is licence, author and 2-3 lines of what the file is; a doc
# comment is 2-3 lines of prose plus as many @tags as it needs. Design history
# belongs in git, which already has it in full.
#
# SPDX-License-Identifier: Apache-2.0

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Vendor headers and separate repos are not ours to reformat.
SKIP_DIRS = {"build", "temp", "drivers", "TikuBench", "tikukits", ".git", "hardware"}
SKIP_PATHS = ("arch/ambiq/cmsis", "arch/nordic/mdk", "tools/fat32")

HEADER_MAX = 15   # boilerplate 7 + filename + blank + 3 desc + blank + SPDX + close
PROSE_MAX = 3     # @param/@return/@brief tag lines do not count

TAG = re.compile(r'^\s*\*\s*@')
DOC = re.compile(r'/\*\*.*?\*/', re.S)
HDR = re.compile(r'/\*.*?\*/', re.S)

# Process log, not documentation: build-phase names, first person, and the
# narration of how the code came to be.
BANNED = [
    # Phase names as the plan writes them: "(P3g)", "see A2b".  A bare P0..P3
    # is a Nordic GPIO port and M3/M4 are Cortex cores, so the P-form requires
    # its letter suffix and the M-form is not matched at all.
    (re.compile(r'\((?:P[0-9][a-g]|A[1-4][a-b]?|H[0-3])\)'
                r'|\b(?:see|per|from|in|since|until|after|before|blocked on)\s+'
                r'(?:P[0-9][a-g]|A[1-4][a-b]?|H[0-3])\b'), 'build-phase reference'),
    # Case-sensitive: "I" is I/O and @p i far more often than it is a pronoun.
    # "us" is microseconds after a number or a comma-separated unit label, and
    # an identifier when it follows '*' or precedes an operator.
    (re.compile(r'\b(?:we|We|our|Our)\b'
                r'|(?<![\d,*] )(?<!@param )(?<!@p )(?<!\*)'
                r'\b(?:us|Us)\b(?![,);:=!]|\s*\*/)'),
     'first person'),
    (re.compile(r'\b(?:used to|predated|shipped because|never worked|turned out|'
                r'which is the point|worth recording|the mistake (?:was|here))\b',
                re.I), 'design history (git has it)'),
]


def sources():
    for dirpath, dirnames, filenames in os.walk(ROOT):
        rel = os.path.relpath(dirpath, ROOT)
        top = rel.split(os.sep)[0]
        if top in SKIP_DIRS:
            dirnames[:] = []
            continue
        dirnames[:] = [d for d in dirnames
                       if d not in SKIP_DIRS and not d.startswith('.')]
        for name in filenames:
            if not name.endswith(('.c', '.h', '.inl')):
                continue
            path = os.path.relpath(os.path.join(dirpath, name), ROOT)
            if not path.startswith(SKIP_PATHS):
                yield path


def prose_lines(block):
    """Lines of prose in a doc comment; @tags and their continuations excluded."""
    count = 0
    in_tag = False
    for line in block.split('\n'):
        stripped = line.strip()
        if stripped.startswith('/**') or stripped.startswith('*/'):
            continue
        if TAG.match(line):
            in_tag = True
            continue
        body = stripped.lstrip('*').strip()
        if not body:
            in_tag = False
            continue
        if not in_tag:
            count += 1
    return count


def check(path):
    """Report every style violation in one file."""
    text = open(os.path.join(ROOT, path), errors='replace').read()
    out = []

    header = HDR.match(text)
    if header:
        n = header.group(0).count('\n') + 1
        if n > HEADER_MAX:
            out.append(f"{path}:1: header is {n} lines (max {HEADER_MAX})")
        body_at = header.end()
    else:
        body_at = 0

    for m in DOC.finditer(text, body_at):
        n = prose_lines(m.group(0))
        if n > PROSE_MAX:
            line = text[:m.start()].count('\n') + 1
            out.append(f"{path}:{line}: doc comment has {n} prose lines "
                       f"(max {PROSE_MAX})")

    for m in HDR.finditer(text):
        line = text[:m.start()].count('\n') + 1
        for pattern, why in BANNED:
            hit = pattern.search(m.group(0))
            if hit:
                out.append(f"{path}:{line}: {why}: \"{hit.group(0)}\"")
                break
    return out


def main():
    only = sys.argv[1:]
    problems = []
    for path in sorted(sources()):
        if only and not any(path.startswith(p) for p in only):
            continue
        problems += check(path)
    if problems:
        print("check_comment_style: file headers are licence + author + 2-3 lines;")
        print("  doc comments are 2-3 lines of prose. Design history lives in git.")
        for p in problems[:40]:
            print("  " + p)
        if len(problems) > 40:
            print(f"  ... and {len(problems) - 40} more ({len(problems)} total)")
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
