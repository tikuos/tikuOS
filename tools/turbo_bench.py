#!/usr/bin/env python3
"""
turbo_bench.py - host-side runner for the CPU frequency-scaling benchmark.

Times the apps/turbo_bench firmware at the desktop level: the device runs each
TikuKits workload at 96 MHz (LP) and 192 MHz (HP/turbo) and brackets every run
with serial markers ([BENCH:RUN] / [BENCH:DONE]); this script timestamps the
markers and prints the wall-clock speedup. The per-workload checksum is printed
by the firmware identically at both clocks, so the time delta is purely the
core-frequency speedup.

Build + flash the firmware first (Apollo4 Lite shown; apollo510 also works):

    make MCU=apollo4l TIKU_TURBO_BENCH=1 TIKU_SHELL_ENABLE=0 \\
         HAS_TESTS=0 HAS_EXAMPLES=0 flash

then (the firmware waits ~3 s at boot for you to attach):

    python3 tools/turbo_bench.py --port /dev/cu.usbmodemXXXX

Authors: Ambuj Varshney <ambuj@tiku-os.org>
SPDX-License-Identifier: Apache-2.0
"""

import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("ERROR: pyserial not installed (pip install pyserial)")

RUN_RE = re.compile(r"\[BENCH:RUN\]\s+(\S+)\s+mhz=(\d+)\s+iters=(\d+)")
DONE_RE = re.compile(r"\[BENCH:DONE\]\s+(\S+)\s+mhz=(\d+)")


def main():
    ap = argparse.ArgumentParser(description="Time the turbo benchmark firmware.")
    ap.add_argument("--port", required=True, help="serial port of the board")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=int, default=90,
                    help="overall read timeout in seconds (default 90)")
    ap.add_argument("--quiet", action="store_true",
                    help="don't echo the raw [BENCH:*] lines")
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as e:
        sys.exit(f"cannot open {args.port}: {e}")

    start, iters, res, order = {}, {}, {}, []
    buf = b""
    deadline = time.time() + args.timeout
    while time.time() < deadline:
        try:
            chunk = ser.read(256)
        except serial.SerialException as e:
            print(f"serial error: {e}", file=sys.stderr)
            break
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").rstrip("\r")
            if not text:
                continue
            now = time.time()
            if not args.quiet and "[BENCH" in text:
                print(f"  | {text}")
            m = RUN_RE.search(text)
            if m:
                start[(m.group(1), int(m.group(2)))] = now
                iters[m.group(1)] = int(m.group(3))
            m = DONE_RE.search(text)
            if m:
                key = (m.group(1), int(m.group(2)))
                if key in start:
                    res.setdefault(m.group(1), {})[int(m.group(2))] = now - start[key]
                    if m.group(1) not in order:
                        order.append(m.group(1))
            if "[BENCH:END]" in text:
                deadline = 0.0
                break
    ser.close()

    print("\n  workload   iters      96 MHz (LP)   192 MHz (HP)   speedup")
    print("  " + "-" * 57)
    for name in order:
        d = res.get(name, {})
        if 96 in d and 192 in d:
            sp = d[96] / d[192] if d[192] > 0 else 0.0
            print(f"  {name:8} {iters.get(name, 0):8d}    {d[96]*1000:8.1f} ms   "
                  f"{d[192]*1000:9.1f} ms    {sp:4.2f}x")
    if not order:
        sys.exit("no [BENCH] markers seen -- is the turbo-bench firmware flashed "
                 "and did you attach within the ~3 s boot window?")


if __name__ == "__main__":
    main()
