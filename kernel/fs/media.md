# Native media filesystem

`TIKU_MEDIA_FS_ENABLE=1` builds `tiku_mfs`, the queued media store and
`/media` with `/sys/media`. The filesystem stores opaque byte objects on
an explicitly selected 512-byte block extent on a 32-bit-or-wider integer
ABI. Its catalog grows on disk as files are created, bounded by available
space and 64-bit object IDs. It has eight snapshot readers, one writer
and a four-request service queue.
Names contain up to 63 bytes, including slash-separated virtual prefixes.
A file and directory prefix cannot have the same name.

The native API uses 64-bit lengths, offsets and IDs. SHA-256's bit-count
encoding limits an object to `UINT64_MAX / 8` bytes; volume capacity and
allocation reserve impose lower physical bounds. Supported extents contain
8 MiB through 2 TiB. The VFS adapter refuses files above `UINT32_MAX` bytes;
native range reads retain their 64-bit offsets.

## Publication and reads

`begin(STREAM)` publishes an empty file. `append` accepts up to 64 KiB and
writes unreferenced frames. `sync` publishes the accepted prefix after two
ordered backend barriers. `release` discards an unsynced tail; committed
prefixes remain resumable by ID. `finalize` marks the file complete.

`begin(ATOMIC)` creates a private upload with an expected length and SHA-256.
`sync` persists its upload offset and hash state without exposing its name.
`finalize` checks length and digest, then publishes the object and replaces
an existing visible name in one transaction. Readers already holding the
old version retain its extent root. The hash checks byte integrity; it does
not authenticate an untrusted model or manifest.

`open_read` pins a committed snapshot. `read_at` verifies the requested
4 KiB payload slices and returns at most 64 KiB. `close_read` releases the
snapshot. Tokens expire on remount. Calls on a context are serialized;
contexts and bitmap buffers belong to the caller. There is no heap use
inside the core or service.

The caller supplies two bitmaps, each sized by
`tiku_mfs_bitmap_bytes(block.sectors)`. These and the filesystem context
must outlive the mount. A capture application supplies its input queue
through the memory-tier allocator and sets its own batch and sync policy.
Partial final chunks use copy-on-write; committing many short appends to
one chunk rewrites its earlier bytes. Applications should batch captures.

## Format

All integers are little-endian. CRC32C uses reflected polynomial
`0x82f63b78`, initial value `0xffffffff` and final inversion. Metadata
sectors store their CRC at byte 508 over bytes 0 through 507. Bytes 8
through 23 bind each sector to the caller-supplied 128-bit volume UUID.
The format version and required feature word are 2. Other versions and
feature words are refused. Version 1 images require export with a version 1
reader and import into a newly formatted volume; there is no in-place upgrade.

The first 1 MiB segment holds two checkpoint banks. Each bank contains a
header, the segment-allocation bitmap rounded up to sectors, and 128
two-sector journal records. A checkpoint writes a zero header and
synchronizes, writes the bitmap and synchronizes, then writes the
checksummed header and synchronizes. Header fields include epoch,
sequence, next object ID, allocator position, bitmap checksum and two
catalog roots. The previous bank remains available during publication.

The catalog has two immutable AVL indexes: object ID to descriptor sector,
and name to visible and private object IDs. Each node occupies one sector,
with checksummed child references, subtree height and visible/private counts.
Insertions, replacements and deletions copy the affected paths and rebalance
before publishing. Names and IDs use logarithmic lookup. Descriptors and
index nodes occupy allocation segments; catalog size does not determine RAM
usage or checkpoint size. Traversals use bounded stacks with room for the
AVL height supported by the maximum volume geometry.

A journal record contains both new catalog roots and a commit sector. The
commit binds the root-record checksum, epoch, sequence, predecessor and
allocator progress. Catalog nodes, descriptors, payload dependencies and the
root record are synchronized before writing the commit; the commit is
synchronized before acknowledging durability. A mount replays a contiguous
journal prefix and stops at its first absent or incomplete commit. The first
subsequent mutation checkpoints into a new epoch before reusing journal slots.

