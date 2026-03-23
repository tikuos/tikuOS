#!/usr/bin/env python3
"""
TikuOS Example Runner

Interactive tool that lists all available examples, lets you pick one,
compiles with the correct flags, flashes the device, and captures the
serial output.

Usage:
    python3 tools/run_example.py                # interactive menu
    python3 tools/run_example.py --list         # list examples and exit
    python3 tools/run_example.py -e 74          # run example 74 directly
    python3 tools/run_example.py -e dns         # search by name
    python3 tools/run_example.py -e dns -i      # use picocom (interactive)

Authors: Ambuj Varshney <ambuj@tiku-os.org>
SPDX-License-Identifier: Apache-2.0
"""

import argparse
import glob
import os
import subprocess
import sys
import time

# ---------------------------------------------------------------------------
# Example registry
# ---------------------------------------------------------------------------

EXAMPLES = [
    (1,  "BLINK",                "Single LED blink",                 ""),
    (2,  "DUAL_BLINK",           "Two LEDs, two processes",          ""),
    (3,  "BUTTON_LED",           "Button-controlled LED",            ""),
    (4,  "MULTI_PROCESS",        "Inter-process events",             ""),
    (5,  "STATE_MACHINE",        "Event-driven state machine",       ""),
    (6,  "CALLBACK_TIMER",       "Callback-mode timers",             ""),
    (7,  "BROADCAST",            "Broadcast events",                 ""),
    (8,  "TIMEOUT",              "Timeout pattern",                  ""),
    (9,  "CHANNEL",              "Channel message passing",          ""),
    (10, "I2C_TEMP",             "I2C temperature sensor (MCP9808)", ""),
    (11, "DS18B20_TEMP",         "DS18B20 1-Wire temp sensor",       ""),
    (12, "UDP_SEND",             "UDP sender over SLIP",
         "-DTIKU_APP_NET=1"),
    (13, "TCP_SEND",             "TCP sender over SLIP",
         "-DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_APP_NET=1"),
    (14, "DNS_RESOLVE",          "DNS resolver over SLIP",
         "-DTIKU_APP_NET=1"),
    (15, "HTTP_GET",             "HTTPS GET over SLIP",
         "-DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_KITS_NET_HTTP_ENABLE=1"
         " -DTIKU_APP_NET=1"),
    (16, "TCP_ECHO",             "TCP echo client over SLIP",
         "-DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_APP_NET=1"),
    (17, "HTTP_FETCH",           "Fetch real webpage via gateway",
         "-DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_APP_NET=1"),
    (18, "HTTP_DIRECT",          "Direct HTTP GET to internet",
         "-DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_APP_NET=1"
         " -DTIKU_KITS_NET_SLIP_ESC_NUL_ENABLE=0"
         " -DTIKU_KITS_NET_MTU=300"),
    (19, "HTTPS_DIRECT",         "HTTPS GET via PSK-TLS gateway",
         "-DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_KITS_NET_HTTP_ENABLE=1"
         " -DTIKU_APP_NET=1"
         " -DTIKU_KITS_NET_SLIP_ESC_NUL_ENABLE=0"
         " -DTIKU_KITS_NET_MTU=150"
         " -DTIKU_KITS_NET_TCP_MAX_CONNS=1"
         " -DTIKU_KITS_CRYPTO_TLS_MAX_FRAG_LEN=80"
         " -DTIKU_KITS_CRYPTO_TLS_RECORD_BUF_SIZE=150"
         " -DTIKU_KITS_NET_HTTP_REQ_BUF_SIZE=150"
         " -DTIKU_KITS_NET_DNS_CACHE_SIZE=1"),
    (20, "KITS_MATRIX",          "Matrix operations",                ""),
    (21, "KITS_STATISTICS",      "Statistics functions",              ""),
    (22, "KITS_DISTANCE",        "Distance metrics",                 ""),
    (30, "KITS_DS_ARRAY",        "Array data structure",             ""),
    (31, "KITS_DS_BITMAP",       "Bitmap data structure",            ""),
    (32, "KITS_DS_BTREE",        "B-tree data structure",            ""),
    (33, "KITS_DS_HTABLE",       "Hash table",                       ""),
    (34, "KITS_DS_LIST",         "Linked list",                      ""),
    (35, "KITS_DS_PQUEUE",       "Priority queue",                   ""),
    (36, "KITS_DS_QUEUE",        "Queue",                            ""),
    (37, "KITS_DS_RINGBUF",      "Ring buffer",                      ""),
    (38, "KITS_DS_SM",           "State machine",                    ""),
    (39, "KITS_DS_SORTARRAY",    "Sorted array",                     ""),
    (40, "KITS_DS_STACK",        "Stack",                            ""),
    (41, "KITS_DS_BLOOM",        "Bloom filter",                     ""),
    (42, "KITS_DS_CIRCLOG",      "Circular log",                     ""),
    (43, "KITS_DS_DEQUE",        "Double-ended queue",               ""),
    (44, "KITS_DS_TRIE",         "Trie",                             ""),
    (50, "KITS_ML_LINREG",       "Linear regression",                ""),
    (51, "KITS_ML_LOGREG",       "Logistic regression",              ""),
    (52, "KITS_ML_DTREE",        "Decision tree",                    ""),
    (53, "KITS_ML_KNN",          "K-nearest neighbors",              ""),
    (54, "KITS_ML_NBAYES",       "Naive Bayes",                      ""),
    (55, "KITS_ML_LINSVM",       "Linear SVM",                       ""),
    (56, "KITS_ML_TNN",          "Tiny neural network",              ""),
    (60, "KITS_SENSORS",         "Temperature sensors",              ""),
    (61, "KITS_SIGFEATURES",     "Signal feature extraction",        ""),
    (62, "KITS_TEXTCOMPRESSION", "Text compression",                 ""),
    (70, "KITS_NET_IPV4",        "IPv4 fundamentals (loopback)",     ""),
    (71, "KITS_NET_UDP",         "UDP datagrams (loopback)",         ""),
    (72, "KITS_NET_TFTP",        "TFTP client (loopback)",           ""),
    (73, "KITS_NET_TCP",         "TCP transport (loopback)",
         "-DTIKU_KITS_NET_TCP_ENABLE=1"),
    (74, "KITS_NET_DNS",         "DNS resolver (loopback)",          ""),
    (75, "KITS_NET_TLS",         "TLS 1.3 PSK crypto",
         "-DTIKU_KITS_NET_TCP_ENABLE=1"),
    (76, "KITS_NET_HTTP",        "HTTP/1.1 client (loopback)",       ""),
]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def find_port():
    """Find the eZ-FET serial port (same logic as 'make monitor').

    Looks for TI vendor ID (0451 or 2047) among /dev/ttyACM* ports.
    Falls back to the first ttyACM* or ttyUSB* port found.
    """
    ports = sorted(glob.glob("/dev/ttyACM*"))
    # Prefer port with TI vendor ID
    for p in ports:
        try:
            base = os.path.basename(p)
            vid_path = f"/sys/class/tty/{base}/device/../idVendor"
            with open(vid_path) as f:
                vid = f.read().strip()
            if vid in ("0451", "2047"):
                return p
        except (OSError, IOError):
            pass
    if ports:
        return ports[0]
    usb = sorted(glob.glob("/dev/ttyUSB*"))
    if usb:
        return usb[0]
    return None


