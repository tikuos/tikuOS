#!/bin/sh
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# mkcorpus.sh - build the FAT images fat_host_test reads.
#
# Formats each image with mkfs.vfat (dosfstools) and fills it with mtools
# (mcopy, mmd, mdel), which write into an image file without root.
# usage: tools/fat32/mkcorpus.sh <outdir>, default /tmp/fat32corpus
#
# SPDX-License-Identifier: Apache-2.0

set -e
OUT="${1:-/tmp/fat32corpus}"
mkdir -p "$OUT"

have() { command -v "$1" >/dev/null 2>&1; }
for t in mkfs.vfat mcopy mmd; do
    have "$t" || { echo "missing $t (install dosfstools and mtools)"; exit 2; }
done

# Deterministic payloads: the test recomputes these, so they must not be random.
# Every little-endian 32-bit word holds its own byte offset, so bytes read from
# the wrong cluster never match the ones expected.
gen() {  # gen <file> <size>
    python3 - "$1" "$2" <<'PY'
import struct, sys
p, n = sys.argv[1], int(sys.argv[2])
data = b"".join(struct.pack("<I", o) for o in range(0, n, 4))
open(p, "wb").write(data[:n])
PY
}

echo "corpus -> $OUT"
gen "$OUT/small.bin"  1000          # smaller than a sector
gen "$OUT/exact.bin"  4096          # exact cluster multiple
gen "$OUT/bound.bin"  12288         # ends on a boundary at both cluster sizes
gen "$OUT/big.bin"    1500000       # many clusters
gen "$OUT/frag_a.bin" 300000
gen "$OUT/frag_b.bin" 300000

# ---- FAT32 at two cluster sizes -------------------------------------------
for SPC in 1 8; do
    IMG="$OUT/fat32_spc$SPC.img"
    # 512 MB keeps both cluster sizes above FAT32's 65525-cluster floor
    rm -f "$IMG"; truncate -s 512M "$IMG"
    mkfs.vfat -F 32 -s "$SPC" -n TIKUTEST "$IMG" >/dev/null

    mmd   -i "$IMG" ::/sub                                  2>/dev/null || true
    mmd   -i "$IMG" ::/sub/deeper                           2>/dev/null || true
    mcopy -i "$IMG" "$OUT/small.bin" ::/SMALL.BIN
    mcopy -i "$IMG" "$OUT/exact.bin" ::/EXACT.BIN
    mcopy -i "$IMG" "$OUT/bound.bin" ::/BOUND.BIN
    mcopy -i "$IMG" "$OUT/big.bin"   ::/big.bin
    mcopy -i "$IMG" "$OUT/small.bin" "::/a rather long file name.dat"
    # Names that fill their last 13-character piece carry no terminator.
    mcopy -i "$IMG" "$OUT/small.bin" "::/thirteen-char"
    mcopy -i "$IMG" "$OUT/small.bin" "::/twenty-six-characters-name"
    mcopy -i "$IMG" "$OUT/exact.bin" ::/sub/deeper/NESTED.BIN

    # Fragmentation: write two files, delete the first, and write a third
    # that fills the first one's hole and continues past the second.
    mcopy -i "$IMG" "$OUT/frag_a.bin" ::/FRAGA.BIN
    mcopy -i "$IMG" "$OUT/frag_b.bin" ::/FRAGB.BIN
    mdel  -i "$IMG" ::/FRAGA.BIN 2>/dev/null || true
    # mtools allocates from the FSInfo next-free hint, which points past
    # FRAGB.BIN.  Setting it to 0xFFFFFFFF ("unknown") starts the search at
    # cluster 2, so FRAGGED.BIN fills the hole and continues past FRAGB.BIN.
    python3 - "$IMG" <<'PY'
