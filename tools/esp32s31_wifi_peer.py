#!/usr/bin/env python3
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32s31_wifi_peer.py - NetworkManager client qualification and uplink restoration.
#
# SPDX-License-Identifier: Apache-2.0

import ipaddress
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import threading
import uuid

import esp32s31_ram as ram

PASSWORD = "s31-peer-test-password"


def command(args, timeout=15):
    """Run a bounded command, returning output or raising with its diagnostic."""
    result = subprocess.run(args, capture_output=True, text=True, timeout=timeout,
                            env=os.environ | {"LC_ALL": "C"})
    if result.returncode:
        raise RuntimeError(f"{args[0]} exited {result.returncode}: {result.stderr.strip()}")
    return result.stdout.strip()


def active_connection(interface):
    """Return the device's active connection UUID, or an empty string."""
    value = command(["nmcli", "-g", "GENERAL.CON-UUID", "device", "show", interface])
    if value in ("", "--"):
        return ""
    return str(uuid.UUID(value))


def restore(interface, original):
    """Restore and verify the saved connection; retry transient activation errors."""
    for attempt in range(3):
        try:
            if active_connection(interface) == original:
                return
            if original:
                command(["nmcli", "--wait", "30", "connection", "up", "uuid", original,
                         "ifname", interface], 35)
            else:
                command(["nmcli", "--wait", "10", "device", "disconnect", interface])
            if active_connection(interface) == original:
                return
        except (RuntimeError, subprocess.TimeoutExpired):
            if attempt == 2:
                raise
    raise RuntimeError("host Wi-Fi restoration could not be verified")


def watchdog(interface, original, deadline):
    """Restore on parent exit or timeout; accept disarm only after verified restoration."""
    ready, _, _ = select.select([sys.stdin.buffer], [], [], deadline)
    message = sys.stdin.buffer.readline() if ready else b""
    if message == b"restored\n" and active_connection(interface) == original:
        return
    restore(interface, original)


def collect(wire, operation, sink):
    """Drain the board UART while a bounded host operation runs."""
    data, stopped = bytearray(), threading.Event()
    errors = []

    def read():
        try:
            while not stopped.is_set():
                data.extend(wire.read(4096))
        except Exception as error:
            errors.append(error)

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    try:
        result = operation()
    finally:
        stopped.set()
        reader.join(timeout=2)
        sink.extend(data)
    if reader.is_alive() or errors:
        raise RuntimeError("peer test lost the board console")
    return result


