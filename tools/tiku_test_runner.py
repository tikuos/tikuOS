#!/usr/bin/env python3
"""
TikuOS Test Runner

Interactive tool for selecting, building, flashing, and parsing test results
from MSP430-based TikuOS firmware over UART.

Usage:
    python3 tools/tiku_test_runner.py
    python3 tools/tiku_test_runner.py --mcu msp430fr5969 --port /dev/ttyACM1
    python3 tools/tiku_test_runner.py --list
    python3 tools/tiku_test_runner.py --category matrix --no-interactive

Authors: Ambuj Varshney <ambuj@tiku-os.org>
SPDX-License-Identifier: Apache-2.0
"""

import argparse
import os
import re
import subprocess
import sys
import time

# ---------------------------------------------------------------------------
# Project paths (relative to repo root)
# ---------------------------------------------------------------------------

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJ_DIR = os.path.dirname(SCRIPT_DIR)
CONFIG_FILE = os.path.join(PROJ_DIR, "tests", "tiku_test_config.h")
TIKUKITS_DIR = os.path.join(PROJ_DIR, "tikukits")

# ---------------------------------------------------------------------------
# ANSI colors
# ---------------------------------------------------------------------------

class C:
    BOLD    = "\033[1m"
    GREEN   = "\033[92m"
    RED     = "\033[91m"
    YELLOW  = "\033[93m"
    CYAN    = "\033[96m"
    BLUE    = "\033[94m"
    DIM     = "\033[2m"
    RESET   = "\033[0m"

def supports_color():
    return hasattr(sys.stdout, "isatty") and sys.stdout.isatty()

if not supports_color():
    for attr in dir(C):
        if not attr.startswith("_"):
            setattr(C, attr, "")

# ---------------------------------------------------------------------------
# Test category definitions
# ---------------------------------------------------------------------------

