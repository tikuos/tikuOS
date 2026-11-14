#!/usr/bin/env python3
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32s31_flash.py - Qualify flash boot and restore a verified factory backup.
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import time

import esp32s31_ram as ram


def validate_image(image, data, radio=False, firmware=False):
    """Require two mapped regions, a low-SRAM entry and application metadata."""
    radio = radio or firmware
    limit = 1048576 if radio else 131072
    if image.chip_id != 32 or not 256 < len(data) <= limit:
        raise ValueError("requires an S31 flash image within the selected profile size")
    if data[32:36] != b"2T\xcd\xab":
        raise ValueError("missing first-segment application descriptor")
    mapped, loaded, ranges = [], [], []
    for segment in image.segments:
        end = segment.addr + len(segment.data)
        if segment.addr == 0:
            continue
        if radio and any(segment.addr < hi and lo < end for lo, hi in ranges):
            raise ValueError("radio image segments overlap")
        ranges.append((segment.addr, end))
        if radio and segment.addr == 0x40000020 and end > 0x40020000:
            raise ValueError("radio constants exceed their flash mapping")
        if 0x40000020 <= segment.addr < end <= (0x400A0000 if radio else 0x40020000):
            mapped.append(segment.addr)
        elif 0x2F000000 <= segment.addr < end <= (0x2F05E000 if firmware else 0x2F03F000 if radio else 0x2F00F000):
            loaded.append(segment)
        else:
            raise ValueError("segment exceeds the flash-shell layout")
    if mapped != [0x40000020, 0x40020020 if radio else 0x40010020]:
        raise ValueError("factory bootloader requires both mapped segments")
    if not any(s.addr <= image.entrypoint < s.addr + len(s.data) for s in loaded):
        raise ValueError("entry is outside loaded SRAM")


def validate_backup(backup, digest):
    """Require a hash-matched 16 MiB backup with the Korvo factory partition."""
    if len(backup) != 16 * 1024 * 1024 or hashlib.sha256(backup).hexdigest() != digest:
        raise ValueError("factory backup size or SHA256 mismatch")
    entries = [struct.unpack_from("<HBBII16sI", backup, offset)
               for offset in range(0x8000, 0x8C00, 32)]
    apps = [e for e in entries if e[0] == 0x50AA and e[1] == 0]
    if len(apps) != 1 or apps[0][2:5] != (0, 0x10000, 0x800000):
        raise ValueError("requires the Korvo factory application partition")


def check_boot(data, psram=False, memory=False, display=False, radio=False, radio_data=False, firmware=False):
    """Require one completed native boot with no intervening ROM reset."""
    project = (b"tikuos_s31_wifi" if firmware else b"tikuos_s31_radio" if radio else b"tikuos_s31_display" if display else b"tikuos_s31_memory" if memory else
               b"tikuos_s31_psram" if psram else b"tikuos_s31_shell")
    checker = (check_firmware if firmware else ram.check_wifi_result if radio else ram.check_display_result if display else ram.check_memory_result if memory else
               ram.check_psram_result if psram else ram.check_shell_result)
    return (checker(data) and (not radio_data or ram.check_station_data_result(data)) and data.count(b"ESP-ROM:esp32s31-") == 1 and
            data.count(b"Loaded app from partition at offset 0x10000") == 1 and
            b"flash_project=" + project in data)


def check_firmware(data):
    """Require shell operation, both AP security modes and the AP IP service."""
    return (ram.check_shell_result(data) and b"FATAL" not in data and
            b"AP: up channel=6 clients=0 auth=WPA2" in data and
            b"AP: up channel=6 clients=0 auth=SAE/PMF" in data and
            b"wifi: AP IP service at 192.168.4.1" in data and
            b"\r\nS31_WIFI_END\r\n" in data)


def capture_firmware(wire, seconds):
    """Exercise normal shell commands using public fixture credentials."""
    data, _ = ram.capture_shell(wire, seconds)
    for command, delay in [
        (b"wifi on", 3), (b"wifi scan", 7), (b"wifi list", 1),
        (b"wifi ap start S31-Native-Test test-passphrase 6", 10),
        (b"wifi ap status", 1), (b"wifi up", 1), (b"wifi ap stop", 2),
        (b"wifi ap start3 S31-Native-SAE test-passphrase 6", 15),
        (b"wifi ap status", 1), (b"wifi off", 2), (b"echo S31_WIFI_END", 1),
    ]:
        wire.write(command + b"\r\n")
        chunk, _ = ram.capture(wire, delay)
        data += chunk
    return data, None


