#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_peripheral_qualify.py - C6 internal peripheral and idle qualification.
#
# SPDX-License-Identifier: Apache-2.0
"""Require C6_PERIPHERAL_TEST=1 and a P4 with TIKU_SHELL_CMD_DELAY=1."""
import argparse
import json
from pathlib import Path
import re
from esp32c6_shell import RemoteShell
from esp32c6_storage_reset_qualify import hold_host
from esp32c6_transport import Console


def qualify(console, mac, build):
    """Check internal UART, pin refusal, entropy, alarm, WFI and watchdog reset."""
    result = {"passed": False, "build": build}

    def native(word, expected=None):
        value = int(console.native(word))
        if expected is not None and value != expected:
            raise ValueError(word + ": " + str(value))
        return value

    identity = RemoteShell(console, mac)
    try:
        if identity.build != build:
            raise ValueError("C6 peripheral test build mismatch")
        identity.drain()
    finally:
        identity.close()
    native("build", build)
    route = native("umap")
    native("uarttest", 0)
    native("umap", route)
    result["uart_interrupts"] = native("uartirq")
    if not result["uart_interrupts"]:
        raise ValueError("UART loopback completed without RX interrupts")
    native("pincheck", 0)
    native("entropy", 0)
    result["protected_pins_refused"] = True
    result["entropy_words_completed_and_varied"] = True
    before = native("idles")
    remote = RemoteShell(console, mac)
    try:
        remote.drain()
        text = remote.command("htimer")
        hz = re.search(r"measured ~(\d+) Hz", text)
        if not hz or not 980000 <= int(hz[1]) <= 1020000 or "-- OK" not in text:
            raise ValueError("hardware alarm failed: " + text)
        result["alarm"] = text
        text = remote.command("sleep light")
        if "Idle: WFI" not in text or "awake" not in remote.command("echo awake"):
            raise ValueError("WFI shell/link continuity failed")
    finally:
        remote.close()
    after = native("idles")
    if after <= before:
        raise ValueError("C6 did not execute its idle hook")
    result["wfi_entries"] = after - before
    remote = RemoteShell(console, mac)
    try:
        remote.drain()
        if "LPM disabled" not in remote.command("sleep off"):
            raise ValueError("idle disable failed")
    finally:
        remote.close()
    stopped = native("idles")
    native("entropy", 0)
    native("idles", stopped)
    native("wdtarm", 1)
    hold_host(console, 3000)
    for mode in ("loader", "native"):
        text = console.command("ipc sdio " + mode)
        if "SDMMC result=0 state=2 " not in text:
            raise ValueError("post-watchdog recovery failed: " + text)
    native("build", build)
    native("wdtdone", 1)
    native("wdtwhy", 7)
    native("test", 1)
    result["watchdog_rom_reset_reason"] = 7
    result["passed"] = True
    return result


def main():
    """Require explicit USB identity, C6 identity and test build before writes."""
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("port", "p4-serial", "peer-mac"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--build", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    console = Console(args.port, args.p4_serial, args.output)
    result = {"passed": False}
    try:
        result = qualify(console, args.peer_mac, args.build)
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        console.close()
        (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print("PASS: C6 internal peripherals, WFI and watchdog recovery", flush=True)


if __name__ == "__main__":
    main()
