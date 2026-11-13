#!/usr/bin/env python3
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32s31_ram.py - Load and qualify the S31 SRAM diagnostic over UART.
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import os
from pathlib import Path
import subprocess
import time


def validate_image(image):
    """Require an S31 image with entry and loaded segments in low HP SRAM."""
    if image.chip_id != 32 or not image.segments:
        raise ValueError("requires an ESP32-S31 image with loaded segments")
    for segment in image.segments:
        end = segment.addr + len(segment.data)
        if not (0x2F000000 <= segment.addr < end <= 0x2F00F000):
            raise ValueError("image segment exceeds diagnostic SRAM")
    if not any(s.addr <= image.entrypoint < s.addr + len(s.data)
               for s in image.segments):
        raise ValueError("entry is outside loaded image segments")


def validate_port(port, identity, ports):
    """Require the selected CP2102N identity and an unused Linux port."""
    matches = [p for p in ports if os.path.realpath(p.device) ==
               os.path.realpath(port)]
    if len(matches) != 1 or matches[0].serial_number != identity:
        raise ValueError("port does not match the requested bridge serial")
    if (matches[0].vid, matches[0].pid) != (0x10C4, 0xEA60):
        raise ValueError("requires a CP2102N UART bridge")
    result = subprocess.run(["fuser", port], capture_output=True)
    if result.returncode != 1:
        raise ValueError("port ownership check did not establish an unused port")


def capture(wire, seconds, prefix=b"TIMER", end_marker=None):
    """Capture UART bytes and host elapsed time between timer markers."""
    if end_marker is None:
        end_marker = prefix + b" end\r\n"
    data = bytearray()
    begin = finish = None
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        data.extend(wire.read(wire.in_waiting or 1))
        if begin is None and prefix + b" begin\r\n" in data:
            begin = time.monotonic()
        if finish is None and end_marker in data:
            finish = time.monotonic()
    elapsed = finish - begin if begin is not None and finish is not None else None
    return bytes(data), elapsed


def check_result(data, elapsed):
    """Check diagnostic counters and the one-second host timing interval."""
    required = (b"TIKU S31 RAM diagnostic v2\r\n", b"hart=0x00000000\r\n",
                b"sram_errors=0x00000000\r\n", b"context_errors=0x00000000\r\n",
                b"ecall_count=0x00000064\r\n",
                b"software_irq_count=0x00000064\r\n",
                b"timer_irq_count=0x000003e8\r\n",
                b"PASS S31 RAM diagnostic\r\n")
    return (all(marker in data for marker in required) and b"FAIL" not in data
            and elapsed is not None and 0.9 < elapsed < 1.1)


def check_kernel_result(data, elapsed):
    """Require completed worker and kernel assertions with a two-second run."""
    required = (b"TIKU S31 kernel test v1\r\n", b"failures=0x00000000\r\n",
                b"checks=0x00000010\r\n", b"worker_a=0x00000040\r\n",
                b"worker_b=0x00000040\r\n", b"PASS S31 kernel test\r\n")
    return (all(marker in data for marker in required) and b"FAIL" not in data
            and elapsed is not None and 1.8 < elapsed < 2.2)


def check_board_result(data, elapsed):
    """Require the kernel checks and successful touch polling at both bus rates."""
    return (check_kernel_result(data, elapsed) and
            data.count(b"touch_poll_errors=0x00000000\r\n") == 2 and
            data.count(b"touch_product=0x31313538\r\n") == 2 and
            b"i2c_errors=0x00000000\r\n" in data)


def check_shell_result(data, elapsed=None):
    """Require executed commands and a complete burst of distinct echo replies."""
    del elapsed
    required = [b"TIKU S31 SRAM shell v1\r\n", b"\r\n  63\r\n",
                b"process(es) registered", b"-- OK\r\n", b"Unknown command: nonexistent_s31"]
    required += [f"\r\nS31_BURST_{i:02d}\r\n".encode() for i in range(16)]
    return (all(marker in data for marker in required) and b"FAIL S31" not in data
            and data.count(b"TIKU S31 SRAM shell v1\r\n") == 1)


def capture_shell(wire, seconds):
    """Capture boot, then exercise the parser and interrupt-fed UART receive."""
    boot, _ = capture(wire, 1)
    if b"TIKU S31 SRAM shell v1\r\n" not in boot:
        return boot, None
    commands = b"help\r\ncalc 7 * 9\r\nps\r\nhtimer\r\nnonexistent_s31\r\n"
    commands += b"".join(f"echo S31_BURST_{i:02d}\r\n".encode() for i in range(16))
    wire.write(commands)
    reply, _ = capture(wire, seconds - 1)
    return boot + reply, None