CATEGORIES = [
    # --- Core OS tests ---
    {
        "key": "watchdog",
        "name": "Watchdog Timer",
        "description": "Basic kick, pause/resume, interval timer mode",
        "flags": [
            "TEST_WATCHDOG", "TEST_WDT_BASIC", "TEST_WDT_PAUSE_RESUME",
            "TEST_WDT_INTERVAL",
        ],
        "note": "TEST_WDT_TIMEOUT excluded (causes device reset)",
        "requires_tikukits": False,
    },
    {
        "key": "watchdog-timeout",
        "name": "Watchdog Timeout (DESTRUCTIVE)",
        "description": "Intentionally does NOT kick watchdog - device will reset",
        "flags": ["TEST_WATCHDOG", "TEST_WDT_TIMEOUT"],
        "note": "WARNING: device will reset during test",
        "requires_tikukits": False,
    },
    {
        "key": "cpuclock",
        "name": "CPU Clock Output",
        "description": "Exposes CPU clock on P3.4 for oscilloscope measurement",
        "flags": ["TEST_CPUCLOCK"],
        "requires_tikukits": False,
    },
    {
        "key": "process",
        "name": "Process / Protothread",
        "description": "Lifecycle, events, yield, broadcast, poll, queue, local storage, exit",
        "flags": [
            "TEST_PROCESS_LIFECYCLE", "TEST_PROCESS_EVENTS",
            "TEST_PROCESS_YIELD", "TEST_PROCESS_BROADCAST",
            "TEST_PROCESS_POLL", "TEST_PROCESS_QUEUE",
            "TEST_PROCESS_LOCAL", "TEST_PROCESS_BROADCAST_EXIT",
            "TEST_PROCESS_GRACEFUL_EXIT", "TEST_PROCESS_CURRENT_CLEARED",
        ],
        "requires_tikukits": False,
    },
    {
        "key": "timer",
        "name": "Timer Subsystem",
        "description": "Event timer, callback, periodic, stop, hardware timer",
        "flags": [
            "TEST_TIMER", "TEST_TIMER_EVENT", "TEST_TIMER_CALLBACK",
            "TEST_TIMER_PERIODIC", "TEST_TIMER_STOP",
            "TEST_HTIMER_BASIC", "TEST_HTIMER_PERIODIC",
        ],
        "requires_tikukits": False,
    },
    {
        "key": "arena",
        "name": "Arena Allocator",
        "description": "Create, alloc, alignment, full, reset, peak, secure wipe, multi-arena",
        "flags": [
            "TEST_MEM_CREATE", "TEST_MEM_ALLOC", "TEST_MEM_ALIGNMENT",
            "TEST_MEM_FULL", "TEST_MEM_RESET", "TEST_MEM_PEAK",
            "TEST_MEM_INVALID", "TEST_MEM_SECURE_RESET",
            "TEST_MEM_TWO_ARENAS",
        ],
        "requires_tikukits": False,
    },
    {
        "key": "persist",
        "name": "Persistent FRAM Store",
        "description": "Init, register, read/write, delete, full, wear, reboot survival",
        "flags": [
            "TEST_PERSIST_INIT", "TEST_PERSIST_REGISTER",
            "TEST_PERSIST_WRITE_READ", "TEST_PERSIST_SMALL_BUF",
            "TEST_PERSIST_OVERFLOW", "TEST_PERSIST_NOT_FOUND",
            "TEST_PERSIST_DELETE", "TEST_PERSIST_FULL",
            "TEST_PERSIST_REBOOT", "TEST_PERSIST_POWERCYCLE",
            "TEST_PERSIST_WEAR", "TEST_PERSIST_DUP_KEY",
        ],
        "requires_tikukits": False,
    },
    {
        "key": "mpu",
        "name": "MPU Write Protection",
        "description": "Init, lock/unlock, permissions, scoped write, violation detect",
        "flags": [
            "TEST_MPU_INIT", "TEST_MPU_UNLOCK_LOCK", "TEST_MPU_SET_PERM",
            "TEST_MPU_SCOPED", "TEST_MPU_IDEMPOTENT",
            "TEST_MPU_ALL_SEGMENTS", "TEST_MPU_PERM_FLAGS",
            "TEST_MPU_REINIT", "TEST_MPU_UNLOCK_CUSTOM",
            "TEST_MPU_SCOPED_CUSTOM", "TEST_MPU_VIOLATION",
        ],
        "requires_tikukits": False,
    },
    {
        "key": "pool",
        "name": "Pool Allocator",
        "description": "Create, alloc/free, exhaustion, LIFO, peak, reset, poisoning",
        "flags": [
            "TEST_POOL_CREATE", "TEST_POOL_ALLOC_FREE",
            "TEST_POOL_EXHAUSTION", "TEST_POOL_FREE_RANGE",
            "TEST_POOL_FREE_ALIGN", "TEST_POOL_REALLOC",
            "TEST_POOL_PEAK", "TEST_POOL_RESET",
            "TEST_POOL_INVALID", "TEST_POOL_TWO_POOLS",
            "TEST_POOL_BLOCK_ALIGN", "TEST_POOL_STATS",
            "TEST_POOL_POISON", "TEST_POOL_WITHIN_BUF",
        ],
        "requires_tikukits": False,
    },
    {
        "key": "region",
        "name": "Region Registry",
        "description": "Init, contains, boundary, overflow, claim/unclaim, type lookup",
        "flags": [
            "TEST_REGION_INIT", "TEST_REGION_INIT_INVALID",
            "TEST_REGION_CONTAINS", "TEST_REGION_WRONG_TYPE",
            "TEST_REGION_BOUNDARY", "TEST_REGION_OVERFLOW",
            "TEST_REGION_CLAIM", "TEST_REGION_CLAIM_OVERLAP",
            "TEST_REGION_CLAIM_UNKNOWN", "TEST_REGION_CLAIM_FULL",
            "TEST_REGION_GET_TYPE", "TEST_REGION_NOT_FOUND",
        ],
        "requires_tikukits": False,
    },
    # --- TikuKits tests ---
    {
        "key": "matrix",
        "name": "TikuKits: Matrix / Linear Algebra",
        "description": "Init, identity, get/set, add/sub, multiply, scale, det, trace",
        "flags": ["TEST_KITS_MATRIX"],
        "requires_tikukits": True,
    },
    {
        "key": "statistics",
        "name": "TikuKits: Statistics",
        "description": "Windowed stats, Welford variance, min/max, EWMA, energy, isqrt",
        "flags": ["TEST_KITS_STATISTICS"],
        "requires_tikukits": True,
    },
    {
        "key": "distance",
        "name": "TikuKits: Distance Metrics",
        "description": "Manhattan, Euclidean, dot product, cosine similarity, Hamming",
        "flags": ["TEST_KITS_DISTANCE"],
        "requires_tikukits": True,
    },
    {
        "key": "sensor",
        "name": "TikuKits: Sensor Interface",
        "description": "Fractional conversion helpers, sensor name lookups",
        "flags": ["TEST_KITS_SENSOR"],
        "requires_tikukits": True,
    },
    {
        "key": "sigfeatures",
        "name": "TikuKits: Signal Features",
        "description": "Peak, ZCR, histogram, delta, Goertzel, z-score, scale",
        "flags": ["TEST_KITS_SIGFEATURES"],
        "requires_tikukits": True,
    },
    {
        "key": "textcompression",
        "name": "TikuKits: Text Compression",
        "description": "RLE, byte-pair encoding, heatshrink round-trip",
        "flags": ["TEST_KITS_TEXTCOMPRESSION"],
        "requires_tikukits": True,
    },
    {
        "key": "ml-linreg",
        "name": "TikuKits: ML Linear Regression",
        "description": "Streaming OLS fit, slope, intercept, predict, R-squared, edge cases",
        "flags": ["TEST_KITS_ML_LINREG"],
        "requires_tikukits": True,
    },
]