Data occupies 1 MiB allocation segments. A frame has a 512-byte header,
1 through 128 payload sectors and a 512-byte footer. The footer carries
CRC32C for each logical 4 KiB slice; its checksum is bound by the header.
Frames do not cross segment boundaries. A logical chunk contains up to
64 KiB; an object ends with a partial chunk when required.

Each object has a copy-on-write radix index with 16 children per 512-byte
page and at most 12 levels. References carry a sector address and expected
CRC. Index pages bind object ID, level and logical range. Each descriptor stores
an extent root, policy, visibility, length, name, expected digest and the
incremental SHA-256 state. `list_pending` and the administrative service
`UPLOADS` request enumerate interrupted private uploads for resume or deletion.
Listings return names in byte order through a caller-owned cursor.
`list_seek` starts at a byte prefix; private listings count pending IDs
separately. Mutation or remount invalidates an existing cursor with `STALE`.
Mount reads checkpoint bitmaps, bounded journal records and referenced
catalog root pages. It does not enumerate files or traverse extent trees.
Bitmap size depends on volume capacity.

## Space and maintenance

Allocation searches free segments with a rotating cursor. Journal and
checkpoint storage is reserved separately from payload space. Data allocation
preserves a 1 MiB reserve for catalog publication. Beginning a writer also
requires space for its initial catalog paths. A data append that exhausts
usable space can still publish the current writer's completed metadata.
Repeated sync without appended data performs no writes. Exhausted space
and reader slots have explicit error results.

Collection checkpoints the current catalog roots into both recovery banks,
marks catalog, descriptor, extent-index and data segments reachable from
the catalog and pinned readers,
then publishes the resulting bitmap into both banks. Traversal advances
through `collect_step` with an index-node budget. Start and completion also
write bounded checkpoint banks. Collection refuses writers and new readers;
existing readers can read or close their snapshots.

`tiku_mfs_copy_begin/step/abort` relocates a completed object through a
private verified replacement, using a caller-supplied buffer. Each step
copies and synchronizes at most the buffer capacity. The service exposes
these as administrative `COPY_BEGIN`, `COPY_STEP` and `COPY_ABORT` requests
under the requesting client's writer ownership. Run collection after
relocation to reclaim dead segments. Replacement needs capacity for both
versions. Collection does not discard live objects or implement a retention
policy; applications choose deletion and maintenance schedules.

## P4 backend and service

`tiku_p4_mfs_block_init` binds the observed card CID, an explicit sector
extent and a selected 400 kHz through 20 MHz clock. Acquisition identifies
the card, checks CID and capacity, authorizes writes to that extent, then
selects the clock. Release restores the C6 controller resources after
quiescence. Transfers use an aligned 4 KiB SRAM DMA stage and one through
eight sectors; multi-block transfers require automatic-stop completion.
Writes check programming completion and CMD13 status. Optional SD cache
is not enabled.

The driver uses a one-bit bus and synchronous operations. Identification
runs on each acquired session. A service step holds the SDMMC lease for
one filesystem operation, including its barriers. The node budget limits
collection traversal, not card busy time or checkpoint latency. Capture
buffer sizing, sustained throughput, concurrent radio latency, card power
cycling and energy savings require hardware qualification.

`tiku_media_store_submit/step` owns serialization, client writer/reader
ownership and buffer lifetime. A busy acquisition retains the queued
request for retry. Release happens before request completion is published.
An I/O or restoration failure requires quiescence and recovery; a failed
service is reinitialized after the backend has been recovered. The trusted
caller maps external authorization to the service's permission bits.

A board/application initializes the service with its selected extent, UUID,
bitmaps and backend hooks, submits `MOUNT`, then attaches it with
`tiku_media_vfs_attach`. Boot publishes the providers but does not select,
mount or format a card. `FORMAT` requires an explicit administrative request.
The legacy VFS provider streams directory listings with fixed memory and
offers range reads; creation and recording use the native service API.
Catalog lookup, listing and reader opening acquire a backend session to
read index pages. USB/Wi-Fi applications can export
ordinary file bytes through these APIs; no custom volume is exposed as a
writable PC mass-storage drive.

