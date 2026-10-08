#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# check_commit_msg.py - hold commit messages to the shape in commentstyle.md.
#
# Subject is `Area: what now works`; the body is bullet points, at most five.
# Runs from the commit-msg hook, so a message that drifts is refused at the
# moment it is written rather than found in the log months later.
#
# SPDX-License-Identifier: Apache-2.0

import os
import re
import subprocess
import sys

SUBJECT_MAX = 72
BODY_MAX_BULLETS = 3

# `Area: something`, where the area reads as a name rather than a slug.
SUBJECT_RE = re.compile(r"^([A-Z][A-Za-z0-9./+-]*(?: [A-Za-z0-9./+-]+)*): (.+)$")

# Milestone shorthand nobody can decode later.  Only the unambiguous forms:
# a bare letter-digit token is as often a part number (Ethos-U55, Cortex-M85,
# nRF54L15) as a milestone, so it counts only inside parentheses or when
# followed by milestone vocabulary.
MILESTONE_RE = re.compile(
    r"\((?:[A-Z]\d+[a-z]?|\d+)\)|"
    r"\b(?i:phase|milestone|step|stage|track)\s*[\dA-Z]\b|"
    r"\bM\d+\.\d+\b|"
    r"(?<![\w-])[A-Z]\d{1,2}[a-z]?\s+"
    r"(?i:gate|gates|done|closed|complete|completed|landed|log|plan)\b|"
    r"(?<![\w-])[A-Z]\d{1,2}[a-z]?\s*(?:-|to|through)\s*[A-Z]?\d{1,2}[a-z]?"
    r"\s+(?i:done|closed|complete|landed)\b")

# Openers that name the session rather than the change.  These are the bare
# IMPERATIVE forms; the noun forms are the accepted way to title a set of
# related changes ("Improvements to X", "Fixes for X", "Support for X").  The
# \b does that split on its own -- `improve\b` misses "Improvements", `fix\b`
# misses "Fixes" -- so adding a verb here never bans its noun.
ACTIVITY_RE = re.compile(
    r"^(?:finish|finished|trim|cleanup|clean up|update|updated|improve|"
    r"improved|tweak|refactor|rework|misc|various|more|fix|fix up|wip)\b",
    re.IGNORECASE)

TRAILER_RE = re.compile(r"^\s*(?:Co-Authored-By|Generated with|Claude-Session)",
                        re.IGNORECASE)


def _git(*args):
    """One git query, or None when git has nothing to say."""
    try:
        out = subprocess.run(("git",) + args, capture_output=True, text=True,
                             timeout=5)
    except (OSError, subprocess.SubprocessError):
        return None
    return out.stdout.strip() if out.returncode == 0 and out.stdout.strip() \
        else None


def author_mismatch():
    """The author this commit will carry, when it is not this repo's identity.

    A `-c user.email=` override sits above local and global config, so the
    effective author and the configured one disagree exactly when an identity
    has been substituted for a single command.  Returns (effective, configured)
    on a mismatch, else None.
    """
    git_dir = _git("rev-parse", "--git-dir")
    if git_dir:
        # A replay carries someone else's authorship on purpose.
        for marker in ("rebase-merge", "rebase-apply", "CHERRY_PICK_HEAD",
                       "MERGE_HEAD", "REVERT_HEAD"):
            if os.path.exists(os.path.join(git_dir, marker)):
                return None

    ident = _git("var", "GIT_AUTHOR_IDENT")
    if not ident:
        return None
    m = re.search(r"<([^>]*)>", ident)
    if not m:
        return None
    effective = m.group(1)

    configured = _git("config", "--local", "user.email") or \
        _git("config", "--global", "user.email")
    if not configured or effective == configured:
        return None
    return (effective, configured)


def check(text):
    """Return a list of complaints about one commit message."""
    bad = []
    # Comment lines are the editor's template, not the message.
    lines = [ln for ln in text.splitlines() if not ln.startswith("#")]
    while lines and not lines[-1].strip():
        lines.pop()
    if not lines or not lines[0].strip():
        return ["empty commit message"]

    subject = lines[0].rstrip()

    # A fixup or a merge is machinery, not authored prose.
    if subject.startswith(("Merge ", "Revert ", "fixup!", "squash!")):
        return []

    if len(subject) > SUBJECT_MAX:
        bad.append(f"subject is {len(subject)} chars, max {SUBJECT_MAX}")
    if subject.endswith("."):
        bad.append("subject ends with a period")

    m = SUBJECT_RE.match(subject)
    if not m:
        bad.append("subject is not `Area: what now works` with a capitalised "
                   "area (e.g. `RA8P1 Port: 1 GHz core clock`)")
    else:
        rest = m.group(2)
        if ACTIVITY_RE.match(rest):
            bad.append(f"subject describes the session, not the change: "
                       f"{rest.split()[0]!r}")
    if MILESTONE_RE.search(subject):
        bad.append("subject carries a milestone marker; name the capability")

    if len(lines) > 1 and lines[1].strip():
        bad.append("no blank line between subject and body")

    body = [ln for ln in lines[2:] if ln.strip()]
    if body:
        bullets = [ln for ln in body if ln.lstrip().startswith("- ")]
        # Continuations are indented under their bullet; anything else at the
        # left margin is a prose paragraph.
        prose = [ln for ln in body
                 if not ln.lstrip().startswith("- ")
                 and not ln.startswith(("  ", "\t"))
                 and not TRAILER_RE.match(ln)]
        if not bullets:
            bad.append("body is prose; use `- ` bullet points")
        elif prose:
            bad.append(f"body mixes prose with bullets: {prose[0].strip()[:40]!r}")
        if len(bullets) > BODY_MAX_BULLETS:
            bad.append(f"body has {len(bullets)} bullets, max "
                       f"{BODY_MAX_BULLETS}")

    for ln in lines:
        if TRAILER_RE.match(ln):
            bad.append(f"tool or assistant trailer: {ln.strip()[:40]!r}")
            break
    # Plain English: a "--" aside is the tell of a written-up message.
    for ln in lines:
        if " -- " in ln or ln.rstrip().endswith("--"):
            bad.append("plain English only: no `--` asides "
                       f"({ln.strip()[:40]!r}); say the fact in one clause")
            break
    return bad


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: check_commit_msg.py <file>\n")
        return 2
    try:
        with open(argv[1], encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError as e:
        sys.stderr.write(f"check_commit_msg: {e}\n")
        return 2

    bad = check(text)

    mismatch = author_mismatch()
    if mismatch:
        effective, configured = mismatch
        sys.stderr.write(
            f"\ncommit author is not this repository's identity:\n"
            f"  committing as: {effective}\n"
            f"  configured:    {configured}\n\n"
            f"  Drop any `-c user.name=` / `-c user.email=` and commit plainly.\n"
            f"  An address from elsewhere maps to a different GitHub handle.\n"
            f"  Deliberate? git commit --no-verify\n\n")
        return 1

    if not bad:
        return 0
    sys.stderr.write("\ncommit message does not match commentstyle.md:\n")
    for b in bad:
        sys.stderr.write(f"  - {b}\n")
    sys.stderr.write("\n  Area: what now works\n\n"
                     "  - bullet, at most five, one line each\n"
                     "  - say how it was checked\n\n"
                     "See commentstyle.md. To bypass once: git commit "
                     "--no-verify\n\n")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