# Flags that are auto-derived (computed via || expressions) — never touch these
AUTO_DERIVED_FLAGS = {
    "TEST_PROCESS", "TEST_MEM", "TEST_PERSIST", "TEST_MPU",
    "TEST_POOL", "TEST_REGION", "TEST_KITS_MATHS", "TEST_KITS_ML",
    "TEST_KITS",
}

# All settable flags (union of all category flags)
ALL_SETTABLE_FLAGS = set()
for cat in CATEGORIES:
    ALL_SETTABLE_FLAGS.update(cat["flags"])

# ---------------------------------------------------------------------------
# Config file manipulation
# ---------------------------------------------------------------------------

# Matches: #define TEST_SOMETHING 0  or  #define TEST_SOMETHING 1
FLAG_RE = re.compile(r"^(\s*#define\s+)(TEST_\w+)(\s+)([01])(\s*.*)$")


def read_config():
    """Read the config file and return its lines."""
    with open(CONFIG_FILE, "r") as f:
        return f.readlines()


def write_config(lines):
    """Write modified lines back to the config file."""
    with open(CONFIG_FILE, "w") as f:
        f.writelines(lines)


def modify_config(category):
    """
    Modify tiku_test_config.h to enable the selected test category.

    Sets TEST_ENABLE=1, enables all flags for the category,
    disables all other settable flags.
    """
    enable_flags = set(category["flags"])
    lines = read_config()
    modified = []

    for line in lines:
        m = FLAG_RE.match(line)
        if m:
            prefix, flag_name, spacing, _old_val, suffix = m.groups()

            if flag_name == "TEST_ENABLE":
                modified.append(f"{prefix}{flag_name}{spacing}1{suffix}\n")
            elif flag_name in AUTO_DERIVED_FLAGS:
                # Leave auto-derived flags untouched
                modified.append(line)
            elif flag_name in enable_flags:
                modified.append(f"{prefix}{flag_name}{spacing}1{suffix}\n")
            elif flag_name in ALL_SETTABLE_FLAGS or flag_name.startswith("TEST_"):
                # Disable any other test flag that has a simple 0/1 value
                modified.append(f"{prefix}{flag_name}{spacing}0{suffix}\n")
            else:
                modified.append(line)
        else:
            modified.append(line)

    write_config(modified)


# ---------------------------------------------------------------------------
# Build and flash
# ---------------------------------------------------------------------------

