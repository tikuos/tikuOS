#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_shell.py - Identified C6 shell sessions through the owned P4 console.
#
# SPDX-License-Identifier: Apache-2.0
"""Carry a C6 shell session over USB channel f6 and SDIO control messages."""
import argparse
from pathlib import Path
import secrets
import struct
import time
from esp32c6_transport import Console


class RemoteShell:
    """Keep one physical P4 port and one sequenced downstream C6 session."""

    def __init__(self, console, mac, *, enter_bridge=True):
        self.console = console
        self.sid = secrets.randbits(32) or 1
        self.sequence = 0
        self.opened = False
        self.bridge_open = False
        start = len(console.text)
        console.remote_incoming = []
        if enter_bridge:
            console.wire.write(b"ipc sdio shell\n")
            console.wait(
                lambda: "C6 shell bridge ready channel=f6" in console.text[start:]
            )
        self.bridge_start = start
        self.bridge_open = True
        try:
            reply = self.request(1)
            self.opened = True
            if (
                len(reply) != 12
                or reply[:2] != b"C6"
                or reply[2:8] != bytes.fromhex(mac.replace(":", ""))
            ):
                raise ValueError("remote C6 identity mismatch")
            self.build = int.from_bytes(reply[8:12], "little")
        except Exception:
            try:
                self.close()
            except Exception:
                pass
            raise

    def request(self, op, data=b""):
        """Retry one exact request; consume a sequence number after admission."""
        if not self.bridge_open:
            raise ConnectionError("P4 closed the C6 shell bridge")
        if not 1 <= op <= 4 or len(data) > 4 or self.sequence == 65535:
            raise ValueError("remote request bounds/session exhausted")
        seq = self.sequence + 1
        request = struct.pack("<BBIH", 0xA6, op, self.sid, seq) + data
        console = self.console
        for _ in range(4):
            console.wire.write(console.frame(0xF6, request))
            until = time.monotonic() + 2.5
            while time.monotonic() < until:
                console.pump()
                if "C6 shell bridge done" in console.text[self.bridge_start :]:
                    self.bridge_open = self.opened = False
                    raise ConnectionError("P4 closed the C6 shell bridge")
                while console.remote_incoming:
                    reply = console.remote_incoming.pop(0)
                    if len(reply) < 9 or reply[:8] != request[:8]:
                        continue
                    if len(reply) > 24 or reply[8] > 5:
                        raise ConnectionError("invalid C6 shell reply")
                    if reply[8] == 3:
                        break
                    if reply[8] == 5:
                        self.sequence = seq
                    if reply[8]:
                        raise ValueError("remote shell status " + str(reply[8]))
                    self.sequence = seq
                    return reply[9:]
        raise TimeoutError("C6 remote shell did not admit the request")

    def write(self, data):
        """Admit text in bounded chunks without interpreting console frame bytes."""
        for at in range(0, len(data), 4):
            self.request(2, data[at : at + 4])

    def read(self):
        """Read up to fifteen bytes, with exact-repeat recovery for a lost reply."""
        return self.request(3)

    def drain(self, quiet=0.2, timeout=15):
        """Drain output until the queue stays empty for the requested interval."""
        out = bytearray()
        until = time.monotonic() + timeout
        last = time.monotonic()
        while time.monotonic() < until:
            data = self.read()
            if data:
                out.extend(data)
                last = time.monotonic()
            elif time.monotonic() - last >= quiet:
                return out.decode(errors="replace")
        raise TimeoutError(
            "C6 output did not become idle: " + out[-300:].decode(errors="replace")
        )

    def command(self, text, timeout=15):
        """Write one line and return its output after it becomes idle."""
        self.write(text.encode() + b"\n")
        return self.drain(timeout=timeout)

    def close_bridge(self):
        """Release USB channel f6 and wait for the physical P4 prompt."""
        console = self.console
        if not self.bridge_open:
            return
        if "C6 shell bridge done" in console.text[self.bridge_start :]:
            self.bridge_open = False
            return
        start = len(console.text)
        console.wire.write(console.frame(0xF6, b"\0"))
        console.wait(
            lambda: "C6 shell bridge done" in console.text[start:]
            and "tikuOS:/> " in console.text[start:]
        )
        self.bridge_open = False

    def close(self):
        """Close the C6 session before returning the P4 console to its owner."""
        try:
            if self.opened:
                self.request(4)
                self.opened = False
        finally:
            self.close_bridge()


def main():
    """Run explicitly supplied shell commands against one identified board."""
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("port", "p4-serial", "peer-mac"):
        parser.add_argument("--" + key, required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--command", action="append", default=[])
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    console = Console(args.port, args.p4_serial, args.output)
    shell = None
    try:
        shell = RemoteShell(console, args.peer_mac)
        print("ESP32-C6 " + args.peer_mac + " build=" + str(shell.build), flush=True)
        print(shell.drain(), end="", flush=True)
        for command in args.command:
            print(shell.command(command), end="", flush=True)
    finally:
        try:
            if shell:
                shell.close()
        finally:
            console.close()


if __name__ == "__main__":
    main()
