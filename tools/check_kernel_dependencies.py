#!/usr/bin/env python3
"""Reject optional-service includes and calls from the core kernel, and
platform includes from the portable radio layer."""
import argparse
import pathlib
import re
import sys

OPTIONAL = re.compile(
    r'(?:^|/)(?:shell|basic|services|applications|console|link|usb|init)/'
    r'|(?:^|/)tiku_(?:shell|basic|init|gui|draw|link|console|usbd)(?:_|\.)')
# interfaces/radio/ runs on every port with the radio and reaches the
# hardware through hal/ only.
PORTABLE = ("interfaces/radio",)
PLATFORM = re.compile(r'(?:^|/)arch/')
SYMBOL = re.compile(
    r'\btiku_(?:shell|basic|init|gui|draw|link|console|usbd)_[A-Za-z0-9_]+\b')
TOKEN = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                   re.S)


def without_comments(path):
    """@p path's text with comments blanked; string literals kept."""
    source = path.read_text(encoding="utf-8")
    return TOKEN.sub(lambda m: "\n" * m[0].count("\n")
                     if m[0].startswith("/") else m[0], source)


def inspect_portable(path):
    """Yield each include of a platform (arch/) header."""
    for line, text in enumerate(without_comments(path).splitlines(), 1):
        match = re.match(r'\s*#\s*include\s*[<"]([^>"]+)', text)
        if match and PLATFORM.search(match[1]):
            yield line, "platform include " + match[1]


def inspect(path):
    """Keep quoted includes, but ignore comments and string literals in code."""
    clean = without_comments(path)
    for line, text in enumerate(clean.splitlines(), 1):
        match = re.match(r'\s*#\s*include\s*[<"]([^>"]+)', text)
        if match and OPTIONAL.search(match[1]):
            yield line, "optional include " + match[1]
    code = TOKEN.sub(lambda m: "\n" * m[0].count("\n"), clean)
    for match in SYMBOL.finditer(code):
        yield code.count("\n", 0, match.start()) + 1, "optional symbol " + match[0]


def main():
    """Check every core directory; accept a fixture root for tests."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    files = [p for p in (args.root / "kernel").rglob("*")
             if p.suffix in (".c", ".h", ".inl", ".S")]
    if not files:
        print("kernel dependency check: no core files found", file=sys.stderr)
        return 2
    portable = [p for d in PORTABLE for p in (args.root / d).rglob("*")
                if p.suffix in (".c", ".h", ".inl")]
    failures = 0
    for path in sorted(files):
        for line, message in inspect(path):
            print(f"{path.relative_to(args.root)}:{line}: {message}")
            failures += 1
    for path in sorted(portable):
        for line, message in inspect_portable(path):
            print(f"{path.relative_to(args.root)}:{line}: {message}")
            failures += 1
    print(f"kernel dependency check: {len(files) + len(portable)} files, "
          f"{failures} violations")
    return int(failures != 0)


if __name__ == "__main__":
    sys.exit(main())
