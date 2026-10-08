#!/usr/bin/env python3
"""Reject optional-service includes and calls from the core kernel."""
import argparse
import pathlib
import re
import sys

OPTIONAL = re.compile(
    r'(?:^|/)(?:shell|basic|services|applications|console|link|usb|init)/'
    r'|(?:^|/)tiku_(?:shell|basic|init|gui|draw|link|console|usbd)(?:_|\.)')
SYMBOL = re.compile(
    r'\btiku_(?:shell|basic|init|gui|draw|link|console|usbd)_[A-Za-z0-9_]+\b')
TOKEN = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                   re.S)


def inspect(path):
    """Keep quoted includes, but ignore comments and string literals in code."""
    source = path.read_text(encoding="utf-8")
    clean = TOKEN.sub(lambda m: "\n" * m[0].count("\n")
                      if m[0].startswith("/") else m[0], source)
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
    failures = 0
    for path in sorted(files):
        for line, message in inspect(path):
            print(f"{path.relative_to(args.root)}:{line}: {message}")
            failures += 1
    print(f"kernel dependency check: {len(files)} files, {failures} violations")
    return int(failures != 0)


if __name__ == "__main__":
    sys.exit(main())