import struct, sys
f = open(sys.argv[1], "r+b")
b = f.read(512)
bps = struct.unpack_from("<H", b, 11)[0]
fsinfo = struct.unpack_from("<H", b, 48)[0]
f.seek(fsinfo * bps + 492)
f.write(struct.pack("<I", 0xFFFFFFFF))
f.close()
PY
    mcopy -i "$IMG" "$OUT/big.bin"   ::/FRAGGED.BIN
    echo "  $IMG (spc=$SPC)"
done

# ---- an MBR-partitioned image ---------------------------------------------
IMG="$OUT/fat32_mbr.img"
rm -f "$IMG"; truncate -s 544M "$IMG"
# one primary partition, type 0x0C, starting at 2048 sectors (1 MB aligned)
python3 - "$IMG" <<'PY'
import struct, sys
img = sys.argv[1]
start, size = 2048, (544*1024*1024)//512 - 2048
mbr = bytearray(512)
e = struct.pack("<BBBBBBBBII", 0x00, 0,0,0, 0x0C, 0,0,0, start, size)
mbr[446:446+16] = e
mbr[510], mbr[511] = 0x55, 0xAA
with open(img, "r+b") as f:
    f.write(mbr)
PY
mkfs.vfat -F 32 -s 8 --offset 2048 -n TIKUMBR "$IMG" >/dev/null
mcopy -i "$IMG@@1048576" "$OUT/big.bin" ::/BIG.BIN
echo "  $IMG (MBR, partition at LBA 2048)"

# ---- images the parser must refuse -----------------------------------------
rm -f "$OUT/fat16.img"; truncate -s 32M "$OUT/fat16.img"
mkfs.vfat -F 16 -n TIKUFAT16 "$OUT/fat16.img" >/dev/null
echo "  $OUT/fat16.img (must be refused: NOT_FAT32)"

# FAT16 in the first partition and a non-FAT one after it: refused as
# NOT_FAT32, though the last partition parsed has no filesystem at all.
IMG="$OUT/fat16_mbr.img"
rm -f "$IMG"; truncate -s 33M "$IMG"
python3 - "$IMG" <<'PY'
import struct, sys
img = sys.argv[1]
fat16 = (32 * 1024 * 1024) // 512
mbr = bytearray(512)
mbr[446:462] = struct.pack("<BBBBBBBBII", 0, 0,0,0, 0x06, 0,0,0, 2048, fat16)
mbr[462:478] = struct.pack("<BBBBBBBBII", 0, 0,0,0, 0x83, 0,0,0,
                           2048 + fat16, 2048)
mbr[510], mbr[511] = 0x55, 0xAA
with open(img, "r+b") as f:
    f.write(mbr)
PY
mkfs.vfat -F 16 --offset 2048 -n TIKUF16P "$IMG" >/dev/null
truncate -s 34M "$IMG"                  # the second partition: zeros
echo "  $IMG (must be refused: NOT_FAT32)"

rm -f "$OUT/fat12.img"; truncate -s 2M "$OUT/fat12.img"
mkfs.vfat -F 12 -n TIKUFAT12 "$OUT/fat12.img" >/dev/null
echo "  $OUT/fat12.img (must be refused: NOT_FAT32)"

head -c 1048576 /dev/zero > "$OUT/zeros.img"
echo "  $OUT/zeros.img (must be refused: NOFS)"

# A valid FAT32 image whose cluster chain loops (3 -> 2): a chain walker
# without a bound hangs on it.
cp "$OUT/fat32_spc8.img" "$OUT/fat32_loop.img"
python3 - "$OUT/fat32_loop.img" <<'PY'
import struct, sys
p = sys.argv[1]
f = open(p, "r+b")
b = f.read(512)
bps = struct.unpack_from("<H", b, 11)[0]
rsvd = struct.unpack_from("<H", b, 14)[0]
fatsz = struct.unpack_from("<I", b, 36)[0]
fat = rsvd * bps
# point cluster 3 back at cluster 2: any chain reaching 3 now cycles forever
f.seek(fat + 3*4); f.write(struct.pack("<I", 2))
f.close()
print("  %s (chain loop planted: cluster 3 -> 2)" % p)
PY

echo "done"
