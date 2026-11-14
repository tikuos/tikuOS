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


def validate_image(image, radio=False):
    """Require an S31 image with entry and loaded segments in low HP SRAM."""
    if image.chip_id != 32 or not image.segments:
        raise ValueError("requires an ESP32-S31 image with loaded segments")
    limit = 0x2F01F000 if radio else 0x2F00F000
    for segment in image.segments:
        end = segment.addr + len(segment.data)
        if not (0x2F000000 <= segment.addr < end <= limit):
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


def capture(wire, seconds, prefix=b"TIMER", end_marker=None, stream=None):
    """Capture UART bytes and host elapsed time between timer markers."""
    if end_marker is None:
        end_marker = prefix + b" end\r\n"
    data = bytearray()
    begin = finish = None
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        chunk = wire.read(wire.in_waiting or 1)
        data.extend(chunk)
        if stream is not None and chunk:
            stream.write(chunk)
            stream.flush()
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


def check_memory_result(data, elapsed=None):
    """Require allocator placement, four DMA directions and fault recovery."""
    del elapsed
    required = (b"TIKU S31 memory test v1\r\n", b"psram_bytes=0x01000000\r\n",
                b"memory_errors=0x00000000\r\n", b"dma_cases=0x00000038\r\n",
                b"dma_context_refusals=0x00000002\r\n", b"dma_sustained_frames=0x00000020\r\n",
                b"dma_error_recovery=0x00000001\r\n", b"dma_timeout_recovery=0x00000001\r\n",
                b"PASS S31 memory\r\n")
    return (all(marker in data for marker in required) and b"FAIL" not in data
            and data.count(b"TIKU S31 memory test v1\r\n") == 1)


def check_display_result(data, elapsed=None, soak_seconds=0):
    """Require frame handoffs, pad values, timing and bounded fault recovery."""
    import re
    if not 0 <= soak_seconds <= 1800:
        return False
    if b"FAIL" in data or any(data.count(marker) != 1 for marker in
            (b"TIKU S31 display test v1\r\n", b"PASS S31 display\r\n")):
        return False
    fixed = {b"lcd_underruns": 0, b"lcd_dma_errors": 0, b"lcd_touch_errors": 0,
             b"lcd_updates": 8, b"lcd_refusals": 5, b"lcd_context_refusals": 2,
             b"lcd_fault_recovery": 1, b"display_errors": 0, b"lcd_pair_refusals": 5,
             b"lcd_flip_pad_errors": 0, b"lcd_flip_timeout": 1,
             b"lcd_flip_underruns": 0, b"lcd_flip_dma_errors": 0,
             b"lcd_soak_requested": soak_seconds}
    limits = [(name, value, value) for name, value in fixed.items()]
    limits += [(b"lcd_vsync", 95, 110), (b"lcd_eof", 95, 110),
               (b"lcd_timer_ticks", 1270, 1290), (b"lcd_touch_polls", 990, 1010),
               (b"lcd_pad_edges", 190, 205),
               (b"lcd_flips", max(16, soak_seconds * 2), max(16, soak_seconds * 10)),
               (b"lcd_flip_frames", max(16, soak_seconds * 9), max(100, soak_seconds * 11 + 50)),
               (b"lcd_flip_period_min_us", 98000, 106000),
               (b"lcd_flip_period_max_us", 98000, 106000),
               (b"lcd_flip_us_max", 1, 500000),
               (b"lcd_soak_seconds", max(1, soak_seconds), max(10, soak_seconds + 5))]
    for name, low, high in limits:
        values = re.findall(rb"(?:^|\r\n)" + name + rb"=0x([0-9a-f]{8})\r\n", data)
        if len(values) != 1 or not low <= int(values[0], 16) <= high:
            return False
    return not soak_seconds or (elapsed is not None and soak_seconds - 0.2 <= elapsed <= soak_seconds + 5)