def print_examples():
    """Print grouped example list."""
    categories = [
        ("Core OS",          1,  19),
        ("Maths",           20,  22),
        ("Data Structures", 30,  44),
        ("Machine Learning",50,  56),
        ("Sensors/Signal",  60,  62),
        ("Networking",      70,  79),
    ]
    for name, lo, hi in categories:
        group = [e for e in EXAMPLES if lo <= e[0] <= hi]
        if not group:
            continue
        print(f"\n  {name}:")
        for num, flag, desc, extra in group:
            marker = " *" if extra else ""
            print(f"    [{num:>2}] {desc}{marker}")
    print("\n  * = requires additional flags (handled automatically)")


def search_example(query):
    """Find example by number or name substring."""
    try:
        num = int(query)
        for ex in EXAMPLES:
            if ex[0] == num:
                return ex
        return None
    except ValueError:
        pass
    q = query.lower()
    matches = [e for e in EXAMPLES if q in e[2].lower() or q in e[1].lower()]
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        print(f"\n  Multiple matches for '{query}':")
        for num, flag, desc, _ in matches:
            print(f"    [{num:>2}] {desc}")
    return None


def run_make(args_list, cwd, desc):
    """Run make with given arguments, print status."""
    print(f"  {desc}...", end=" ", flush=True)
    r = subprocess.run(["make"] + args_list,
                       cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        print("FAILED")
        if r.stdout:
            print(r.stdout[-300:])
        if r.stderr:
            print(r.stderr[-300:])
        return False
    print("OK")
    return True


def build_and_flash(flag_name, extra_cflags, mcu, project_dir):
    """Clean build and flash."""
    cflags = f"-DTIKU_EXAMPLE_{flag_name}=1 {extra_cflags}".strip()
    make_args = [f"MCU={mcu}", "HAS_TESTS=0",
                 f"EXTRA_CFLAGS={cflags}"]

    print(f"\n  CFLAGS: {cflags}")

    if not run_make(["clean"], project_dir, "Cleaning"):
        return False
    if not run_make(make_args, project_dir, "Compiling"):
        return False
    if not run_make(["flash"] + make_args, project_dir, "Flashing"):
        return False
    return True


def capture_output(port, baud=9600, duration=15):
    """Capture example output using the same serial approach as TikuBench.

    Uses the same open_serial pattern that works for TikuBench network
    tests: open with dtr=False before open(), wait 2s for boot, flush.
    The firmware has a 3-second startup delay before printing output,
    so the total wait is ~5 seconds before output appears.
    """
    try:
        import serial
    except ImportError:
        print("\n  pyserial not installed: pip install pyserial")
        return

    print(f"\n  Opening {port} @ {baud} baud...")

    # Use exact same pattern as TikuBench open_serial()
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.bytesize = serial.EIGHTBITS
    ser.parity = serial.PARITY_NONE
    ser.stopbits = serial.STOPBITS_ONE
    ser.timeout = 0.5
    ser.xonxoff = False
    ser.rtscts = False
    ser.dsrdtr = False
    ser.dtr = False
    ser.open()
    time.sleep(2.0)
    ser.reset_input_buffer()

    print(f"  Port open. Reading for {duration}s...\n")

    # Read output
    output = b''
    deadline = time.time() + duration
    empty_streak = 0
    while time.time() < deadline:
        chunk = ser.read(512)
        if chunk:
            output += chunk
            empty_streak = 0
            # Print incrementally so user sees output in real time
            text = chunk.decode('ascii', errors='replace')
            for ch in text:
                if ch == '\r':
                    continue
                sys.stdout.write(ch)
            sys.stdout.flush()
        else:
            empty_streak += 1
            # If we got output and then 6 empty reads (3s), done
            if output and empty_streak >= 6:
                break

    ser.close()

    if not output:
        print("  (no output captured)")
        print("  The device may have printed before the port opened.")
        print("  Try: python3 tools/run_example.py -e <name> -i")
        print("  Then press RESET on the LaunchPad after picocom opens.")
    print()


def open_monitor(port, baud=9600):
    """Open picocom interactively (replaces this process)."""
    print(f"\n  Opening picocom on {port} @ {baud} baud")
    print(f"  Press RESET on LaunchPad to see output.")
    print(f"  Press Ctrl-A Ctrl-X to exit.\n")
    os.execvp("picocom", ["picocom", "-b", str(baud), port])

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(
        description="TikuOS Example Runner — build, flash, capture output")
    parser.add_argument("--list", action="store_true",
                        help="List all examples and exit")
    parser.add_argument("--example", "-e", default=None,
                        help="Example number or name")
    parser.add_argument("--mcu", default="msp430fr5969")
    parser.add_argument("--port", default=None,
                        help="Serial port (default: auto-detect)")
    parser.add_argument("--baud", type=int, default=9600)
    parser.add_argument("-i", "--interactive", action="store_true",
                        help="Use picocom instead of auto-capture")
    parser.add_argument("--no-flash", action="store_true",
                        help="Build only, skip flash and monitor")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)

    if args.list:
        print("\nTikuOS Examples:")
        print_examples()
        print()
        sys.exit(0)

    # Select example
    selected = None
    if args.example:
        selected = search_example(args.example)
        if not selected:
            print(f"Error: No example matching '{args.example}'")
            print("Use --list to see available examples")
            sys.exit(1)
    else:
        print("\n" + "=" * 50)
        print("  TikuOS Example Runner")
        print("=" * 50)
        print_examples()
        print()
        try:
            choice = input("  Enter example number (or name): ").strip()
        except (KeyboardInterrupt, EOFError):
            print("\n  Cancelled.")
            sys.exit(0)
        if not choice:
            sys.exit(0)
        selected = search_example(choice)
        if not selected:
            print(f"  No example matching '{choice}'")
            sys.exit(1)

    num, flag, desc, extra = selected

    print(f"\n{'=' * 50}")
    print(f"  [{num}] {desc}")
    print(f"{'=' * 50}")

    if not build_and_flash(flag, extra, args.mcu, project_dir):
        print("\n  Build/flash failed!")
        sys.exit(1)

    if args.no_flash:
        print("\n  Build complete (--no-flash).")
        sys.exit(0)

    port = args.port or find_port()
    if not port:
        print("  No serial port found. Use --port to specify.")
        sys.exit(1)

    # The eZ-FET cannot be programmatically captured via pyserial —
    # it only forwards UART data when a real terminal (picocom/screen)
    # holds the port open.  Always use picocom and tell the user to
    # press the physical RESET button on the LaunchPad.
    #
    # The firmware delays 20 seconds before printing output, giving
    # the user time to see "Terminal ready" and press RESET.
    open_monitor(port, args.baud)


if __name__ == "__main__":
    main()