## Whole-card operations on FireBeetle 2

The P4 build provides `media mount [hz]` and
`media format CID UUID [hz]`. Format replaces the whole card's volume and
partition table; the 32-digit hexadecimal CID must match `sd probe` and
UUID must be a new nonzero 32-digit hexadecimal identity. Mount discovers
identity from checksummed version-2 checkpoint headers without writing.
Neither command runs automatically at boot. The default card clock is
4 MHz; the accepted range is 400 kHz through 20 MHz.

`tiku_media_card_store()` returns the mounted service for applications.
Its two bitmaps occupy an SRAM-tier arena sized to card capacity. Metadata
operations validate the arena before accessing it; a tier reset invalidates
the mount and requires `media mount`. Insufficient SRAM refuses mounting.
The controller and CID are checked on each acquired service operation.

Shell object names omit `/media/`; VFS reads use that prefix. `media begin
NAME` starts a stream. `media begin NAME LENGTH SHA256` starts a private
verified upload. `media append HEX`, `media sync`, `media finish` and
`media release` control that writer. `media uploads` enumerates interrupted
uploads; `media resume ID` resumes their committed prefix and `media abort
ID` deletes them. `media rm NAME` deletes a visible object. `media gc`
reclaims unreachable whole segments. Shell transfers use bounded hexadecimal
windows; capture applications use the native service's binary buffers.

```text
media mount
media begin notes/hello.txt
media append 68656c6c6f0a
media finish
media stat notes/hello.txt
media read notes/hello.txt 0 128
ls /media/notes
cat /media/notes/hello.txt
```

`media info` reports volume identity and object counts. `media ls [prefix]`
lists visible objects, and `media read NAME OFFSET BYTES` returns up to 128
bytes as hex. Keep shell append payloads at 120 bytes or fewer with the
256-character input line. Remount after reboot to reattach `/media`.

## Host images and verification

Build the regular-file image utility:

```sh
cc -std=c99 -O2 -I. tools/tiku_mfs_image.c kernel/fs/tiku_mfs.c \
  kernel/fs/tiku_mfs_copy.c -o /tmp/tiku_mfs_image
/tmp/tiku_mfs_image format /tmp/media.img \
  000102030405060708090a0b0c0d0e0f 67108864
/tmp/tiku_mfs_image list /tmp/media.img 000102030405060708090a0b0c0d0e0f
python3 tools/tiku_mfs_read.py /tmp/media.img 000102030405060708090a0b0c0d0e0f
make -C TikuBench/tests/host/kernel media-fs media-fs-mutations media-fs-images
```

The C utility also accepts `put NAME INPUT SHA256`, `get NAME OUTPUT`,
`delete NAME`, `copy NAME`, `uploads`, `abort ID` and `collect` after the image and UUID arguments.
Formatting creates a new regular file and refuses an existing path. The core
also refuses a valid existing checkpoint with the requested UUID; a new
format requires a new nonzero UUID. The
Python reader independently decodes the catalog, journal, indexes and
frames; adding `NAME OUTPUT` exports a checked file without modifying the
image. Both exporters refuse an existing output path.

The host suite injects failures at write/barrier boundaries, including
missing, complete and torn current writes. It checks standard SHA-256 and
CRC32C vectors, indexed reads, journal rollover, private-upload recovery,
replacement, pinned readers, reclamation, relocation, full-volume finalization,
service ownership, 1024-file creation and deletion, sorted directory
pagination, bounded mount reads, catalog rotations under interrupted writes
and sparse SDXC image export with an independent 128-file decoder check. Mutation tests remove
publication and integrity guards and require assertion failures. These
models assume completed barriers preserve prior sectors; they do not
qualify a physical card's internal flash controller.

## Computer transfers

The P4 USB console and Wi-Fi TCP service provide uploads, downloads and
listing through [the computer transfer tool](../../services/storage/media-transfer.md).
