#!/bin/sh
# Stage a model on the TikuOS disk and commit it to flash.
#   usage: tikuimport.sh <file> [name]
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
# Commit record: magic "TKIM", u32 length, 24-byte name -- written to the
# LAST block, which no ordinary tool touches.
echo "--- writing commit record to LBA $LAST ---"
python3 -c "
import struct,sys
rec = struct.pack('<II', 0x4D494B54, $LEN) + '$N'.encode()[:23].ljust(24, b'\0')
sys.stdout.buffer.write(rec.ljust(512, b'\0'))
" | dd of=/dev/"$D" bs=512 seek="$LAST" oflag=direct conv=fsync || exit 1
echo "--- committed; watch the board console ---"
