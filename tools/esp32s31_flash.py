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

import esp32s31_ram as ram


def validate_image(image, data):
    """Require two mapped regions, a low-SRAM entry and application metadata."""
    if image.chip_id != 32 or not 256 < len(data) <= 131072:
        raise ValueError("requires an S31 flash-shell image of at most 128 KiB")
    if data[32:36] != b"2T\xcd\xab":
        raise ValueError("missing first-segment application descriptor")
    mapped, loaded = [], []
    for segment in image.segments:
        end = segment.addr + len(segment.data)
        if segment.addr == 0:
            continue
        if 0x40000020 <= segment.addr < end <= 0x40020000:
            mapped.append(segment.addr)
        elif 0x2F000000 <= segment.addr < end <= 0x2F00F000:
            loaded.append(segment)
        else:
            raise ValueError("segment exceeds the flash-shell layout")
    if mapped != [0x40000020, 0x40010020]:
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


def check_boot(data, psram=False):
    """Require one completed native boot with no intervening ROM reset."""
    project = b"tikuos_s31_psram" if psram else b"tikuos_s31_shell"
    checker = ram.check_psram_result if psram else ram.check_shell_result
    return (checker(data) and data.count(b"ESP-ROM:esp32s31-") == 1 and
            data.count(b"Loaded app from partition at offset 0x10000") == 1 and
            b"flash_project=" + project in data)


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


def qualify(esptool, wire, image, backup, log, runs, seconds, state, journal, psram=False):
    """Temporarily replace application sectors; restore them after any test result."""
    span = (len(image) + 4095) & ~4095
    original, table = backup[0x10000:0x10000 + span], backup[0x8000:0x9000]
    esp = None
    try:
        esp = connect(esptool, wire)
        if (esptool.read_flash(esp, 0x10000, span, no_progress=True) != original or
                esptool.read_flash(esp, 0x8000, 4096, no_progress=True) != table):
            raise ValueError("board application sectors or partition table differ from backup")
        state["mutation_started"] = True
        save_journal(journal, state)
        esptool.write_flash(esp, [(0x10000, image)], flash_size="16MB")
        esptool.verify_flash(esp, [(0x10000, image)])
        for run in range(runs):
            wire.baudrate = 115200
            esp.hard_reset()
            data, _ = (ram.capture(wire, seconds) if psram else ram.capture_shell(wire, seconds))
            log.write(f"RUN {run + 1}\n".encode() + data)
            log.flush()
            passed = check_boot(data, psram)
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
    parser.add_argument("--psram-test", action="store_true")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--seconds", type=float, default=15)
    args = parser.parse_args()
    if not 1 <= args.runs <= 100 or not 4 <= args.seconds <= 60:
        parser.error("runs must be 1..100 and seconds must be 4..60")
    import esptool
    from esptool.bin_image import LoadFirmwareImage
    import serial
    from serial.tools import list_ports

    if esptool.__version__ != "5.6.0":
        raise ValueError("requires esptool==5.6.0")
    image, backup = args.image.read_bytes(), args.backup.read_bytes()
    validate_image(LoadFirmwareImage("esp32s31", image), image)
    validate_backup(backup, args.backup_sha256)
    ram.validate_port(args.port, args.serial, list(list_ports.comports()))
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
            qualify(esptool, wire, image, backup, log, args.runs, args.seconds, state, journal, args.psram_test)
        finally:
            wire.close()
    return 0 if (state["restored"] and state.get("factory_booted") and
                 len(state["boots"]) == args.runs and
                 all(b["passed"] for b in state["boots"])) else 1


if __name__ == "__main__":
    raise SystemExit(main())