def capture_settings(wire, seconds, run):
    """Save on the first boot; reject a wrong key, unlock and forget on the second."""
    data, _ = ram.capture_shell(wire, seconds)
    commands = ([
        (b"wifi saved", 1), (b"wifi on", 3),
        (b"wifi ap start3 S31-Native-SAE test-passphrase 6", 15),
        (b"wifi ap status", 1), (b"wifi save s31-test-vault-passphrase", 1),
        (b"wifi saved", 1),
    ] if run == 0 else [
        (b"wifi saved", 1), (b"wifi unlock s31-wrong-vault-passphrase", 1),
        (b"wifi status", 1), (b"wifi unlock s31-test-vault-passphrase", 10),
        (b"wifi ap status", 1), (b"wifi forget", 3), (b"wifi saved", 1),
    ])
    for command, delay in commands + [(b"echo S31_WIFI_END", 1)]:
        wire.write(command + b"\r\n")
        if command.startswith((b"wifi save ", b"wifi unlock ")):
            result, deadline = b"", time.monotonic() + 180
            while time.monotonic() < deadline and b"wifi: profile " not in result and b"FATAL" not in result:
                chunk, _ = ram.capture(wire, 1)
                result += chunk
            data += result
        chunk, _ = ram.capture(wire, delay)
        data += chunk
    return data, None


def check_settings(data, run):
    """Require an authenticated profile lifecycle and completed shell operation."""
    common = (ram.check_shell_result(data) and b"FATAL" not in data and
              data.count(b"ESP-ROM:esp32s31-") == 1 and
              data.count(b"Loaded app from partition at offset 0x10000") == 1 and
              b"flash_project=tikuos_s31_wifi" in data and
              b"AP: up channel=6 clients=0 auth=SAE/PMF" in data and
              b"wifi: profile complete (rc=0)" in data and
              b"\r\nS31_WIFI_END\r\n" in data)
    if run == 0:
        return common and b"Saved: encrypted S31-Native-SAE mode=ap channel=6 store_result=0" in data
    return (common and b"Saved: encrypted (locked)" in data and
            b"wifi: profile failed (rc=-4)" in data and
            b"State:       down" in data.split(b"wifi: profile failed (rc=-4)", 1)[1].split(b"wifi: profile complete", 1)[0] and
            b"Saved: none mode=station channel=0 store_result=0" in data and
            b"wifi: stored credentials cleared" in data)


def save_journal(path, state):
    """Persist the restoration state before flash mutation and after verification."""
    with path.open("w") as stream:
        json.dump(state, stream, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())


def connect(esptool, wire):
    """Identify unprotected S31 revision 0.0 before loading the flash stub."""
    wire.baudrate = 115200
    chip = esptool.detect_chip(wire, connect_attempts=3)
    if (chip.CHIP_NAME != "ESP32-S31" or
            (chip.get_major_chip_version(), chip.get_minor_chip_version()) != (0, 0)):
        raise ValueError("requires ESP32-S31 revision 0.0")
    if chip.get_secure_boot_enabled() or chip.get_flash_encryption_enabled():
        raise ValueError("requires unencrypted flash with secure boot disabled")
    chip = esptool.run_stub(chip)
    chip.change_baud(460800)
    return chip


def qualify(esptool, wire, image, backup, log, runs, seconds, state, journal, psram=False, memory=False, display=False, radio=False, radio_data=False, settings=False, firmware=False, peer=None):
    """Temporarily replace application sectors; restore them after any test result."""
    span = (len(image) + 4095) & ~4095
    original, table = backup[0x10000:0x10000 + span], backup[0x8000:0x9000]
    esp = None
    try:
        esp = connect(esptool, wire)
        if (esptool.read_flash(esp, 0x10000, span, no_progress=True) != original or
                esptool.read_flash(esp, 0x8000, 4096, no_progress=True) != table):
            raise ValueError("board application sectors or partition table differ from backup")
        if settings:
            tail = backup[0xffe000:0x1000000]
            if tail != b"\xff" * 8192 or esptool.read_flash(esp, 0xffe000, 8192, no_progress=True) != tail:
                raise ValueError("settings qualification requires the erased factory reservation")
            state["settings_restored"] = False
        state["mutation_started"] = True
        save_journal(journal, state)
        esptool.write_flash(esp, [(0x10000, image)], flash_size="16MB")
        esptool.verify_flash(esp, [(0x10000, image)])
        for run in range(runs):
            wire.baudrate = 115200
            esp.hard_reset()
            if peer:
                try:
                    data, _ = peer.capture(wire, seconds)
                except Exception as error:
                    log.write(bytes(getattr(peer, "console", b"")))
                    log.flush()
                    state["peer_error"] = str(error)
                    raise
                finally:
                    state["peer"] = peer.results
                    save_journal(journal, state)
            else:
                data, _ = (capture_settings(wire, seconds, run) if firmware and settings else capture_firmware(wire, seconds) if firmware else ram.capture(wire, seconds) if psram or memory or display or radio else ram.capture_shell(wire, seconds))
            log.write(f"RUN {run + 1}\n".encode() + data)
            log.flush()
            passed = check_settings(data, run) if firmware and settings else check_boot(data, psram, memory, display, radio, radio_data, firmware)
            if peer:
                passed = passed and peer.passed()
            state["boots"].append(dict(run=run + 1, passed=passed))
            save_journal(journal, state)
            print(json.dumps(state["boots"][-1]), flush=True)
            if not passed:
                break
    finally:
        if state["mutation_started"]:
            for attempt in range(3):
                try:
                    esp = connect(esptool, wire)
                    esptool.write_flash(esp, [(0x10000, original)], flash_size="16MB")
                    esptool.verify_flash(esp, [(0x10000, original), (0x8000, table)])
                    if settings:
                        esptool.write_flash(esp, [(0xffe000, tail)], flash_size="16MB")
                        esptool.verify_flash(esp, [(0xffe000, tail)])
                        state["settings_restored"] = True
                    state["restored"] = True
                    save_journal(journal, state)
                    break
                except Exception as error:
                    state["restore_error"] = str(error)
                    save_journal(journal, state)
                    if attempt == 2:
                        raise RuntimeError("factory restoration failed; use the recorded backup") from error
        if esp is not None:
            wire.baudrate = 115200
            esp.hard_reset()
            data, _ = ram.capture(wire, 5)
            log.write(b"RESTORED FIRMWARE\n" + data)
            state["factory_booted"] = b"factory_demo:" in data and b"is ready" in data
            save_journal(journal, state)


