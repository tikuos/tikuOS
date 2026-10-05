#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# check_comment_style.py - check source comments against comment-style.md.
#
# Reports file headers over HEADER_MAX lines, doc comments over PROSE_MAX lines
# of prose and the banned vocabulary; --strict adds emphasis capitals, plan
# labels and arguing words.  Exits 1 on findings, 2 when no path matched.
#
# SPDX-License-Identifier: Apache-2.0

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Scope is the source tree git tracks (see tracked()).  A gitignored file is
# not opened, so a nested repository this one ignores is not checked by a plain
# run, which reports success without it.  Such a tree lints itself with --root,
# which points the walk and the git query at it.  SKIP_DIRS (at any depth) and
# NESTED (top level only: kernel/drivers is this repository's code) cover what
# git tracks but this check skips: build output and the nested repositories.
SKIP_DIRS = {"build", ".git"}
NESTED = {"drivers", "TikuBench", "tikukits", "hygiene"}
SKIP_PATHS = ("arch/ambiq/cmsis", "arch/nordic/mdk",
              # This file quotes the banned patterns in order to define them.
              "tools/check_comment_style.py")

# C family: /* */ header, /** */ doc comments.  Everything else carries a
# header and nothing doxygen-shaped, so only the header cap and the vocabulary
# apply there.
C_EXT = ('.c', '.h', '.inl')
BLOCK_EXT = ('.ld', '.S', '.m')     # /* */ header
HASH_EXT = ('.py', '.sh')           # # header, after any #! line

HEADER_MAX = 15   # 7 boilerplate, filename, blank, 3 desc, blank, SPDX, close
PROSE_MAX = 3     # @param/@return/@brief tag lines do not count

TAG = re.compile(r'^\s*\*\s*@')
# Block tags and structural commands only: an inline @p or @c starting a line
# is still prose.
BLOCK_TAG = re.compile(r'^\s*\*\s*@(?:brief|param|return|returns|retval|note|'
                       r'warning|see|pre|post|deprecated|code|endcode|def|'
                       r'typedef|defgroup|addtogroup|ingroup|file|struct|union|'
                       r'enum|fn|var|name|section|subsection|page|\{|\})(?![A-Za-z])')

# --strict adds these checks; make lint runs it over kernel/, hal/,
# interfaces/, tiku.h and main.c.
STRICT = False
EMPHASIS = re.compile(
    r'(?<![A-Za-z0-9_])(?:NOT|ONLY|MUST|NEVER|ALWAYS|ONE|ONCE|BOTH|EVERY|ALL|'
    r'IS|ARE|BEFORE|AFTER|HERE|THIS|THAT|SAME|OWN|LAST|FIRST|ANY|EACH|NOW|'
    r'WHOLE|EXACTLY|NOTHING|ALREADY|STILL|ITSELF|CANNOT|NO)(?![A-Za-z0-9_])')
STRICT_BANNED = [
    (re.compile(r'\b(?:[Pp]hase|[Mm]ilestone) [0-9A-Z][0-9]?(?:\.[0-9]+)?[a-z]?\b'
                r'|\bTier-[0-9]\b'), 'plan label'),
    (re.compile(r'\b(?:deliberately|load-bearing|on purpose|the whole point)\b',
                re.I), 'arguing, not stating'),
    (re.compile(r'(?<![\d*] )(?<!@param )(?<!@p )(?<!\()\b(?:us|Us)\b(?=[,)])'
                r'|\bours\b'), 'first person'),
]
DOC = re.compile(r'/\*\*.*?\*/', re.S)
HDR = re.compile(r'/\*.*?\*/', re.S)

