#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# tiku_usb_bulk.py - Exclusive vendor bulk records on the P4 high-speed USB port.
#
# SPDX-License-Identifier: Apache-2.0
import errno
import time

CAPACITY = 8192


class Bulk:
    """Claim a TikuOS vendor interface without detaching a kernel driver."""

    def __init__(self, serial=None, timeout=30000, wait=0):
        import usb.core
        import usb.util
        self.device = None
        self.util = usb.util
        self.timeout = timeout
        self.timeout_error = usb.core.USBTimeoutError
        deadline = time.monotonic() + wait
        while True:
            matches = [d for d in usb.core.find(find_all=True, idVendor=0x1209, idProduct=0x0003)
                       if serial in (None, 'auto') or d.serial_number == serial]
            if len(matches) > 1:
                raise ValueError('multiple TikuOS bulk devices; specify the USB serial')
            if matches:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError('TikuOS bulk device absent; enable media usbhs on the P4')
            time.sleep(.05)
        device = matches[0]
        try:
            config = device.get_active_configuration()
            interface = config[(0, 0)]
            endpoints = {e.bEndpointAddress: e for e in interface}
            if (interface.bInterfaceClass != 0xff or set(endpoints) != {2, 0x83} or
                    any(e.bmAttributes & 3 != 2 for e in endpoints.values()) or
                    endpoints[2].wMaxPacketSize not in (64, 512) or
                    endpoints[0x83].wMaxPacketSize != endpoints[2].wMaxPacketSize):
                raise ValueError('unexpected TikuOS bulk interface')
            self.util.claim_interface(device, 0)
            self.device = device
            self.packet_size = endpoints[2].wMaxPacketSize
        except BaseException:
            self.util.dispose_resources(device)
            raise

    def send(self, data):
        """Send one bounded record and terminate an aligned record with a short packet."""
        if not 0 < len(data) <= CAPACITY:
            raise ValueError('USB bulk record length')
        try:
            if self.device.write(2, data, timeout=self.timeout) != len(data):
                raise OSError('short USB bulk write')
            if len(data) < CAPACITY and len(data) % self.packet_size == 0:
                self.device.write(2, b'', timeout=self.timeout)
        except self.timeout_error as error:
            raise TimeoutError('USB bulk write timed out') from error

    def receive(self):
        """Receive one short-packet-terminated record with a bounded USB wait."""
        try:
            data = bytes(self.device.read(0x83, CAPACITY, timeout=self.timeout))
        except self.timeout_error as error:
            raise TimeoutError('USB bulk read timed out') from error
        if not data:
            raise ValueError('empty USB bulk record')
        return data

    def close(self):
        """Release the interface and its libusb resources."""
        if self.device is not None:
            device = self.device
            self.device = None
            try:
                self.util.release_interface(device, 0)
            except OSError as error:
                if error.errno != errno.ENODEV:
                    raise
            finally:
                self.util.dispose_resources(device)