def run_cmd(cmd, description, cwd=None):
    """Run a shell command, streaming output. Returns (success, output)."""
    print(f"\n{C.CYAN}[BUILD]{C.RESET} {description}")
    print(f"{C.DIM}$ {cmd}{C.RESET}")

    proc = subprocess.Popen(
        cmd, shell=True, cwd=cwd or PROJ_DIR,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True,
    )

    output_lines = []
    for line in proc.stdout:
        output_lines.append(line)
        sys.stdout.write(f"  {C.DIM}{line}{C.RESET}")
        sys.stdout.flush()

    proc.wait()
    success = proc.returncode == 0

    if success:
        print(f"{C.GREEN}[OK]{C.RESET} {description}")
    else:
        print(f"{C.RED}[FAIL]{C.RESET} {description} (exit code {proc.returncode})")

    return success, "".join(output_lines)


def build_and_flash(mcu):
    """Clean, build, and flash firmware. Returns True on success."""
    ok, _ = run_cmd(f"make clean", "Cleaning build artifacts", PROJ_DIR)
    if not ok:
        return False

    ok, _ = run_cmd(f"make MCU={mcu}", f"Building for {mcu}", PROJ_DIR)
    if not ok:
        return False

    ok, _ = run_cmd(f"make flash MCU={mcu}", f"Flashing {mcu}", PROJ_DIR)
    if not ok:
        return False

    return True


# ---------------------------------------------------------------------------
# Serial monitor and output parsing
# ---------------------------------------------------------------------------

def monitor_serial(port, baud, timeout_sec):
    """
    Read UART output from the device until [TS:END] or timeout.
    Returns list of raw lines.
    """
    try:
        import serial as pyserial
    except ImportError:
        print(f"\n{C.RED}[ERROR]{C.RESET} pyserial is required for UART monitoring.")
        print(f"  Install with: {C.CYAN}pip install pyserial{C.RESET}")
        sys.exit(1)

    print(f"\n{C.CYAN}[MONITOR]{C.RESET} Opening {port} @ {baud} baud "
          f"(timeout {timeout_sec}s)")

    # Small delay to let the programmer release the port
    time.sleep(1.5)

    try:
        ser = pyserial.Serial(port, baud, timeout=1)
    except pyserial.SerialException as e:
        print(f"{C.RED}[ERROR]{C.RESET} Cannot open {port}: {e}")
        print(f"  Is the LaunchPad plugged in?")
        print(f"  Try: ls /dev/ttyACM*")
        sys.exit(1)

    # Flush any stale data
    ser.reset_input_buffer()

    print(f"{C.DIM}  Waiting for test output...{C.RESET}")

    lines = []
    start = time.time()
    suite_started = False
    suite_ended = False

    while (time.time() - start) < timeout_sec:
        try:
            raw = ser.readline()
        except pyserial.SerialException:
            break

        if not raw:
            continue

        try:
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        except Exception:
            continue

        if not line:
            continue

        lines.append(line)

        # Show raw output with dimmed styling
        if line.startswith("[T:P:"):
            sys.stdout.write(f"  {C.GREEN}{line}{C.RESET}\n")
        elif line.startswith("[T:F:"):
            sys.stdout.write(f"  {C.RED}{line}{C.RESET}\n")
        elif line.startswith("[TS:") or line.startswith("[TG:"):
            sys.stdout.write(f"  {C.CYAN}{line}{C.RESET}\n")
        else:
            sys.stdout.write(f"  {C.DIM}{line}{C.RESET}\n")
        sys.stdout.flush()

        if line.startswith("[TS:BEGIN]"):
            suite_started = True
        elif line.startswith("[TS:END]"):
            suite_ended = True
            break

    ser.close()

    if not suite_started:
        print(f"\n{C.YELLOW}[WARN]{C.RESET} No [TS:BEGIN] received. "
              f"Possible causes:")
        print(f"  - TEST_ENABLE not set to 1")
        print(f"  - Wrong serial port or baud rate")
        print(f"  - Device did not reset after flash")
    elif not suite_ended:
        print(f"\n{C.YELLOW}[WARN]{C.RESET} Timed out waiting for [TS:END]. "
              f"Test may still be running or device reset.")

    return lines


# ---------------------------------------------------------------------------
# Result parsing
# ---------------------------------------------------------------------------