# Banned in every run: plan labels, first person, design-history phrases,
# capitals used for emphasis, and milestones used as headings.
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
    # ALL-CAPS used for emphasis rather than for a name: an opener of three
    # or more capitalised words with no lowercase between them.  Register and
    # field names are one token (SCKDIVCR2, HFNMIENA) and acronym runs like
    # "MPU BEFORE cache" carry a lowercase word, so both fall outside; the
    # match needs a comma or a full stop, which is what a sentence-shaped
    # declaration has and a name list does not.
    # One or two spaces after the star only: a deeper indent is a wrapped
    # @brief/@note continuation, where a run of caps is a name being carried
    # over ("... NVIC number for the / EVB COM UART.") rather than an opener.
    (re.compile(r'^[ \t]*\*[ \t]{1,2}[A-Z][A-Z0-9_]*(?:[ \t]+[A-Z][A-Z0-9_]*){2,}'
                r'[ \t]*[,.]', re.M),
     'capitals for emphasis, not a name'),
    # A milestone used as a heading: "* U1: bring-up", "R7b: SDRAM".  Only the
    # label form is matched.  A bare U3 or R7 is a board designator (the flash
    # is U3, the eMMC U11) and R1b is an eMMC response type, so shape alone
    # cannot separate those from plan names -- the trailing colon can.
    (re.compile(r'^[ \t]*\*?[ \t]*[UR][0-9]{1,2}[a-c]?:[ \t]', re.M),
     'milestone as a heading'),
]
# Joined across line breaks under --strict, so "used to" split over two
# lines is found as well.
HISTORY = BANNED[2]
assert HISTORY[1] == 'design history (git has it)'


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
    """Yield the root-relative path of every tracked source file checked."""
    keep = tracked()
    for dirpath, dirnames, filenames in os.walk(ROOT):
        rel = os.path.relpath(dirpath, ROOT)
        top = rel.split(os.sep)[0]
        if top in SKIP_DIRS or top in NESTED:
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
    """(line count, text) of the leading '#' block, after any shebang."""
    lines = text.split('\n')
    i = 1 if lines and lines[0].startswith('#!') else 0
    start = i
    while i < len(lines) and lines[i].startswith('#'):
        i += 1
    return (i - start), '\n'.join(lines[start:i])


def prose_lines(block):
    """Prose lines in a doc comment; @tags and continuations do not count."""
    count = 0
    in_tag = False
    tag = BLOCK_TAG if STRICT else TAG
    for line in block.split('\n'):
        stripped = line.strip()
        if stripped.startswith('/**'):
            first = stripped[3:].lstrip('<').strip().strip('*/').strip()
            if STRICT and first.startswith('@'):
                in_tag = BLOCK_TAG.match(' * ' + first) is not None
                count += 0 if in_tag else 1
            elif STRICT and first:
                count += 1      # text on the opening line is prose too
            continue
        if stripped.startswith('*/'):
            continue
        if tag.match(line):
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
        hit = None
        for pattern, why in BANNED:
            hit = pattern.search(m.group(0))
            if hit:
                out.append(f"{path}:{line}: {why}: \"{hit.group(0)}\"")
                break
        if STRICT and hit is None:
            out += strict_findings(path, line, m.group(0))
    return out


def strict_findings(path, line, block):
    """The --strict checks for one comment block that passed the default."""
    out = []
    words = ' '.join(l.strip().lstrip('/*').strip()
                     for l in block.split('\n'))
    for pattern, why in STRICT_BANNED + [HISTORY]:
        hit = pattern.search(words)
        if hit:
            out.append(f"{path}:{line}: {why}: \"{hit.group(0)}\"")
            return out
    for i, raw in enumerate(block.split('\n')):
        prose = raw.strip().lstrip('/*').strip()
        if not re.search('[a-z]', prose):
            continue        # a banner label or a list of names
        for hit in EMPHASIS.finditer(prose):
            if not is_name(path, prose, hit):
                out.append(f"{path}:{line + i}: capitals for emphasis: "
                           f"\"{hit.group(0)}\"")
                return out
    return out


# Code with its comments removed and its string literals kept.
UNCOMMENT = re.compile(r'//[^\n]*|/\*.*?\*/|("(?:\\.|[^"\\\n])*"'
                       r"|'(?:\\.|[^'\\\n])*')", re.S)
NAMES = {}


