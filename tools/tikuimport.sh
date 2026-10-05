#!/bin/sh
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# tikuimport.sh - stage a model on the EK-RA8P1's USB disk and commit it.
#
# Writes the file from LBA 0 of the board's "SDRAM Stage" disk, then a commit
# record to its last block, which starts the board's import into flash.
# usage: tikuimport.sh <file> [name]
#
# SPDX-License-Identifier: Apache-2.0

F="$1"; N="${2:-model}"
[ -r "$F" ] || { echo "cannot read '$F'"; exit 1; }
D=$(lsblk -ndo NAME,MODEL | grep "SDRAM Stage" | cut -d" " -f1)
[ -n "$D" ] || { echo "board disk not found - ABORT"; exit 1; }
LEN=$(stat -c %s "$F")
BLKS=$(cat /sys/block/"$D"/size)
LAST=$((BLKS - 1))
echo "device : /dev/$D  ($BLKS blocks)"
echo "model  : $F  ($LEN bytes)  name='$N'"
echo "--- staging payload at LBA 0 ---"
dd if="$F" of=/dev/"$D" bs=1M oflag=direct conv=fsync || exit 1
# The commit record goes to the last block, the one the board watches: magic
# "TKIM", u32 length, then the name cut to 23 bytes and NUL-padded to 24.
echo "--- writing commit record to LBA $LAST ---"
python3 -c "
import struct,sys
rec = struct.pack('<II', 0x4D494B54, $LEN) + '$N'.encode()[:23].ljust(24, b'\0')
sys.stdout.buffer.write(rec.ljust(512, b'\0'))
" | dd of=/dev/"$D" bs=512 seek="$LAST" oflag=direct conv=fsync || exit 1
echo "--- committed; watch the board console ---"
