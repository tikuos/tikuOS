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
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Scope is the tracked source tree, decided by git (see tracked()). Anything
# gitignored -- notes, scratch, experiments, separate repos -- is out of scope
# by construction, which means a nested repo that this one ignores is NOT
# checked by a plain run: it reports success without opening a file there.
# Such a tree lints itself with --root, which points both the walk and the
# git query at it, so it is scoped by its OWN tracking.
# SKIP_DIRS covers what git DOES track but this repo should not reformat:
# vendor headers and submodule content.
SKIP_DIRS = {"build", ".git", "drivers", "TikuBench", "tikukits"}
SKIP_PATHS = ("arch/ambiq/cmsis", "arch/nordic/mdk", "tools/fat32",
              # This file quotes the banned patterns in order to define them.
              "tools/check_comment_style.py")

# C family: /* */ header, /** */ doc comments.  Everything else carries a
# header and nothing doxygen-shaped, so only the header cap and the vocabulary
# apply there.
C_EXT = ('.c', '.h', '.inl')
BLOCK_EXT = ('.ld', '.S', '.m')     # /* */ header
HASH_EXT = ('.py', '.sh')           # # header, after any #! line

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


def tracked():
    """Paths git tracks, or None when that cannot be determined."""
    try:
        out = subprocess.run(["git", "-C", ROOT, "ls-files"],
                             capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    names = {line for line in out.stdout.splitlines() if line}
    return names or None


def sources():
    keep = tracked()
    for dirpath, dirnames, filenames in os.walk(ROOT):
        rel = os.path.relpath(dirpath, ROOT)
        top = rel.split(os.sep)[0]
        if top in SKIP_DIRS:
            dirnames[:] = []
            continue
        dirnames[:] = [d for d in dirnames
                       if d not in SKIP_DIRS and not d.startswith('.')]
        for name in filenames:
            if not name.endswith(C_EXT + BLOCK_EXT + HASH_EXT):
                continue
            path = os.path.relpath(os.path.join(dirpath, name), ROOT)
            if path.startswith(SKIP_PATHS):
                continue
            if keep is not None and path not in keep:
                continue
            yield path


def hash_header(text):
    """(line count, text) of a leading '#' comment block, skipping any shebang."""
    lines = text.split('\n')
    i = 1 if lines and lines[0].startswith('#!') else 0
    start = i
    while i < len(lines) and lines[i].startswith('#'):
        i += 1
    return (i - start), '\n'.join(lines[start:i])


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

    if path.endswith(HASH_EXT):
        n, _ = hash_header(text)
        if n > HEADER_MAX:
            out.append(f"{path}:1: header is {n} lines (max {HEADER_MAX})")
        # '#' comments are line-scoped, so the vocabulary scan is per line.
        for i, line in enumerate(text.split('\n'), 1):
            stripped = line.lstrip()
            if not stripped.startswith('#'):
                continue
            for pattern, why in BANNED:
                hit = pattern.search(line)
                if hit:
                    out.append(f"{path}:{i}: {why}: \"{hit.group(0)}\"")
                    break
        return out

    header = HDR.match(text)
    if header:
        n = header.group(0).count('\n') + 1
        if n > HEADER_MAX:
            out.append(f"{path}:1: header is {n} lines (max {HEADER_MAX})")
        body_at = header.end()
    else:
        body_at = 0

    if path.endswith(C_EXT):
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
    global ROOT
    argv = sys.argv[1:]
    if argv and argv[0].startswith("--root="):
        ROOT = os.path.abspath(argv[0].split("=", 1)[1])
        argv = argv[1:]
    only = argv
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