def names_near(path):
    """Capitalised words the code of a file's directory uses as names.

    A string literal of the word ("EVERY", a BASIC keyword) or the last part
    of an identifier (TIKU_RESTART_NEVER) makes it a name there.
    """
    where = os.path.dirname(path)
    if where not in NAMES:
        found = set()
        full = os.path.join(ROOT, where)
        for name in os.listdir(full):
            if not name.endswith(C_EXT):
                continue
            text = open(os.path.join(full, name), errors='replace').read()
            code = UNCOMMENT.sub(lambda m: m.group(1) or ' ', text)
            found.update(re.findall(r'"([A-Z]+)"', code))
            found.update(ident.rsplit('_', 1)[1] for ident in
                         re.findall(r'\b[A-Za-z0-9_]*_[A-Z]+\b', code))
        NAMES[where] = found
    return NAMES[where]


def is_name(path, prose, hit):
    """True when a capitalised word is a name rather than emphasis."""
    before = prose[:hit.start()]
    after = prose[hit.end():]
    if after[:1] in ('(', '$') or re.search(r'@[cp]\s+$', before):
        return True     # NOW(), DATE$, @c ALL
    if before[-1:] in ('"', "'", '`') and after[:1] == before[-1:]:
        return True     # quoted
    if before[-1:] == '|' or after[:1] == '|':
        return True     # OR'd flags: ONE|ONE
    return hit.group(0) in names_near(path)


def untracked_sources():
    """Source files git does not track: the check skips them, and main()
    prints them as not checked."""
    try:
        out = subprocess.run(["git", "-C", ROOT, "ls-files", "--others",
                              "--exclude-standard"],
                             capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return []
    if out.returncode != 0:
        return []
    exts = C_EXT + BLOCK_EXT + HASH_EXT
    return [f for f in out.stdout.splitlines()
            if f.endswith(exts)
            and not f.startswith(tuple(SKIP_DIRS | NESTED))
            and not f.startswith(SKIP_PATHS)]


def main():
    """Check the given paths; return 0, 1 on findings, 2 if none matched."""
    global ROOT
    argv = sys.argv[1:]
    # --prose= and --header= set another tree's ceilings: the applications
    # repository runs 6 lines of prose and a 20-line header.
    global HEADER_MAX, PROSE_MAX, STRICT
    while argv and argv[0].startswith("--"):
        if argv[0].startswith("--root="):
            ROOT = os.path.abspath(argv[0].split("=", 1)[1])
        elif argv[0].startswith("--prose="):
            PROSE_MAX = int(argv[0].split("=", 1)[1])
        elif argv[0].startswith("--header="):
            HEADER_MAX = int(argv[0].split("=", 1)[1])
        elif argv[0] == "--strict":
            STRICT = True
        else:
            break
        argv = argv[1:]
    # A path argument that names something on disk is taken relative to the
    # working directory and made relative to the root; anything else is a
    # prefix of root-relative paths.
    only = []
    for a in argv:
        if os.path.exists(a):
            a = os.path.relpath(os.path.abspath(a), ROOT)
            if a == '.':
                a = ''
        only.append(a)
    problems = []
    seen = 0
    for path in sorted(sources()):
        if only and not any(path.startswith(p) for p in only):
            continue
        seen += 1
        problems += check(path)
    if only and seen == 0:
        print("check_comment_style: no tracked source under " +
              " ".join(argv) + " (paths are taken from " + ROOT + ")")
        return 2
    skipped = untracked_sources()
    if skipped:
        print("check_comment_style: NOT CHECKED -- untracked, so out of scope:")
        for f in skipped[:10]:
            print("  " + f)
        if len(skipped) > 10:
            print(f"  ... and {len(skipped) - 10} more")
        print("  Stage them and run again; a new file otherwise passes unread.")

    if problems:
        print("check_comment_style: file headers are licence + author + 2-3 lines;")
        print(f"  doc comments are at most {PROSE_MAX} lines of prose. Design history lives in git.")
        for p in problems[:40]:
            print("  " + p)
        if len(problems) > 40:
            print(f"  ... and {len(problems) - 40} more ({len(problems)} total)")
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
