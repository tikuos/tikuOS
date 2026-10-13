#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# check_prose_style.py - check tracked Markdown against prose-style.md.
#
# Reports the Banned vocabulary of that page in every tracked .md file outside
# the vendor trees and nested repositories; fenced and inline code and the
# contract pages that quote the words are not matched.  Exits 1 on findings.
#
# SPDX-License-Identifier: Apache-2.0

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SKIP_DIRS = {"build", ".git"}
NESTED = {"drivers", "TikuBench", "tikukits", "applications", "experiment",
          "kintsugi", "temp"}
SKIP_PATHS = ("arch/ambiq/cmsis", "arch/nordic/mdk",
              # These quote the banned words in order to define them.
              "prose-style.md", "comment-style.md", "hygiene/commit-style.md")

# Each entry: the pattern and the reason prose-style.md gives.
BANNED = [
    (re.compile(r"\b(?:easy|easily|simple|simply|quick|quickly|"
                r"straightforward|trivial|trivially|effortless|intuitive)\b",
                re.I), "reassurance: say what it does"),
    # "just" as a minimiser ("just type", "just a few") rather than a time
    # ("just landed") or justice; the forms before a verb or a count.
    (re.compile(r"\bjust (?:type|run|write|call|add|use|a few|one|two|"
                r"a single|a)\b", re.I), "reassurance: say what it does"),
    (re.compile(r"\b(?:clean|cleanly|elegant|elegantly|powerful|robust|"
                r"seamless|seamlessly|nice|nicely|lightweight)\b", re.I),
     "praise standing in for a fact"),
    (re.compile(r"\b(?:tiny|small|minimal) (?:and|but|,)? ?(?:elegant|clean|"
                r"simple|lightweight|powerful)\b", re.I),
     "praise standing in for a size"),
    (re.compile(r"\b(?:in most cases|for the most part|generally|typically|"
                r"usually|more or less)\b", re.I), "hedge: name the case"),
    (re.compile(r"\((?:P[0-9][a-g]|A[1-4][a-b]?|H[0-3]|M[0-9]+[a-d]?)\)"
                r"|\b(?:phase|milestone) [0-9]+\b", re.I),
     "plan label: describe the state"),
    (re.compile(r"\b(?:used to|turned out|we found that|the mistake was|"
                r"never worked)\b", re.I), "history: say what it does now"),
    (re.compile(r"\b(?:note that|it is worth noting|worth noting|"
                r"importantly|of course|obviously|basically|essentially)\b",
                re.I), "opener: delete it, keep the sentence"),
    (re.compile(r"\b(?:feel free to|don't hesitate|happy to)\b", re.I),
     "encouragement: an instruction, or nothing"),
]

FENCE = re.compile(r"^(```|~~~)")
INLINE_CODE = re.compile(r"`[^`\n]*`")
LINK_TARGET = re.compile(r"\]\([^)]*\)")


def tracked():
    """Root-relative paths git tracks, or None when git cannot say."""
    try:
        out = subprocess.run(["git", "-C", ROOT, "ls-files"],
                             capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    names = {line for line in out.stdout.splitlines() if line}
    return names or None


def pages():
    """Yield the root-relative path of every tracked Markdown file checked."""
    keep = tracked()
    for dirpath, dirnames, filenames in os.walk(ROOT):
        rel = os.path.relpath(dirpath, ROOT)
        top = rel.split(os.sep)[0]
        if top in SKIP_DIRS or top in NESTED:
            dirnames[:] = []
            continue
        dirnames[:] = [d for d in dirnames
                       if d not in SKIP_DIRS and not d.startswith(".")]
        for name in filenames:
            if not name.endswith(".md"):
                continue
            path = os.path.relpath(os.path.join(dirpath, name), ROOT)
            if path.startswith(SKIP_PATHS):
                continue
            if keep is not None and path not in keep:
                continue
            yield path


def prose_lines(text):
    """(line number, text) for each line outside fenced code, with inline
    code and link targets blanked, so a flag or path is never matched."""
    fenced = False
    for n, line in enumerate(text.splitlines(), 1):
        if FENCE.match(line.strip()):
            fenced = not fenced
            continue
        if fenced:
            continue
        line = INLINE_CODE.sub("`", line)
        line = LINK_TARGET.sub("]()", line)
        yield n, line


def check(path):
    """The findings in one page, each as 'path:line: hit (reason)'."""
    with open(os.path.join(ROOT, path), encoding="utf-8",
              errors="replace") as f:
        text = f.read()
    found = []
    for n, line in prose_lines(text):
        for pattern, reason in BANNED:
            for hit in pattern.finditer(line):
                found.append(f"{path}:{n}: \"{hit.group(0)}\" ({reason})")
    return found


def main():
    """Check the given paths, or every tracked page; 1 on findings."""
    only = []
    for a in sys.argv[1:]:
        if os.path.exists(a):
            a = os.path.relpath(os.path.abspath(a), ROOT)
            if a == ".":
                a = ""
        only.append(a)
    problems = []
    seen = 0
    for path in sorted(pages()):
        if only and not any(path.startswith(p) for p in only):
            continue
        seen += 1
        problems += check(path)
    if only and seen == 0:
        print("check_prose_style: no tracked Markdown under " +
              " ".join(sys.argv[1:]))
        return 2
    if problems:
        print("check_prose_style: prose states what the system does; "
              "prose-style.md has the words it does not use.")
        for p in problems[:40]:
            print("  " + p)
        if len(problems) > 40:
            print(f"  ... and {len(problems) - 40} more ({len(problems)} total)")
        return 1
    print(f"check_prose_style: {seen} Markdown files, no findings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