def check_sd_result(data, elapsed=None, allow_no_response=False):
    """Require SPI qualification and card reads, or explicit no-response diagnostics."""
    import re
    del elapsed
    if b"FAIL" in data or data.count(b"TIKU S31 SD test v1\r\n") != 1:
        return False
    def value(name):
        values = re.findall(rb"(?:^|\r\n)" + name + rb"=0x([0-9a-f]{8})\r\n", data)
        return int(values[0], 16) if len(values) == 1 else -1
    fixed = {b"spi_cs_levels": 2, b"spi_cases": 10, b"spi_refusals": 5, b"spi_worker_refused": 1,
             b"spi_timeout": 1, b"sd_errors": 0}
    if any(value(name) != expected for name, expected in fixed.items()):
        return False
    card = b"PASS S31 SD card\r\n"
    no_response = b"PASS S31 SD no response\r\n"
    if value(b"sd_open_result") == 0xfffffffb:
        return (allow_no_response and data.count(no_response) == 1 and card not in data
                and value(b"sd_last_command") == 0 and value(b"sd_last_response") == 255
                and not re.search(rb"sd_(?:sectors|ocr|reads|media|partitions)", data))
    hi, lo = value(b"sd_sectors_hi"), value(b"sd_sectors_lo")
    sectors = (hi << 32) + lo
    ocr = value(b"sd_ocr")
    return (value(b"sd_open_result") == 0 and data.count(card) == 1 and no_response not in data
            and value(b"sd_last_command") == 10 and value(b"sd_last_response") == 0
            and hi >= 0 and lo >= 0 and 1024 <= sectors <= 1 << 32
            and sectors % 1024 == 0 and ocr >= 0 and ocr & 0xc0000000 == 0xc0000000
            and bool(ocr & 0x00300000) and 3 <= value(b"sd_reads") < 1000
            and value(b"sd_media_result") in (0, 0xfffffffd, 0xfffffffc)
            and 0 <= value(b"sd_table") <= 3 and 0 <= value(b"sd_partitions") <= 16)


def audio_pcm(data):
    """Decode the unique one-second stereo PCM16 excerpt, or return None."""
    import re
    begin, end = b"AUDIO PCM begin\r\n", b"AUDIO PCM end\r\n"
    if data.count(begin) != 1 or data.count(end) != 1:
        return None
    payload = data.split(begin)[1].split(end)[0]
    if not re.fullmatch(rb"(?:[0-9a-f]{128}\r\n){1000}", payload):
        return None
    return bytes.fromhex(payload.decode())


def check_phy_result(data, elapsed=None):
    """Require three PHY lifecycles and a tested entropy fault latch."""
    import re
    required = [b"TIKU S31 radio PHY test v1", b"phy_cycles=0x00000003",
                b"rng_changes=0x000003ff", b"phy_errors=0x00000000", b"PASS S31 radio PHY"]
    times = re.findall(rb"phy_cal_us=0x([0-9a-f]{8})\r\n", data)
    return (all(data.count(line + b"\r\n") == 1 for line in required)
            and len(times) == 3 and all(1000 <= int(t, 16) <= 1000000 for t in times)
            and b"FAIL" not in data and b"ESP-ROM" not in data)


def check_station_result(data):
    """Require three public-driver scans, receive interrupts and ownership restoration."""
    import re
    required = [b"TIKU S31 Wi-Fi test v1", b"wifi_cycles=0x00000003",
                b"wifi_errors=0x00000000", b"PASS S31 Wi-Fi"]
    aps = re.findall(rb"^wifi_scan_aps=0x([0-9a-f]{8})\r\n", data, re.MULTILINE)
    irqs = re.findall(rb"^wifi_scan_irqs=0x([0-9a-f]{8})\r\n", data, re.MULTILINE)
    return (all(data.count(line + b"\r\n") == 1 for line in required)
            and len(aps) == len(irqs) == 3 and all(0 < int(v, 16) <= 16 for v in aps)
            and all(int(v, 16) > 0 for v in irqs) and b"FAIL" not in data)


def check_station_data_result(data):
    """Require WPA2 association, DHCP and sixteen exact UDP echoes per lifecycle."""
    import re
    return (check_station_result(data) and
            all(re.findall(rb"^" + marker + rb"=0x([0-9a-f]{8})\r\n", data, re.MULTILINE)
                == [value] * 3 for marker, value in
                ((b"wifi_joined", b"00000001"), (b"wifi_dhcp", b"00000001"),
                 (b"wifi_udp_echoes", b"00000010"))))