def check_watchdog_result(data, elapsed):
    """Require feed/hold/off survival and a timed TG0 watchdog reset."""
    required = (b"TIKU S31 watchdog test v1\r\n", b"WDT feeds passed\r\n",
                b"WDT hold passed\r\n", b"WDT off passed\r\n",
                b"(HP_SYS_HP_WDT0_RESET)")
    return (all(marker in data for marker in required) and b"FAIL S31" not in data
            and elapsed is not None and 0.15 < elapsed < 0.4)


def check_psram_result(data, elapsed=None):
    """Require full capacity, two pattern passes and physical cache visibility."""
    del elapsed
    required = (b"TIKU S31 PSRAM test v1\r\n", b"psram_bytes=0x01000000\r\n",
                b"psram_stage=0x00000005\r\n", b"unflushed_stale_words=0x00000010\r\n",
                b"cache_errors=0x00000000\r\n", b"pattern_pass=0x00000000\r\n",
                b"pattern_pass=0x00000001\r\n", b"psram_errors=0x00000000\r\n",
                b"PASS S31 PSRAM\r\n")
    return (all(marker in data for marker in required) and b"FAIL" not in data
            and data.count(b"TIKU S31 PSRAM test v1\r\n") == 1)


def main():
    """Run requested warm-reset probes and restore the installed firmware."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--serial", required=True, help="CP2102N serial identity")
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--seconds", type=float, default=4)
    profile = parser.add_mutually_exclusive_group()
    profile.add_argument("--kernel-test", action="store_true")
    profile.add_argument("--board-test", action="store_true")
    profile.add_argument("--shell-test", action="store_true")
    profile.add_argument("--watchdog-test", action="store_true")
    profile.add_argument("--psram-test", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.runs <= 100 or not 3 <= args.seconds <= 60:
        parser.error("runs must be 1..100 and seconds must be 3..60")

    import esptool
    from esptool.bin_image import LoadFirmwareImage
    import serial
    from serial.tools import list_ports

    if esptool.__version__ != "5.6.0":
        raise ValueError("requires esptool==5.6.0")
    data = args.image.read_bytes()
    validate_image(LoadFirmwareImage("esp32s31", data))
    validate_port(args.port, args.serial, list(list_ports.comports()))
    summary_path = args.log.with_suffix(".json")
    factory_path = args.log.with_suffix(".factory.log")
    if summary_path.exists() or factory_path.exists():
        raise ValueError("output companion file already exists")
    results = []
    with args.log.open("xb") as log:
        wire = serial.Serial(port=None, baudrate=115200, timeout=0.05,
                             exclusive=True)
        wire.dtr = False
        wire.rts = False
        wire.port = args.port
        wire.open()
        esp = None
        try:
            for run in range(args.runs):
                esp = esptool.detect_chip(wire, connect_attempts=3)
                if esp.CHIP_NAME != "ESP32-S31":
                    raise ValueError("ROM reports a different chip")
                if (esp.get_major_chip_version(), esp.get_minor_chip_version()) != (0, 0):
                    raise ValueError("diagnostic is qualified for S31 revision 0.0")
                esptool.load_ram(esp, data)
                if args.shell_test:
                    received, elapsed = capture_shell(wire, args.seconds)
                elif args.watchdog_test:
                    received, elapsed = capture(wire, args.seconds, b"WDT", b"(HP_SYS_HP_WDT0_RESET)")
                else:
                    received, elapsed = capture(wire, args.seconds,
                                                b"KERNEL" if args.kernel_test or args.board_test else b"TIMER")
                log.write(f"RUN {run + 1}\n".encode() + received)
                log.flush()
                checker = (check_psram_result if args.psram_test else
                           check_watchdog_result if args.watchdog_test else
                           check_shell_result if args.shell_test else
                           check_board_result if args.board_test else
                           check_kernel_result if args.kernel_test else check_result)
                passed = checker(received, elapsed)
                if not args.watchdog_test and b"ESP-ROM:" in received:
                    passed = False
                results.append(dict(run=run + 1, passed=passed,
                                    timer_host_seconds=elapsed))
                print(json.dumps(results[-1]), flush=True)
                if not passed:
                    print(received.decode(errors="replace"), flush=True)
                    break
        finally:
            try:
                if esp is not None:
                    esp.hard_reset()
                    factory, _ = capture(wire, 4)
                    factory_path.write_bytes(factory)
            finally:
                wire.close()
                summary_path.write_text(json.dumps(results, indent=2) + "\n")
    return 0 if len(results) == args.runs and all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