# Parse patterns
RE_TS_BEGIN = re.compile(r"^\[TS:BEGIN\]\s+(.+)$")
RE_TS_END   = re.compile(
    r"^\[TS:END\]\s+(.+?)\s+total=(\d+)\s+pass=(\d+)\s+fail=(\d+)$"
)
RE_TG_BEGIN = re.compile(r"^\[TG:BEGIN\]\s+(.+)$")
RE_TG_END   = re.compile(r"^\[TG:END\]\s+(.+)$")
RE_TEST     = re.compile(r"^\[T:([PF]):(\d+)\]\s+(.+)$")


def parse_results(lines):
    """
    Parse tagged UART output into structured results.

    Returns dict:
        suite_name: str
        groups: [{name, tests: [{num, passed, msg}]}]
        total: int
        passed: int
        failed: int
        raw_lines: [str]
    """
    result = {
        "suite_name": None,
        "groups": [],
        "total": 0,
        "passed": 0,
        "failed": 0,
        "raw_lines": lines,
        "complete": False,
    }

    current_group = None
    ungrouped_tests = []

    for line in lines:
        m = RE_TS_BEGIN.match(line)
        if m:
            result["suite_name"] = m.group(1)
            continue

        m = RE_TS_END.match(line)
        if m:
            result["suite_name"] = result["suite_name"] or m.group(1)
            result["total"] = int(m.group(2))
            result["passed"] = int(m.group(3))
            result["failed"] = int(m.group(4))
            result["complete"] = True
            continue

        m = RE_TG_BEGIN.match(line)
        if m:
            # Save any ungrouped tests
            if ungrouped_tests:
                result["groups"].append({
                    "name": "(ungrouped)",
                    "tests": ungrouped_tests,
                })
                ungrouped_tests = []
            current_group = {"name": m.group(1), "tests": []}
            result["groups"].append(current_group)
            continue

        m = RE_TG_END.match(line)
        if m:
            current_group = None
            continue

        m = RE_TEST.match(line)
        if m:
            test_entry = {
                "num": int(m.group(2)),
                "passed": m.group(1) == "P",
                "msg": m.group(3),
            }
            if current_group is not None:
                current_group["tests"].append(test_entry)
            else:
                ungrouped_tests.append(test_entry)
            continue

    # Trailing ungrouped tests
    if ungrouped_tests:
        result["groups"].append({
            "name": "(ungrouped)",
            "tests": ungrouped_tests,
        })

    # If no [TS:END] was received, compute from parsed tests
    if not result["complete"]:
        all_tests = [t for g in result["groups"] for t in g["tests"]]
        result["total"] = len(all_tests)
        result["passed"] = sum(1 for t in all_tests if t["passed"])
        result["failed"] = sum(1 for t in all_tests if not t["passed"])

    return result


# ---------------------------------------------------------------------------
# Display
# ---------------------------------------------------------------------------

def display_results(result, category):
    """Pretty-print parsed test results."""
    line_w = 72

    print()
    print(f"{C.BOLD}{'=' * line_w}{C.RESET}")
    print(f"{C.BOLD}  TEST RESULTS{C.RESET}")
    print(f"{'=' * line_w}")

    suite = result["suite_name"] or "Unknown"
    print(f"  Suite:    {C.CYAN}{suite}{C.RESET}")
    print(f"  Category: {C.CYAN}{category['name']}{C.RESET}")
    print(f"  {category['description']}")
    print(f"{'─' * line_w}")

    for group in result["groups"]:
        # Count pass/fail in group
        g_pass = sum(1 for t in group["tests"] if t["passed"])
        g_fail = sum(1 for t in group["tests"] if not t["passed"])
        g_total = len(group["tests"])

        if g_fail > 0:
            g_status = f"{C.RED}FAIL{C.RESET}"
        else:
            g_status = f"{C.GREEN}PASS{C.RESET}"

        print(f"\n  {C.BOLD}{group['name']}{C.RESET}  "
              f"[{g_status}] ({g_pass}/{g_total})")

        for t in group["tests"]:
            if t["passed"]:
                tag = f"{C.GREEN}PASS{C.RESET}"
            else:
                tag = f"{C.RED}FAIL{C.RESET}"

            print(f"    [{tag}] #{t['num']:>3d}  {t['msg']}")

    # Summary
    total = result["total"]
    passed = result["passed"]
    failed = result["failed"]

    print(f"\n{'─' * line_w}")

    if total > 0:
        pct = (passed / total) * 100
    else:
        pct = 0.0

    print(f"  {C.BOLD}TOTAL:{C.RESET} {total}  "
          f"| {C.GREEN}PASSED:{C.RESET} {passed}  "
          f"| {C.RED}FAILED:{C.RESET} {failed}  "
          f"| {C.BOLD}RATE:{C.RESET} {pct:.1f}%")

    if failed == 0 and total > 0:
        print(f"\n  {C.GREEN}{C.BOLD}ALL {total} TESTS PASSED{C.RESET}")
    elif failed > 0:
        print(f"\n  {C.RED}{C.BOLD}{failed} TEST(S) FAILED{C.RESET}")
    else:
        print(f"\n  {C.YELLOW}No test results received{C.RESET}")

    if not result["complete"]:
        print(f"  {C.YELLOW}(incomplete — [TS:END] not received){C.RESET}")

    print(f"{'=' * line_w}\n")