def check_wifi_result(data, elapsed=None):
    """Require a native station scan and completed teardown, without claiming association."""
    import re
    if b"TIKU S31 Wi-Fi test v1" in data:
        return check_station_result(data)
    zero = [b"wifi_init", b"wifi_wpa", b"wifi_mode", b"wifi_start", b"wifi_scan_start",
            b"wifi_scan_num_rc", b"wifi_stop", b"wifi_deinit", b"wifi_osi_stop", b"wifi_heap_used"]
    required = [name + b"=0x00000000" for name in zero]
    required += [b"S31 native Wi-Fi probe", b"wifi_scan_done=0x00000001", b"Wi-Fi probe complete"]
    counts = re.findall(rb"(?:^|\r\n)wifi_ap_count=0x([0-9a-f]{8})\r\n", data)
    return (all(data.count(line + b"\r\n") == 1 for line in required)
            and len(counts) == 1 and 0 < int(counts[0], 16) <= 65535 and b"FAIL" not in data)


def check_radio_os_result(data, elapsed=None):
    """Require the radio OS lifecycle counters without claiming RF operation."""
    required = [b"TIKU S31 radio OS test v1", b"radio_worker_calls=0x00000003",
                b"radio_event_calls=0x00000003", b"radio_wifi_irqs=0x00000006",
                b"radio_bt_irqs=0x00000003", b"radio_os_errors=0x00000000",
                b"TIMER begin", b"TIMER end", b"PASS S31 radio OS"]
    return (all(data.count(line + b"\r\n") == 1 for line in required)
            and b"FAIL" not in data and b"ESP-ROM" not in data
            and elapsed is not None and 0.95 < elapsed < 1.10)


def check_audio_result(data, elapsed=None):
    """Require exact loopback, queue recovery, a timed minute and a checked PCM excerpt."""
    import re
    if b"FAIL" in data or b"ESP-ROM:" in data or elapsed is None or not 59.5 <= elapsed <= 60.5:
        return False
    markers = (b"TIKU S31 audio test v1\r\n", b"PASS S31 audio\r\n",
               b"AUDIO_RECORD begin\r\n", b"AUDIO_RECORD end\r\n")
    if any(data.count(marker) != 1 for marker in markers):
        return False
    def value(name):
        values = re.findall(rb"(?:^|\r\n)" + name + rb"=0x([0-9a-f]{8})\r\n", data)
        return int(values[0], 16) if len(values) == 1 else -1
    fixed = {b"audio_loop_frames": 16384, b"audio_loop_errors": 0,
             b"audio_exhaustion": 1, b"audio_refusals": 7, b"audio_worker_refused": 1,
             b"audio_irq_refused": 1, b"codec_open": 0, b"audio_capture_frames": 960000,
             b"amp_high": 128, b"amp_low": 0, b"audio_control_errors": 0, b"audio_blocks": 3782,
             b"audio_errors": 0, b"audio_rx_faults": 0, b"audio_tx_faults": 0, b"audio_timeouts": 0}
    if any(value(name) != expected for name, expected in fixed.items()):
        return False
    limits = [(b"audio_loop_us", 1020000, 1030000), (b"audio_capture_us", 59900000, 60100000)]
    for channel in (b"left", b"right"):
        limits += [(b"audio_" + channel + b"_nonzero", 1000, 960000),
                   (b"audio_" + channel + b"_peak", 1, 32768),
                   (b"audio_" + channel + b"_clipped", 0, 960000)]
    if any(not low <= value(name) <= high for name, low, high in limits):
        return False
    if value(b"audio_record_hash") < 0 or value(b"audio_record_hash") != value(b"audio_psram_hash"):
        return False
    pcm = audio_pcm(data)
    if pcm is None:
        return False
    checksum = 2166136261
    for byte in pcm:
        checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
    return checksum == value(b"audio_pcm_hash")


