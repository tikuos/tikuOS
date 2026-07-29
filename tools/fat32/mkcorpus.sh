#!/bin/sh
# Build the FAT32 test corpus.
#
# Every image here is written by mkfs.vfat and populated through the Linux
# kernel's own FAT driver, which makes the reference implementation for every
# gate the same code the rest of the world uses.  A parser tested only against
# images its own author generated proves considerably less.
#
# Needs: mkfs.vfat (dosfstools) and mtools (mcopy/mmd/mdel) -- mtools writes
# into the images WITHOUT root, which is the whole reason this harness needs
# no privileges and can run anywhere.
#
# usage: tools/fat32/mkcorpus.sh <outdir>
set -e
OUT="${1:-/tmp/fat32corpus}"
mkdir -p "$OUT"

have() { command -v "$1" >/dev/null 2>&1; }
for t in mkfs.vfat mcopy mmd; do
    have "$t" || { echo "missing $t (install dosfstools and mtools)"; exit 2; }
done

# Deterministic payloads: the test recomputes these, so they must not be random.
gen() {  # gen <file> <size>
    python3 - "$1" "$2" <<'PY'
import sys
p, n = sys.argv[1], int(sys.argv[2])
open(p, "wb").write(bytes(((i * 37 + 11) & 0xFF) for i in range(n)))
PY
}

echo "corpus -> $OUT"
gen "$OUT/small.bin"  1000          # smaller than a sector
gen "$OUT/exact.bin"  4096          # exact cluster multiple
gen "$OUT/big.bin"    1500000       # many clusters
gen "$OUT/frag_a.bin" 300000
gen "$OUT/frag_b.bin" 300000

# ---- FAT32 at two cluster sizes -------------------------------------------
for SPC in 1 8; do
    IMG="$OUT/fat32_spc$SPC.img"
    # 64 MB is comfortably past the 65525-cluster floor at spc=1
    rm -f "$IMG"; truncate -s 512M "$IMG"
    mkfs.vfat -F 32 -s "$SPC" -n TIKUTEST "$IMG" >/dev/null

    mmd   -i "$IMG" ::/sub                                  2>/dev/null || true
    mmd   -i "$IMG" ::/sub/deeper                           2>/dev/null || true
    mcopy -i "$IMG" "$OUT/small.bin" ::/SMALL.BIN
    mcopy -i "$IMG" "$OUT/exact.bin" ::/EXACT.BIN
    mcopy -i "$IMG" "$OUT/big.bin"   ::/big.bin
    mcopy -i "$IMG" "$OUT/small.bin" "::/a rather long file name.dat"
    mcopy -i "$IMG" "$OUT/exact.bin" ::/sub/deeper/NESTED.BIN

    # FRAGMENTATION ON PURPOSE.  Interleave two files, then delete one, then
    # write a third into the holes -- the chain walker's real test, and the
    # thing a synthetic image would never produce.
    mcopy -i "$IMG" "$OUT/frag_a.bin" ::/FRAGA.BIN
    mcopy -i "$IMG" "$OUT/frag_b.bin" ::/FRAGB.BIN
    mdel  -i "$IMG" ::/FRAGA.BIN 2>/dev/null || true
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

# ---- the NEGATIVE corpus: these must all be REFUSED ------------------------
rm -f "$OUT/fat16.img"; truncate -s 32M "$OUT/fat16.img"
mkfs.vfat -F 16 -n TIKUFAT16 "$OUT/fat16.img" >/dev/null
echo "  $OUT/fat16.img (must be refused: NOT_FAT32)"

rm -f "$OUT/fat12.img"; truncate -s 2M "$OUT/fat12.img"
mkfs.vfat -F 12 -n TIKUFAT12 "$OUT/fat12.img" >/dev/null
echo "  $OUT/fat12.img (must be refused: NOT_FAT32)"

head -c 1048576 /dev/zero > "$OUT/zeros.img"
echo "  $OUT/zeros.img (must be refused: NOFS)"

# A valid FAT32 whose chain has been made to LOOP.  This one matters most:
# it is the case where a bounded walker and an unbounded one differ, and the
# difference is a hang.
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
