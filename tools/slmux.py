#!/usr/bin/env python3
"""slmux - SLIP + console multiplexer for TikuOS over one serial line.

When the board is in `slip` mode it interleaves, on a single UART, both the
interactive shell (ASCII text) and SLIP/IP frames (0xC0-delimited).  slmux
demultiplexes that single wire into two things at once:

  * a TUN network interface (default tun0) -- so the Linux kernel's own
    networking rides it: `ping 172.16.7.2`, `curl http://172.16.7.2`, etc.
  * an interactive console -- the board's shell text on stdout, your
    keystrokes (stdin) sent back, so you can still type commands.

So one cable carries the shell AND real networking simultaneously.

Needs root (it creates a TUN device and configures the interface):

    sudo python3 slmux.py /dev/ttyACM0

Then, in the slmux console, type `slip` once to put the board in SLIP mode.
From another terminal the board is now a host:

    ping 172.16.7.2
    curl http://172.16.7.2          # (needs a listener on the board)

Quit slmux with Ctrl-] (the terminal is restored on exit).

Board -> internet (optional), once tun0 is up, as root:
    sysctl -w net.ipv4.ip_forward=1
    iptables -t nat -A POSTROUTING -s 172.16.7.0/24 -o <wan-if> -j MASQUERADE
then `ping 8.8.8.8` from the board's shell.
"""

import argparse
import atexit
import fcntl
import os
import select
import struct
import subprocess
import sys
import termios
import tty

# --- SLIP framing (RFC 1055 + TikuOS NUL-escape extension) ---
SLIP_END, SLIP_ESC = 0xC0, 0xDB
SLIP_ESC_END, SLIP_ESC_ESC, SLIP_ESC_NUL = 0xDC, 0xDD, 0xDE

# --- TUN ioctl ---
TUNSETIFF = 0x400454CA
IFF_TUN, IFF_NO_PI = 0x0001, 0x1000


def slip_encode(pkt):
    out = bytearray([SLIP_END])
    for b in pkt:
        if b == SLIP_END:
            out += bytes([SLIP_ESC, SLIP_ESC_END])
        elif b == SLIP_ESC:
            out += bytes([SLIP_ESC, SLIP_ESC_ESC])
        elif b == 0:
            out += bytes([SLIP_ESC, SLIP_ESC_NUL])
        else:
            out.append(b)
    out.append(SLIP_END)
    return bytes(out)


def slip_unescape(data):
    out = bytearray()
    i = 0
    while i < len(data):
        b = data[i]
        if b == SLIP_ESC and i + 1 < len(data):
            i += 1
            n = data[i]
            out.append(SLIP_END if n == SLIP_ESC_END else
                       SLIP_ESC if n == SLIP_ESC_ESC else
                       0x00 if n == SLIP_ESC_NUL else n)
        else:
            out.append(b)
        i += 1
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--dev", default="tun0")
    ap.add_argument("--host-ip", default="172.16.7.1")
    ap.add_argument("--board-ip", default="172.16.7.2")
    args = ap.parse_args()

    try:
        import serial
    except ImportError:
        sys.exit("slmux: pyserial required (pip install pyserial)")

    if os.geteuid() != 0:
        sys.exit("slmux: must run as root (creates a TUN device). Use sudo.")

    ser = serial.Serial(args.port, args.baud, timeout=0)

    tun = os.open("/dev/net/tun", os.O_RDWR)
    ifr = struct.pack("16sH", args.dev.encode(), IFF_TUN | IFF_NO_PI)
    fcntl.ioctl(tun, TUNSETIFF, ifr)
    subprocess.run(["ip", "addr", "add", args.host_ip, "peer", args.board_ip,
                    "dev", args.dev], check=True)
    subprocess.run(["ip", "link", "set", args.dev, "up"], check=True)

    sys.stderr.write(
        "slmux: %s <-> %s  (board %s, host %s).  In this console type 'slip' "
        "to enable the board's SLIP mode.  Quit: Ctrl-]\r\n"
        % (args.port, args.dev, args.board_ip, args.host_ip))

    fd_in = sys.stdin.fileno()
    old_tty = termios.tcgetattr(fd_in)
    tty.setraw(fd_in)

    def restore():
        termios.tcsetattr(fd_in, termios.TCSADRAIN, old_tty)
    atexit.register(restore)

    in_frame = False
    frame = bytearray()
    ser_fd = ser.fileno()

    try:
        while True:
            r, _, _ = select.select([ser_fd, tun, fd_in], [], [])

            if ser_fd in r:                       # board -> host
                for b in ser.read(2048):
                    if b == SLIP_END:
                        if in_frame:
                            if frame:
                                os.write(tun, slip_unescape(frame))
                            frame = bytearray()
                            in_frame = False
                        else:
                            in_frame = True
                            frame = bytearray()
                    elif in_frame:
                        frame.append(b)
                    else:
                        os.write(1, bytes([b]))   # console text -> stdout

            if tun in r:                          # kernel packet -> board
                ser.write(slip_encode(os.read(tun, 2048)))

            if fd_in in r:                         # keystrokes -> board shell
                k = os.read(fd_in, 64)
                if b"\x1d" in k:                  # Ctrl-]  -> quit
                    break
                ser.write(k)
    finally:
        restore()
        try:
            subprocess.run(["ip", "link", "set", args.dev, "down"])
        except Exception:
            pass


if __name__ == "__main__":
    main()
