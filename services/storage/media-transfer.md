# Computer transfers

`tools/tiku_media_transfer.py` lists, uploads and downloads objects in the
mounted native media filesystem. The P4 shell build with
`TIKU_MEDIA_FS_ENABLE=1` and `TIKU_P4_SDCARD_ENABLE=1` includes its USB console
service. Wi-Fi transfers also require `TIKU_KITS_NET_TCP_ENABLE=1` and a
working Wi-Fi IP connection.

## USB console

After boot, run `media mount` in the **TikuOS shell**, then close the terminal
so the host tool can own the USB port. Run these commands on the **computer**:

```sh
python3 tools/tiku_media_transfer.py --serial /dev/ttyACM0 ls
python3 tools/tiku_media_transfer.py --serial /dev/ttyACM0 put image.jpg photos/image.jpg
python3 tools/tiku_media_transfer.py --serial /dev/ttyACM0 get photos/image.jpg downloaded.jpg
```

USB requires Python 3.10 or later, `pyserial` and `fuser`. Use the board's
`/dev/serial/by-id/` path when multiple boards are attached. The tool checks
that the port is an idle Espressif USB Serial/JTAG device and preserves the
control-line state needed to avoid a reset. It enables `media usb` when it
opens the port. The service transfers binary data through console channel
`0xF7`; the card does not appear as a computer disk drive.

## Wi-Fi

Join a network with the TikuOS Wi-Fi commands, run `wifi up`, then use `ip`
to read the board's address. With the card mounted, create a TCP listener
and its access key from the computer through USB:

```sh
python3 tools/tiku_media_transfer.py --serial /dev/ttyACM0 --key-file media.key serve --port 7331
python3 tools/tiku_media_transfer.py --tcp 192.168.1.125:7331 --key-file media.key ls
python3 tools/tiku_media_transfer.py --tcp 192.168.1.125:7331 --key-file media.key put weights.bin models/weights.bin
python3 tools/tiku_media_transfer.py --tcp 192.168.1.125:7331 --key-file media.key get models/weights.bin downloaded.bin
```

Substitute the address reported by `ip`. `serve` creates a new 32-byte key
file with owner-only permissions and refuses to replace an existing file.
The listener keeps its key in RAM. `media off`, another listener start,
or reboot disables that listener. USB and TCP listeners are selected one at a time. TCP accepts one
connection. Reply segments wait for acknowledgment before the next segment;
the host sizes TCP chunks to fit the negotiated MSS, including framing.
Sessions idle for 120 seconds release their file handles.

The key authorizes listing, reading and uploading files. The protocol sends
the key and file contents over plain TCP; use it on a trusted, secured LAN.
It has no TLS layer and does not expose formatting or deletion operations.
The shell command that provisions the key appears on the USB console and
can remain in shell history or console captures. Stopping the listener does
not erase those copies.

## Publication and resume

Uploads require a name of 1..63 UTF-8 bytes, an expected length and SHA-256.
The board keeps an upload private until length and digest verification pass.
Use `put ... --replace` to replace an existing name. Existing snapshot readers
continue reading the version they opened.

The tool prints the upload ID and volume UUID when an upload starts. Resume
on the same volume, with the same source and destination, using:

```sh
python3 tools/tiku_media_transfer.py --serial /dev/ttyACM0 put weights.bin models/weights.bin --resume 81
```

The board compares the name, expected length and digest with the private
upload before accepting a resume. Accepted data is staged in 64 KiB batches;
a disconnect may require retransmitting the last partial batch. Failed
storage operations require release and resume before more data is accepted.
`media uploads` lists retained uploads; `media abort ID` removes one through
the TikuOS shell. Neither command runs on the computer.

Downloads read a snapshot, verify SHA-256 and publish a temporary file in
the destination directory. A failed digest check leaves the destination
unchanged. `get ... --replace` permits replacing an existing local file.
Downloads restart after interruption. The host tool reads files in bounded
chunks; it does not load an entire recording or model into memory.

## Wire contract

Requests and replies have a 32-byte little-endian header:

| Offset | Field |
|---|---|
| 0 | `TMT1` magic, four bytes |
| 4 | Operation, one byte |
| 5 | Flags: request BEGIN bit 0 permits replacement; replies use `0x80` |
| 6 | Two reserved zero bytes |
| 8 | Nonzero request sequence, 32 bits |
| 12 | Signed native media status, 32 bits; zero in requests |
| 16 | Operation argument or result, 64 bits |
| 24 | Payload length, 32 bits, at most 1024 |
| 28 | CRC32C over header bytes 0..27 followed by payload |

USB uses the console SLIP envelope and channel marker `0xF7`. TCP prefixes
packets with a four-byte little-endian length including the header. Each
session has one outstanding request. An exact repeat of the last request
returns the cached reply without executing the operation again. A conflicting
retry or malformed packet is dropped. HELLO starts a session; subsequent
requests use consecutive sequence values without wraparound.

| Operation | Request | Reply |
|---|---|---|
| 1 HELLO | Empty USB payload or 32-byte TCP key | Argument 1024; payload batch size u32 and volume UUID 16 bytes |
| 2 BEGIN | Argument length; payload digest 32 bytes and name | Argument upload ID |
| 3 RESUME | Argument ID; payload expected length u64, digest and name | Argument durable offset |
| 4 DATA | Argument offset; payload file bytes | Argument accepted offset; payload durable offset u64 |
| 5 FINISH | Argument expected total length | Argument published length |
| 6 RELEASE | Empty | Release writer; durable private prefix remains |
| 7 OPEN | Name payload | Snapshot metadata |
| 8 READ | Argument offset; payload requested length u32 | Argument offset; payload bytes |
| 9 CLOSE | Empty | Release snapshot reader |
| 10 LIST | Prefix payload, possibly empty | Initialize listing cursor |
| 11 NEXT | Empty | Metadata or native `NOENT` status |

Metadata contains ID u64, length u64, digest 32 bytes, completion byte and
name bytes without a terminator. A catalog mutation invalidates a listing
cursor; start another LIST when the service reports `STALE`.

The service allocates its session and staging data from transient SRAM on
listener start, and releases that allocation on stop. Console reassembly uses
static storage so an allocator reset cannot leave the decoder writing through
a stale arena pointer. Storage requests run from the shell pump, outside
console and TCP callbacks, through the media service's ownership checks.

## Checks

```sh
make -C TikuBench/tests/host/kernel media-transfer
```

The tests exercise the production filesystem and media service, protocol
validation, resumable prefixes, retry suppression, digest failures, local
publication and fragmented TCP. The server harness covers RX fragmentation,
TX backpressure, listener ownership, idle cleanup and allocator reset.
Mutation checks cover checksums, keys, lengths, offsets, failed upload
continuation, listener ownership, decoder lifetime, acknowledgment pacing
and host checkpoint splitting. Each altered guard must fail its corresponding
test.
