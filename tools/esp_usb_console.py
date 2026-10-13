# Tiku Operating System v0.06
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# esp_usb_console.py - Exclusive native Espressif USB console ownership.
# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import subprocess


def require_idle_usb_port(port, ports):
    """Require a named, idle native Espressif USB port before opening it."""
    resolved = os.path.realpath(port)
    if not any(os.path.realpath(item.device) == resolved and
               (item.vid, item.pid) == (0x303A, 0x1001) for item in ports):
        raise ValueError("port is not a native Espressif USB Serial/JTAG device")
    if not shutil.which("fuser"):
        raise ValueError("fuser is required to check existing port owners")
    # Linux fuser returns 1 and prints nothing for an idle file; macOS fuser
    # returns 0 and echoes "<path>:" on stdout.  Both print owner PIDs after
    # that, so once the echoed path is removed any remaining text is an owner
    # or an error message.
    owners = subprocess.run(["fuser", resolved], capture_output=True, text=True)
    report = (owners.stdout + owners.stderr).replace(resolved + ":", "")
    if owners.returncode > 1 or report.strip():
        raise ValueError("port is busy or its ownership could not be checked")


def usb_console(serial, port):
    """Construct a closed console with both control lines asserted on open.

    On native USB, deasserting DTR while RTS is asserted resets the device.
    The ROM loader changes these lines explicitly when a reset is requested.
    """
    wire = serial.Serial(port=None, baudrate=115200, timeout=0.1,
                         write_timeout=1, exclusive=True)
    wire.dtr = True
    wire.rts = True
    wire.port = port
    return wire
