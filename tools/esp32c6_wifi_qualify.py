#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# esp32c6_wifi_qualify.py - Native C6 station scan and resource-return cycles.
# SPDX-License-Identifier: Apache-2.0
"""Exercise explicitly identified C6 Wi-Fi firmware through the P4 shell."""
import argparse
import json
from pathlib import Path
import re
import time
from esp32c6_transport import Console
from esp32c6_shell import RemoteShell


def require(pattern, text):
    """Return a required output match or refuse the hardware observation."""
    found = re.search(pattern, text)
    if not found:
        raise ValueError("missing " + pattern + ": " + text[-1000:])
    return found


def free_bytes(text):
    """Read the SRAM workspace allocation and capacity from the first tier row."""
    match = require(r"tier pool\s+(\d+) / (\d+)", text)
    return int(match[1]), int(match[2])


def qualify(console, mac, build, cycles, result, publish):
    """Scan once and require heap, workers and guards to return after each batch."""

    def counters():
        values = {
            key: int(console.native(key))
            for key in ("build", "errors", "thrlive", "thrfault")
        }
        if values != {"build": build, "errors": 0, "thrlive": 0, "thrfault": 0}:
            raise ValueError("C6 baseline/return counters: " + str(values))
        return values

    result["baseline"] = counters()
    result["cycles"] = []
    result["scope"] = (
        "native station startup, scan, shutdown and resource return; no AP join or IP traffic"
    )
    for first in range(0, cycles, 10):
        shell = RemoteShell(console, mac)
        try:
            if shell.build != build:
                raise ValueError("C6 Wi-Fi qualification build mismatch")
            shell.drain()
            before, capacity = free_bytes(shell.command("free"))
            if before:
                raise ValueError("workspace allocated before radio startup")
            for index in range(first, min(first + 10, cycles)):
                started = time.monotonic()
                text = shell.command("wifi on")
                require(r"wifi: radio on", text)
                require("up: MAC " + re.escape(mac), text)
                if "kept:" in text or "failed" in text or "abort" in text:
                    raise ValueError("radio startup: " + text)
                row = {"cycle": index + 1}
                calibration = re.search(r"RF calibrated: ([01]) in (\d+) ms", text)
                if calibration:
                    if index:
                        raise ValueError("PHY recalibrated during radio restart")
                    row.update(
                        calibration_result=int(calibration[1]),
                        calibration_ms=int(calibration[2]),
                    )
                if index == 0:
                    require(r"State:\s+up", shell.command("wifi status"))
                    shell.command("wifi scan")
                    scan = ""
                    deadline = time.monotonic() + 20
                    while "scan done" not in scan and time.monotonic() < deadline:
                        scan += shell.drain()
                    found = require(r"scan done -- (\d+) APs? in (\d+) ms", scan)
                    row["scan_aps"] = int(found[1])
                    row["scan_ms"] = int(found[2])
                text = shell.command("wifi off")
                require(r"wifi: radio off", text)
                heap = require(
                    r"heap peaked at (\d+) of (\d+) bytes, (\d+) refused", text
                )
                if int(heap[3]) or "kept:" in text or "did not end" in text:
                    raise ValueError("radio teardown: " + text)
                row.update(
                    heap_peak=int(heap[1]),
                    heap_bytes=int(heap[2]),
                    refused=int(heap[3]),
                )
                used, after_capacity = free_bytes(shell.command("free"))
                if used or after_capacity != capacity:
                    raise ValueError(
                        "SRAM tier did not return: " + str((used, after_capacity))
                    )
                row["elapsed_ms"] = round((time.monotonic() - started) * 1000)
                result["cycles"].append(row)
                publish()
                print(
                    "C6 Wi-Fi cycle "
                    + str(index + 1)
                    + "/"
                    + str(cycles)
                    + " peak="
                    + str(row["heap_peak"])
                    + " workspace=0",
                    flush=True,
                )
            require(r"State:\s+down", shell.command("wifi status"))
        finally:
            shell.close()
        result["last_counters"] = counters()
        publish()
    result["passed"] = True


def main():
    """Require explicit identities and a native Wi-Fi build before radio operations."""
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("port", "p4-serial", "peer-mac"):
        parser.add_argument("--" + key, required=True)
    parser.add_argument("--build", type=int, required=True)
    parser.add_argument("--cycles", type=int, default=100)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.cycles <= 1000:
        parser.error("--cycles must be 1..1000")
    args.output.mkdir(parents=True, exist_ok=False)
    result = {"passed": False, "build": args.build, "peer_mac": args.peer_mac}

    def publish():
        (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")

    console = Console(args.port, args.p4_serial, args.output)
    try:
        qualify(
            console, args.peer_mac.lower(), args.build, args.cycles, result, publish
        )
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        console.close()
        publish()
    print(
        "PASS: native C6 station scan and "
        + str(args.cycles)
        + " resource-return cycles",
        flush=True,
    )


if __name__ == "__main__":
    main()
