#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# check_kernel_dependencies.py - kernel and compute dependency boundaries.
#
# SPDX-License-Identifier: Apache-2.0
"""Reject optional kernel/compute dependencies and platform includes in
portable radio, compute and ML code."""
import argparse
import pathlib
import re
import sys

OPTIONAL = re.compile(
    r'(?:^|/)(?:shell|basic|services|applications|console|link|usb|init)/'
    r'|(?:^|/)tiku_(?:shell|basic|init|gui|draw|link|console|usbd)(?:_|\.)')
# Portable consumers and shared compute code exclude project arch headers.
PORTABLE = ("interfaces/radio", "hal/compute", "tikukits/ml/operators",
            "tikukits/ml/inference")
PLATFORM = re.compile(r'(?:^|/)arch/')
SYMBOL = re.compile(
    r'\btiku_(?:shell|basic|init|gui|draw|link|console|usbd)_[A-Za-z0-9_]+\b')
COMPUTE_INCLUDE = re.compile(
    r'(?:^|/)(?:tikukits|experiment)/|(?:^|/)(?:esp_nn|arm_nn|tiku_kits_)[^/]*\.'
    r'|(?:^|/)(?:cmsis_nn|CMSIS-NN|esp-nn)/')
COMPUTE_SYMBOL = re.compile(
    r'\b(?:tiku_(?:kits|exp|llm|asr|vit)_|esp_nn_|'
    r'arm_(?:nn_|convolve_|depthwise_|fully_connected_|max_pool_|avgpool_|softmax_))'
    r'[A-Za-z0-9_]+\b')
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


def inspect_compute(path):
    """Reject optional-layer and neural-library dependencies in compute code."""
    yield from inspect(path)
    clean = without_comments(path)
    for line, text in enumerate(clean.splitlines(), 1):
        match = re.match(r'\s*#\s*include\s*[<"]([^>"]+)', text)
        if match and COMPUTE_INCLUDE.search(match[1]):
            yield line, "compute dependency " + match[1]
    code = TOKEN.sub(lambda m: "\n" * m[0].count("\n"), clean)
    for match in COMPUTE_SYMBOL.finditer(code):
        yield code.count("\n", 0, match.start()) + 1, "compute symbol " + match[0]


def main():
    """Check every core directory; accept a fixture root for tests."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--kits-only", action="store_true",
                        help="check portable ML consumers in the TikuKits checkout")
    args = parser.parse_args()
    files = [p for p in (args.root / "kernel").rglob("*")
             if p.suffix in (".c", ".h", ".inl", ".S") and not args.kits_only]
    if not files and not args.kits_only:
        print("kernel dependency check: no core files found", file=sys.stderr)
        return 2
    portable = [p for d in PORTABLE
                if not args.kits_only or d.startswith("tikukits/")
                for p in (args.root / d).rglob("*")
                if p.suffix in (".c", ".h", ".inl")]
    compute_roots = [args.root / "hal/compute"] + list((args.root / "arch").glob("*/compute"))
    compute = [p for d in compute_roots for p in d.rglob("*")
               if p.suffix in (".c", ".h", ".inl", ".S") and not args.kits_only]
    failures = 0
    for path in sorted(files):
        for line, message in inspect(path):
            print(f"{path.relative_to(args.root)}:{line}: {message}")
            failures += 1
    for path in sorted(portable):
        for line, message in inspect_portable(path):
            print(f"{path.relative_to(args.root)}:{line}: {message}")
            failures += 1
    for path in sorted(compute):
        for line, message in inspect_compute(path):
            print(f"{path.relative_to(args.root)}:{line}: {message}")
            failures += 1
    print(f"kernel dependency check: {len(set(files + portable + compute))} files, "
          f"{failures} violations")
    return int(failures != 0)


if __name__ == "__main__":
    sys.exit(main())