def main():
    """Check identity and backups, run warm boots, and leave factory firmware installed."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--backup-sha256", required=True)
    parser.add_argument("--log", type=Path, required=True)
    profile = parser.add_mutually_exclusive_group()
    profile.add_argument("--psram-test", action="store_true")
    profile.add_argument("--memory-test", action="store_true")
    profile.add_argument("--display-test", action="store_true")
    profile.add_argument("--radio-test", action="store_true")
    profile.add_argument("--firmware-test", action="store_true")
    profile.add_argument("--ap-peer-test", action="store_true")
    parser.add_argument("--peer-interface")
    parser.add_argument("--allow-uplink-interruption", action="store_true",
                        help="permit temporary use of an already connected peer Wi-Fi adapter")
    parser.add_argument("--settings-test", action="store_true",
                        help="with --firmware-test --runs 2, qualify and restore encrypted settings")
    parser.add_argument("--wifi-data-required", action="store_true",
                        help="require WPA2 association, DHCP and UDP echoes in each radio cycle")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--seconds", type=float, default=15)
    args = parser.parse_args()
    if args.ap_peer_test:
        if args.runs != 1 or not args.peer_interface or args.settings_test:
            parser.error("--ap-peer-test requires --runs 1 --peer-interface and no settings test")
        args.firmware_test = True
    elif args.peer_interface or args.allow_uplink_interruption:
        parser.error("peer options require --ap-peer-test")
    if args.settings_test and (not args.firmware_test or args.runs != 2):
        parser.error("--settings-test requires --firmware-test --runs 2")
    if args.wifi_data_required and not args.radio_test:
        parser.error("--wifi-data-required requires --radio-test")
    if not 1 <= args.runs <= 100 or not 4 <= args.seconds <= (180 if args.radio_test else 60):
        parser.error("runs must be 1..100; seconds 4..60, or 4..180 for radio tests")
    import esptool
    from esptool.bin_image import LoadFirmwareImage
    import serial
    from serial.tools import list_ports

    if esptool.__version__ != "5.6.0":
        raise ValueError("requires esptool==5.6.0")
    image, backup = args.image.read_bytes(), args.backup.read_bytes()
    validate_image(LoadFirmwareImage("esp32s31", image), image, args.radio_test, args.firmware_test)
    validate_backup(backup, args.backup_sha256)
    ram.validate_port(args.port, args.serial, list(list_ports.comports()))
    peer = None
    if args.ap_peer_test:
        from esp32s31_wifi_peer import Peer
        peer = Peer(args.peer_interface, args.allow_uplink_interruption)
    journal = args.log.with_suffix(".json")
    state = dict(offset=0x10000, span=(len(image) + 4095) & ~4095,
                 image_sha256=hashlib.sha256(image).hexdigest(),
                 backup=str(args.backup.resolve()), backup_sha256=args.backup_sha256,
                 mutation_started=False, restored=False, boots=[])
    with args.log.open("xb") as log, journal.open("x"):
        save_journal(journal, state)
        wire = serial.Serial(port=None, baudrate=115200, timeout=0.05, exclusive=True)
        wire.dtr = wire.rts = False
        wire.port = args.port
        try:
            wire.open()
            qualify(esptool, wire, image, backup, log, args.runs, args.seconds, state, journal, args.psram_test, args.memory_test, args.display_test, args.radio_test, args.wifi_data_required, settings=args.settings_test, firmware=args.firmware_test, peer=peer)
        finally:
            wire.close()
    return 0 if (state["restored"] and state.get("factory_booted") and
                 len(state["boots"]) == args.runs and
                 all(b["passed"] for b in state["boots"])) else 1


if __name__ == "__main__":
    raise SystemExit(main())