# ---------------------------------------------------------------------------
# Interactive menu
# ---------------------------------------------------------------------------

def detect_serial_port():
    """Auto-detect TI LaunchPad serial port."""
    import glob
    ports = sorted(glob.glob("/dev/ttyACM*"))
    if not ports:
        return None

    # Prefer TI vendor ID
    for port in ports:
        dev_name = os.path.basename(port)
        vid_path = f"/sys/class/tty/{dev_name}/device/../idVendor"
        try:
            with open(vid_path) as f:
                vid = f.read().strip()
            if vid in ("0451", "2047"):
                return port
        except (FileNotFoundError, PermissionError):
            pass

    return ports[0]


def show_menu(has_tikukits):
    """Display test category menu and return selected category."""
    print(f"\n{C.BOLD}{'=' * 64}{C.RESET}")
    print(f"{C.BOLD}  TikuOS Test Runner{C.RESET}")
    print(f"{'=' * 64}")
    print()

    available = []
    idx = 1

    # Core OS tests
    print(f"  {C.BOLD}Core OS Tests:{C.RESET}")
    for cat in CATEGORIES:
        if cat["requires_tikukits"]:
            continue
        available.append(cat)
        note = ""
        if cat.get("note"):
            note = f"  {C.DIM}({cat['note']}){C.RESET}"
        print(f"    {C.CYAN}[{idx:>2d}]{C.RESET} {cat['name']:<35s} {cat['description']}{note}")
        idx += 1

    # TikuKits tests
    kits_cats = [c for c in CATEGORIES if c["requires_tikukits"]]
    if kits_cats:
        print()
        if has_tikukits:
            print(f"  {C.BOLD}TikuKits Library Tests:{C.RESET}")
        else:
            print(f"  {C.BOLD}TikuKits Library Tests:{C.RESET} "
                  f"{C.DIM}(tikukits/ not found — unavailable){C.RESET}")

        for cat in kits_cats:
            available.append(cat)
            if has_tikukits:
                print(f"    {C.CYAN}[{idx:>2d}]{C.RESET} {cat['name']:<35s} {cat['description']}")
            else:
                print(f"    {C.DIM}[{idx:>2d}] {cat['name']:<35s} {cat['description']}{C.RESET}")
            idx += 1

    print()
    print(f"    {C.DIM}[ 0] Exit{C.RESET}")
    print()

    while True:
        try:
            choice = input(f"  Select test category [0-{len(available)}]: ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            sys.exit(0)

        if not choice:
            continue

        try:
            n = int(choice)
        except ValueError:
            # Try matching by key name
            matches = [c for c in available if c["key"] == choice.lower()]
            if matches:
                return matches[0]
            print(f"  {C.RED}Invalid selection{C.RESET}")
            continue

        if n == 0:
            sys.exit(0)
        if 1 <= n <= len(available):
            selected = available[n - 1]
            if selected["requires_tikukits"] and not has_tikukits:
                print(f"  {C.RED}tikukits/ directory not found — "
                      f"cannot run this test{C.RESET}")
                continue
            return selected

        print(f"  {C.RED}Invalid selection{C.RESET}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="TikuOS Test Runner — select, build, flash, and parse test results",
    )
    parser.add_argument(
        "--mcu", default="msp430fr5969",
        help="Target MCU (default: msp430fr5969)",
    )
    parser.add_argument(
        "--port", default=None,
        help="Serial port (default: auto-detect /dev/ttyACM*)",
    )
    parser.add_argument(
        "--baud", type=int, default=9600,
        help="Serial baud rate (default: 9600)",
    )
    parser.add_argument(
        "--timeout", type=int, default=60,
        help="UART read timeout in seconds (default: 60)",
    )
    parser.add_argument(
        "--category", default=None,
        help="Test category key (skip interactive menu)",
    )
    parser.add_argument(
        "--list", action="store_true",
        help="List available test categories and exit",
    )
    parser.add_argument(
        "--no-interactive", action="store_true",
        help="Skip confirmation prompts",
    )
    parser.add_argument(
        "--config-only", action="store_true",
        help="Only modify config file (do not build/flash/monitor)",
    )
    parser.add_argument(
        "--parse-file", default=None,
        help="Parse a saved UART log file instead of monitoring live",
    )

    args = parser.parse_args()

    # Check project structure
    if not os.path.exists(CONFIG_FILE):
        print(f"{C.RED}[ERROR]{C.RESET} Config file not found: {CONFIG_FILE}")
        print(f"  Run this script from the TikuOS repository root.")
        sys.exit(1)

    has_tikukits = os.path.isdir(TIKUKITS_DIR)

    # --list mode
    if args.list:
        print(f"\n{C.BOLD}Available test categories:{C.RESET}\n")
        for cat in CATEGORIES:
            avail = ""
            if cat["requires_tikukits"] and not has_tikukits:
                avail = f" {C.DIM}(needs tikukits/){C.RESET}"
            print(f"  {C.CYAN}{cat['key']:<20s}{C.RESET} {cat['name']}{avail}")
            print(f"  {'':20s} {C.DIM}{cat['description']}{C.RESET}")
        print()
        return

    # --parse-file mode: just parse an existing log
    if args.parse_file:
        with open(args.parse_file) as f:
            lines = [l.rstrip("\r\n") for l in f.readlines()]
        # Find which category might match (use a generic one)
        dummy_cat = {"name": "From log file", "description": args.parse_file}
        result = parse_results(lines)
        display_results(result, dummy_cat)
        sys.exit(1 if result["failed"] > 0 else 0)

    # Select category
    if args.category:
        matches = [c for c in CATEGORIES if c["key"] == args.category.lower()]
        if not matches:
            print(f"{C.RED}[ERROR]{C.RESET} Unknown category: {args.category}")
            print(f"  Use --list to see available categories.")
            sys.exit(1)
        category = matches[0]
        if category["requires_tikukits"] and not has_tikukits:
            print(f"{C.RED}[ERROR]{C.RESET} tikukits/ not found — "
                  f"cannot run {category['name']}")
            sys.exit(1)
    else:
        category = show_menu(has_tikukits)

    # Confirm
    print(f"\n{C.BOLD}Selected:{C.RESET} {C.CYAN}{category['name']}{C.RESET}")
    print(f"  {category['description']}")
    print(f"  Flags: {', '.join(category['flags'])}")

    if category.get("note"):
        print(f"  {C.YELLOW}Note: {category['note']}{C.RESET}")

    if not args.no_interactive and not args.config_only:
        # --- Serial port selection ---
        detected_port = args.port or detect_serial_port()
        default_port = detected_port or "/dev/ttyACM0"

        # Discover available ports for the menu
        import glob as _glob
        available_ports = sorted(_glob.glob("/dev/ttyACM*") + _glob.glob("/dev/ttyUSB*"))

        print(f"\n  {C.BOLD}Serial port{C.RESET} [default: {C.CYAN}{default_port}{C.RESET}]")
        if available_ports:
            for i, p in enumerate(available_ports, 1):
                marker = " *" if p == default_port else ""
                print(f"    {C.CYAN}[{i}]{C.RESET} {p}{C.DIM}{marker}{C.RESET}")
            print(f"    {C.DIM}[c] Enter custom path{C.RESET}")
        else:
            print(f"    {C.YELLOW}No ports detected{C.RESET} — press Enter to use default or type a path")

        try:
            port_choice = input(f"  Select port [Enter={default_port}]: ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            sys.exit(0)

        if not port_choice:
            port = default_port
        elif port_choice.lower() == "c":
            try:
                port = input(f"  Enter port path: ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                sys.exit(0)
            if not port:
                port = default_port
        elif port_choice.isdigit() and available_ports and 1 <= int(port_choice) <= len(available_ports):
            port = available_ports[int(port_choice) - 1]
        else:
            # Treat as a direct path
            port = port_choice

        args.port = port

        # --- Baud rate selection ---
        common_bauds = [9600, 19200, 38400, 57600, 115200]
        print(f"\n  {C.BOLD}Baud rate{C.RESET} [default: {C.CYAN}{args.baud}{C.RESET}]")
        for i, b in enumerate(common_bauds, 1):
            marker = " *" if b == args.baud else ""
            print(f"    {C.CYAN}[{i}]{C.RESET} {b}{C.DIM}{marker}{C.RESET}")
        print(f"    {C.DIM}[c] Enter custom value{C.RESET}")

        try:
            baud_choice = input(f"  Select baud [Enter={args.baud}]: ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            sys.exit(0)

        if not baud_choice:
            pass  # keep default
        elif baud_choice.lower() == "c":
            try:
                custom_baud = input(f"  Enter baud rate: ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                sys.exit(0)
            if custom_baud.isdigit() and int(custom_baud) > 0:
                args.baud = int(custom_baud)
            else:
                print(f"  {C.YELLOW}Invalid — using default {args.baud}{C.RESET}")
        elif baud_choice.isdigit() and 1 <= int(baud_choice) <= len(common_bauds):
            args.baud = common_bauds[int(baud_choice) - 1]
        elif baud_choice.isdigit() and int(baud_choice) > 0:
            # Treat as a direct baud value
            args.baud = int(baud_choice)
        else:
            print(f"  {C.YELLOW}Invalid — using default {args.baud}{C.RESET}")

        # --- Confirm ---
        print(f"\n  {C.BOLD}Configuration:{C.RESET}")
        print(f"  MCU:  {args.mcu}")
        print(f"  Port: {C.CYAN}{args.port}{C.RESET}")
        print(f"  Baud: {C.CYAN}{args.baud}{C.RESET}")

        try:
            confirm = input(f"\n  Proceed? [Y/n] ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            print()
            sys.exit(0)

        if confirm and confirm not in ("y", "yes"):
            print("  Aborted.")
            sys.exit(0)

    # Modify config
    print(f"\n{C.CYAN}[CONFIG]{C.RESET} Modifying {os.path.relpath(CONFIG_FILE, PROJ_DIR)}")
    modify_config(category)
    print(f"{C.GREEN}[OK]{C.RESET} TEST_ENABLE=1, "
          f"{', '.join(category['flags'])} enabled")

    if args.config_only:
        print(f"\n{C.GREEN}Config updated. Build manually with:{C.RESET}")
        print(f"  make clean && make flash MCU={args.mcu} && "
              f"make monitor PORT=... BAUD={args.baud}")
        return

    # Build and flash
    if not build_and_flash(args.mcu):
        print(f"\n{C.RED}Build or flash failed. Aborting.{C.RESET}")
        sys.exit(1)

    # Detect serial port
    port = args.port or detect_serial_port()
    if not port:
        print(f"\n{C.RED}[ERROR]{C.RESET} No serial port found (/dev/ttyACM*)")
        print(f"  Is the LaunchPad plugged in?")
        print(f"  Specify manually: --port /dev/ttyACM1")
        sys.exit(1)

    # Monitor
    lines = monitor_serial(port, args.baud, args.timeout)

    # Parse and display
    result = parse_results(lines)
    display_results(result, category)

    # Exit code
    sys.exit(1 if result["failed"] > 0 else 0)


if __name__ == "__main__":
    main()