class Peer:
    """A transient client fixture whose network changes require explicit opt-in."""

    def __init__(self, interface, allow_uplink_interruption=False):
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,15}", interface):
            raise ValueError("invalid peer interface name")
        self.interface = interface
        self.original = active_connection(interface)
        if self.original and not allow_uplink_interruption:
            raise ValueError("peer interface is connected; --allow-uplink-interruption is required")
        self.results = []
        self.ssid = "S31-Peer-" + uuid.uuid4().hex[:8]

    def exercise(self, security):
        """Associate, obtain a lease, exchange ICMP and reconnect the saved network."""
        if active_connection(self.interface) != self.original:
            raise RuntimeError("peer network changed since preflight")
        profile = str(uuid.uuid4())
        keeper = None
        created = False
        record = {"security": security, "passed": False, "network_restored": False}
        self.results.append(record)
        try:
            # The child uses its own session so a failed parent cannot cancel recovery.
            keeper = subprocess.Popen([sys.executable, str(Path(__file__).resolve()),
                "--watchdog", self.interface, self.original, "120"],
                stdin=subprocess.PIPE, start_new_session=True)
            command(["nmcli", "connection", "add", "save", "no", "type", "wifi",
                "ifname", self.interface, "con-name", "tiku-s31-peer-" + profile,
                "connection.uuid", profile, "connection.autoconnect", "no",
                "802-11-wireless.ssid", self.ssid, "802-11-wireless.mode", "infrastructure",
                "802-11-wireless-security.key-mgmt", "sae" if security == "WPA3" else "wpa-psk",
                "802-11-wireless-security.pmf", "3" if security == "WPA3" else "1",
                "802-11-wireless-security.psk", PASSWORD,
                "ipv4.method", "auto", "ipv4.never-default", "yes", "ipv6.method", "disabled"])
            created = True
            command(["nmcli", "--wait", "40", "connection", "up", "uuid", profile,
                     "ifname", self.interface], 45)
            if active_connection(self.interface) != profile:
                raise RuntimeError("client did not activate the fixture profile")
            addresses = command(["nmcli", "-g", "IP4.ADDRESS", "device", "show", self.interface])
            leases = [ipaddress.ip_interface(line) for line in addresses.splitlines() if line]
            if not any(a.network == ipaddress.ip_network("192.168.4.0/24") and
                       a.ip != ipaddress.ip_address("192.168.4.1") for a in leases):
                raise RuntimeError("client did not obtain an S31 DHCP lease")
            ping = command(["ping", "-n", "-I", self.interface, "-c", "8", "-i", "0.25",
                            "-W", "2", "192.168.4.1"], 25)
            if not re.search(r"8 packets transmitted, 8 received, 0% packet loss", ping):
                raise RuntimeError("client ICMP exchange lost packets: " + ping)
            record.update(passed=True, received=8, lease=str(leases[0]))
        finally:
            try:
                restore(self.interface, self.original)
                record["network_restored"] = True
                if keeper:
                    keeper.stdin.write(b"restored\n")
                    keeper.stdin.flush()
                    keeper.stdin.close()
                    keeper.wait(timeout=5)
            finally:
                # EOF asks an armed child to restore immediately after main-path failure.
                if keeper and keeper.poll() is None and keeper.stdin and not keeper.stdin.closed:
                    keeper.stdin.close()
                if created and record["network_restored"]:
                    command(["nmcli", "connection", "delete", "uuid", profile])

    def capture(self, wire, seconds):
        """Exercise WPA2 and SAE/required-PMF AP clients from normal S31 firmware."""
        data, _ = ram.capture_shell(wire, seconds)
        self.console = bytearray(data)
        for security in ("WPA2", "WPA3"):
            phase_start = len(self.console)
            for cmd, delay in [("wifi on", 3),
                               (f"wifi ap {'start3' if security == 'WPA3' else 'start'} {self.ssid} {PASSWORD} 6", 15),
                               ("wifi up", 1), ("wifi ap status", 1)]:
                wire.write(cmd.encode() + b"\r\n")
                chunk, _ = ram.capture(wire, delay)
                self.console.extend(chunk)
            expected = b"auth=SAE/PMF" if security == "WPA3" else b"auth=WPA2"
            phase = self.console[phase_start:]
            if (b"AP: up channel=6 clients=0 " + expected not in phase or
                    b"wifi: AP IP service at 192.168.4.1" not in phase):
                raise RuntimeError("S31 AP did not become ready; host network retained")
            collect(wire, lambda: self.exercise(security), self.console)
            wire.write(b"wifi off\r\n")
            chunk, _ = ram.capture(wire, 3)
            self.console.extend(chunk)
        wire.write(b"echo S31_WIFI_END\r\n")
        chunk, _ = ram.capture(wire, 1)
        self.console.extend(chunk)
        return bytes(self.console), None

    def passed(self):
        """Require both independent client tests and verified host restoration."""
        return ([r["security"] for r in self.results] == ["WPA2", "WPA3"] and
                all(r["passed"] and r["network_restored"] for r in self.results))


if __name__ == "__main__":
    if len(sys.argv) != 5 or sys.argv[1] != "--watchdog":
        raise SystemExit("Use esp32s31_flash.py --ap-peer-test")
    watchdog(sys.argv[2], sys.argv[3], int(sys.argv[4]))
