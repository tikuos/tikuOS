#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_console_qualify.py - Remote shell integrity during a bulk SDIO object.
#
# SPDX-License-Identifier: Apache-2.0
"""Check malformed frames, exact retries and shell progress during SDIO bulk."""
import argparse
import json
from pathlib import Path
import re
import secrets
import struct
import time
from esp32c6_shell import RemoteShell
from esp32c6_transport import Console


def qualify(console, mac, profile="slow", interval=2.0):
    """Transfer 600 KiB and require checked shell replies before bulk completion."""
    identity = RemoteShell(console, mac)
    try:
        identity.drain()
    finally:
        identity.close()
    start = len(console.text)
    console.wire.write(("ipc sdio " + profile + "\n").encode())
    console.wait(lambda: "SDIO remote ready channel=f6" in console.text[start:])
    remote = RemoteShell(console, mac, enter_bridge=False)
    result = {
        "build": remote.build,
        "profile": profile,
        "interval_seconds": interval,
        "passed": False,
        "latencies_seconds": [],
    }
    summary = re.compile(
        rf"SDIO object profile={1 if profile == 'image' else 2} bytes=(\d+) peer_bytes=(\d+) "
        r"crc=([0-9a-f]+) peer_crc=([0-9a-f]+) chunks=(\d+) "
        r"controls=(\d+) max_control=(\d+) ticks=(\d+) failures=(\d+)"
    )

    def raw(data):
        console.remote_incoming.clear()
        console.wire.write(console.frame(0xF6, data))
        console.wait(lambda: bool(console.remote_incoming), 3)
        reply = console.remote_incoming.pop(0)
        if not 9 <= len(reply) <= 24:
            raise ValueError("malformed diagnostic reply")
        return reply

    try:
        remote.drain()
        if raw(b"\xa6") != b"\xa6" + bytes(7) + b"\x01":
            raise ValueError("short frame was not refused")
        remote.read()
        header = struct.pack("<BBIH", 0xA6, 3, remote.sid, remote.sequence)
        first = raw(header)
        if raw(header) != first:
            raise ValueError("exact retry changed output")
        changed = bytes([0xA6, 2]) + header[2:] + b"?"
        if raw(changed)[8] != 4:
            raise ValueError("changed duplicate was not refused")
        console.wire.write(console.frame(0xF6, header + b"12345"))
        remote.read()
        result["malformed_frames"] = True
        result["commands_during_bulk"] = 0
        until = time.monotonic() + 900
        while not summary.search(console.text[start:]):
            nonce = "c6q_" + secrets.token_hex(4)
            began = time.monotonic()
            text = remote.command("echo " + nonce)
            elapsed = time.monotonic() - began
            if not re.search(r"(?m)^" + nonce + r"\r?$", text):
                raise ValueError("shell reply mismatch: " + text)
            if elapsed > 10:
                raise ValueError("shell command exceeded ten seconds")
            result["latencies_seconds"].append(elapsed)
            if not summary.search(console.text[start:]):
                result["commands_during_bulk"] += 1
            pause_until = time.monotonic() + interval
            while time.monotonic() < pause_until and not summary.search(
                console.text[start:]
            ):
                console.pump()
            if time.monotonic() >= until:
                raise TimeoutError("bulk object did not finish")
        match = summary.search(console.text[start:])
        n, peer, crc, peer_crc, chunks, controls, bound, ticks, failures = (
            match.groups()
        )
        if (
            int(n) != 614400
            or n != peer
            or crc != peer_crc
            or int(failures)
            or int(bound) > 256
            or not int(controls)
            or not result["commands_during_bulk"]
        ):
            raise ValueError("bulk integrity/deadline failure: " + match[0])
        from tikubench.core.ipc_sdio import validate_stream

        validate_stream(console.text[start:], profile)
        timing = re.compile(r"SDIO remote requests=(\d+) max_control=(\d+)")
        console.wait(lambda: bool(timing.search(console.text[start:])))
        measured = timing.search(console.text[start:])
        if not int(measured[1]) or int(measured[2]) > 256:
            raise ValueError("remote control deadline failed")
        result["remote_requests_during_bulk"] = int(measured[1])
        result["max_remote_control_ticks"] = int(measured[2])
        result["bulk"] = {
            "bytes": int(n),
            "crc": crc,
            "chunks": int(chunks),
            "controls": int(controls),
            "max_control_ticks": int(bound),
            "ticks": int(ticks),
        }
        result["passed"] = True
    finally:
        remote.close()
    return result


def main():
    """Run against an explicitly identified P4 and downstream C6 pair."""
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("port", "p4-serial", "peer-mac"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--profile", choices=("image", "slow"), default="slow")
    parser.add_argument("--interval", type=float, default=2.0)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 0 <= args.interval <= 5:
        parser.error("interval must be between zero and five seconds")
    args.output.mkdir(parents=True, exist_ok=False)
    console = Console(args.port, args.p4_serial, args.output)
    result = {"passed": False}
    try:
        result = qualify(console, args.peer_mac, args.profile, args.interval)
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        console.close()
        (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print("PASS: remote shell integrity and deadlines during SDIO bulk", flush=True)


if __name__ == "__main__":
    main()
