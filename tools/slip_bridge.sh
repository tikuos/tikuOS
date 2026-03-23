#!/bin/bash
#
# TikuOS SLIP Internet Bridge
#
# Sets up a SLIP interface over the serial port and configures
# Linux NAT so the MSP430 can access the internet directly.
#
# Usage:
#   sudo tools/slip_bridge.sh                    # auto-detect port
#   sudo tools/slip_bridge.sh /dev/ttyUSB0       # specify port
#   sudo tools/slip_bridge.sh /dev/ttyUSB0 9600  # specify baud
#
# After running this script:
#   1. Press RESET on the MSP430 LaunchPad
#   2. The device boots, resolves DNS, and fetches the webpage
#   3. LED1 lights up on success
#   4. Press Ctrl+C to tear down the bridge
#
# Requirements:
#   - slattach (apt install net-tools)
#   - Root access (sudo)
#   - External FTDI/CP2102 UART adapter (eZ-FET can't do TCP)
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0

set -e

PORT="${1:-}"
BAUD="${2:-9600}"
SLIP_IF="sl0"
HOST_IP="172.16.7.1"
DEVICE_IP="172.16.7.2"

# Auto-detect port if not specified
if [ -z "$PORT" ]; then
    for p in /dev/ttyUSB* /dev/ttyACM*; do
        if [ -e "$p" ]; then
            PORT="$p"
            break
        fi
    done
fi

if [ -z "$PORT" ] || [ ! -e "$PORT" ]; then
    echo "Error: no serial port found. Usage: $0 /dev/ttyUSB0"
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: must run as root (sudo $0 $*)"
    exit 1
fi

echo "=================================================="
echo "  TikuOS SLIP Internet Bridge"
echo "=================================================="
echo "  Port:      $PORT"
echo "  Baud:      $BAUD"
echo "  SLIP IF:   $SLIP_IF"
echo "  Host IP:   $HOST_IP"
echo "  Device IP: $DEVICE_IP"
echo ""

# Cleanup function
cleanup() {
    echo ""
    echo "  Tearing down SLIP bridge..."
    # Remove NAT rule (ignore errors if rule doesn't exist)
    iptables -t nat -D POSTROUTING -s "$DEVICE_IP" \
             -j MASQUERADE 2>/dev/null || true
    # Kill slattach
    killall slattach 2>/dev/null || true
    sleep 0.5
    echo "  Done."
}
trap cleanup EXIT

# Step 1: Start SLIP on the serial port
echo "  [1/4] Starting slattach on $PORT @ $BAUD baud..."
slattach -L -p slip -s "$BAUD" "$PORT" &
SLATTACH_PID=$!
sleep 1

# Verify slattach started
if ! kill -0 "$SLATTACH_PID" 2>/dev/null; then
    echo "  Error: slattach failed to start"
    exit 1
fi

# Step 2: Configure IP addresses
echo "  [2/4] Configuring $SLIP_IF interface..."
ip addr add "$HOST_IP/32" peer "$DEVICE_IP" dev "$SLIP_IF" \
    2>/dev/null || true
ip link set "$SLIP_IF" up

# Step 3: Enable IP forwarding
echo "  [3/4] Enabling IP forwarding..."
sysctl -w net.ipv4.ip_forward=1 > /dev/null

# Step 4: Set up NAT (masquerade)
echo "  [4/4] Setting up NAT (MASQUERADE)..."
iptables -t nat -A POSTROUTING -s "$DEVICE_IP" -j MASQUERADE

echo ""
echo "  Bridge is UP. The MSP430 can now access the internet."
echo "  Press RESET on the LaunchPad to start the example."
echo "  Press Ctrl+C to tear down the bridge."
echo ""

# Wait for Ctrl+C
wait "$SLATTACH_PID" 2>/dev/null || true