def main():
    """Run requested warm-reset probes and restore the installed firmware."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--serial", required=True, help="CP2102N serial identity")
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--seconds", type=float, default=4)
    parser.add_argument("--display-soak-seconds", type=int, default=0,
                        help="expected animation duration compiled into the display image")
    profile = parser.add_mutually_exclusive_group()
    profile.add_argument("--kernel-test", action="store_true")
    profile.add_argument("--board-test", action="store_true")
    profile.add_argument("--shell-test", action="store_true")
    profile.add_argument("--watchdog-test", action="store_true")
    profile.add_argument("--psram-test", action="store_true")
    profile.add_argument("--memory-test", action="store_true")
    profile.add_argument("--display-test", action="store_true")
    profile.add_argument("--sd-test", action="store_true")
    profile.add_argument("--audio-test", action="store_true")
    profile.add_argument("--radio-os-test", action="store_true")
    profile.add_argument("--phy-test", action="store_true")
    parser.add_argument("--allow-no-card-response", action="store_true",
                        help="accept SPI diagnostics without qualifying an SD card")
    args = parser.parse_args()
    if args.allow_no_card_response and not args.sd_test:
        parser.error("--allow-no-card-response requires --sd-test")
    if not 0 <= args.display_soak_seconds <= 1800:
        parser.error("display soak must be 0..1800 seconds")
    if args.display_soak_seconds and (not args.display_test or args.runs != 1):
        parser.error("display soak requires --display-test and one run")
    if args.audio_test and args.runs != 1:
        parser.error("audio qualification requires one run per log")
    minimum = 80 if args.audio_test else args.display_soak_seconds + 35 if args.display_soak_seconds else 3
    maximum = 120 if args.audio_test else args.display_soak_seconds + 60
    if not 1 <= args.runs <= 100 or not minimum <= args.seconds <= maximum:
        parser.error("runs must be 1..100; capture must be 3..60 seconds, audio 80..120, or soak duration plus 35..60")

    import esptool
    from esptool.bin_image import LoadFirmwareImage
    import serial
    from serial.tools import list_ports

    if esptool.__version__ != "5.6.0":
        raise ValueError("requires esptool==5.6.0")
    data = args.image.read_bytes()
    validate_image(LoadFirmwareImage("esp32s31", data), radio=args.radio_os_test or args.phy_test)
    validate_port(args.port, args.serial, list(list_ports.comports()))
    summary_path = args.log.with_suffix(".json")
    factory_path = args.log.with_suffix(".factory.log")
    wav_path = args.log.with_suffix(".wav")
    if summary_path.exists() or factory_path.exists() or (args.audio_test and wav_path.exists()):
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
                log.write(f"RUN {run + 1}\n".encode())
                if args.display_soak_seconds:
                    received, elapsed = capture(wire, args.seconds, b"LCD_SOAK", stream=log)
                elif args.audio_test:
                    received, elapsed = capture(wire, args.seconds, b"AUDIO_RECORD", stream=log)
                elif args.shell_test:
                    received, elapsed = capture_shell(wire, args.seconds)
                elif args.watchdog_test:
                    received, elapsed = capture(wire, args.seconds, b"WDT", b"(HP_SYS_HP_WDT0_RESET)")
                else:
                    received, elapsed = capture(wire, args.seconds,
                                                b"KERNEL" if args.kernel_test or args.board_test else b"TIMER")
                if not args.display_soak_seconds and not args.audio_test:
                    log.write(received)
                log.flush()
                checker = (check_phy_result if args.phy_test else check_radio_os_result if args.radio_os_test else
                           check_audio_result if args.audio_test else
                           check_display_result if args.display_test else
                           check_memory_result if args.memory_test else
                           check_psram_result if args.psram_test else
                           check_watchdog_result if args.watchdog_test else
                           check_shell_result if args.shell_test else
                           check_board_result if args.board_test else
                           check_kernel_result if args.kernel_test else check_result)
                passed = (check_display_result(received, elapsed, args.display_soak_seconds)
                          if args.display_test else
                          check_sd_result(received, elapsed, args.allow_no_card_response)
                          if args.sd_test else checker(received, elapsed))
                if not args.watchdog_test and b"ESP-ROM:" in received:
                    passed = False
                results.append(dict(run=run + 1, passed=passed,
                                    timer_host_seconds=elapsed))
                if args.audio_test and passed:
                    import wave
                    with wav_path.open("xb") as output:
                        with wave.open(output, "wb") as wav:
                            wav.setnchannels(2); wav.setsampwidth(2); wav.setframerate(16000)
                            wav.writeframes(audio_pcm(received))
                    results[-1]["wav_excerpt"] = str(wav_path)
                if args.sd_test:
                    results[-1]["card_qualified"] = check_sd_result(received)
                print(json.dumps(results[-1]), flush=True)
                if not passed:
                    preview = received.split(b"AUDIO PCM begin\r\n")[0] if args.audio_test else received
                    print(preview.decode(errors="replace"), flush=True)
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
