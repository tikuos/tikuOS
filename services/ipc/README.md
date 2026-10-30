# IPC transport contracts

The [kernel IPC interface](../../kernel/ipc/README.md) owns process bindings,
copied queues, managed buffer references and completion records. Transport
services translate those operations into shared-memory or framed exchanges.
They run bounded steps from IPC progress and do not dispatch applications.
Source support is selected by `TIKU_IPC_ENABLE=1`.

## Whole-message wire protocol

`tiku_ipc_wire_init()` attaches one logical endpoint to an owned `tiku_link`.
The link copies an accepted send and delivers borrowed receive bytes in kernel
process context. The wire receiver copies a bounded frame before returning.
Initialization requires a static endpoint name, a fresh nonzero 64-bit local
epoch and a frame size from 80 through 256 bytes. Peer identity is a channel
binding, without an authentication claim.

The 40-byte header is little-endian:

| Offset | Bytes | Field |
|--------|-------|-------|
| 0 | 4 | Magic `0x50494b54` |
| 4 | 1 | Version 1 |
| 5 | 1 | Kind |
| 6 | 2 | Reserved, zero |
| 8 | 8 | Source connection epoch |
| 16 | 8 | Destination connection epoch |
| 24 | 4 | Transaction id |
| 28 | 2 | Fragment offset |
| 30 | 2 | Total block length |
| 32 | 2 | This fragment's payload length |
| 34 | 2 | Signed IPC status |
| 36 | 4 | CRC-32 over bytes 0–35 followed by the payload |

Kinds 1–8 are HELLO, HELLO_ACK, COPY, COPY_ACK, INPUT, INPUT_ACK, OUTPUT and
OUTPUT_DATA. HELLO negotiates the endpoint name, frame capacity, copied-message
limit, block limit and supported loan directions. Limits are bounded by the
local context: 64 copied bytes and 512 block bytes. QUERY status exposes the
negotiated message/block limits after the handshake. There is one reserved
copied-control inbox and two bulk inboxes. The transaction id's low two bits
select control lane 0 or bulk lane 1/2; the remaining serial retires before
wrap.

Copied messages occupy one frame, so their usable limit also fits both frame
payload capacities. An 80-byte frame carries at most 40 copied bytes. Peer
advertisements exceeding their own frame capacity are refused before binding.
HELLO also limits copied bytes to the configured local core receive capacity.

HELLO carries destination zero. A changed peer invalidates pending work;
a new HELLO from a ready peer also advances the local nonce. Admission
waits for HELLO_ACK addressed to that current nonce. An ACK confirms its
destination without advancing it. Copied control is serviced before bulk
replies. A refused carrier send retries after one tick; an accepted frame
uses the four-tick retransmission interval.

COPY_ACK attests queue admission. INPUT_ACK with success attests the sink's
final access to the staged block. OUTPUT_DATA contains producer-generated
bytes; the sender applies them to a matching live transfer. A sink returning
`AGAIN` accepts no bytes. A producer fills exactly the requested length.
Each callback is bounded, synchronous and retains no pointer on return.

A copied exchange has a 128-tick lifetime.
Bulk operation deadlines belong to the submitting kernel operation. Received
transaction state suppresses repeated processing. CRC, epoch and full
transaction checks precede delivery. Rebinding or resetting advances the
connection epoch; delayed traffic cannot bind to the replacement. Epoch and
serial exhaustion fail closed. Cancellation clears the copy-out association
before the kernel can return private backing.

`TIKU_IPC_CONSOLE_ENABLE=1` attaches `console/ipc` on channel `0xF4`. Boot
obtains its epoch from the platform TRNG and fails if entropy is unavailable.
The console process services frames during waits that exclude the shell.
Buffered physical text retains its local caller origin; injected text retains
its remote origin and capability restrictions.

`tiku_ipc_mux` prefixes a frame with one channel byte and supplies eight
virtual links over an owned physical link. Both peers agree on channel ids;
wire frames on a virtual link are at most 255 bytes. Closing a virtual link
keeps the physical carrier owned. Its capability mask is inherited. Physical
loss invokes each registered invalidation callback without process dispatch.

## Packet stages and access domains

`tiku_ipc_access_domain_t` declares an address interval, peer, engine,
directions, alignment and cache-line size. Admission checks the full span,
integer overflow, consumer identities and ownership of complete cache lines.
It does not infer device visibility from an SRAM/PSRAM tier. A CPU-copy engine
uses id zero. Ports declare narrower domains for DMA-capable stages.

`tiku_ipc_bus` has separate 320-byte transmit and receive stages aligned to
64 bytes, plus a pending outgoing frame. A packet starts with a two-byte
little-endian frame length, at most 256 bytes of frame data, and zero padding.
One whole 320-byte exchange retains both stages until `poll()` succeeds or
`abort()` confirms final hardware access. Successful polling includes cache
synchronization. An error alone does not release the stages. Failed abort
quarantines them; a late coherent completion can end access. The context may
be reused after close only when `tiku_ipc_bus_reusable()` succeeds.

A target's optional `listening()` callback reports an armed stage awaiting
observable controller progress. That wait retains its stages and packet
advertisement without consuming the transaction timeout. After activity,
the bounded burst deadline applies. Controller ports leave the callback
NULL. C6 observes its first completed SDIO transfer half; RP2350 observes
CS, FIFO/START activity and STOP. Loss before observable activity retains
the target stages until completion or an explicit quiescent reset.

SPI/I²C controller adapters use the portable blocking bus interfaces and a
CPU-copy domain. The caller configures and owns the bus, address/chip select
and port timeout. `burst_ticks` must bound the complete port transaction,
including timeout behavior. A cooperative timeout cannot preempt a port
function that does not return.

`hal/tiku_ipc_target_hal.h` provides fixed target stages and serialized SPI
shift / I²C write, read and transaction-end hooks. A port connects its actual
target peripheral and IRQ handler to these hooks. Its quiesce callback must
disable final access before returning success. These common hooks supply no
peripheral register configuration or IRQ routing.

`tiku_link_uart` owns an independent byte port with partial reads/writes. It
reuses the console envelope's CRC-16, sequence, acknowledgements and pacing,
including SLIP END, ESC and NUL escaping. The port callbacks retain no supplied
span. Each pump performs bounded byte work; pending acknowledgements do not
split an outgoing frame. The caller owns UART configuration and buffering
below the byte-port interface.

## SDIO owner and recovery

`tiku_ipc_sdio` owns one controller and its pins through
`tiku_sdio_owner_ops_t`. Fixed storage reserves a native-control job, two bulk
jobs and a loader-update job. Each job has separate aligned 512-byte TX/RX
stages. Submission copies caller bytes; the driver sees the owner's stages.
CONTROL is selected before another bulk burst; the two bulk jobs alternate.
NATIVE and LOADER accept different traffic classes. UPDATE bytes are passed
to the loader protocol without native diagnostic parsing.

Zero-initialize an owner before first use. Consume or discard outstanding
results after close, then check `tiku_ipc_sdio_reusable()` before reopening
with a fresh session. Reopen preserves job generations and retired slots;
it clears the native sequence and physical exchange serial. Do not erase
the context while old job handles can still be presented.
The driver's release callback must also confirm that private negotiation
DMA has stopped. A refused release retains the lease and context through
the abort deadline, then reports FAILED. An explicit OFF retry can close
after final access has ended.

The native diagnostic format has 12-byte request and 24-byte response payload
capacities in 20-byte and 32-byte envelopes. Sequence occupies byte 0, used
length byte 1, little-endian CRC-16 bytes 2–3, payload starts at byte 4, and
the final four bytes contain the session. The CRC covers sequence, used length,
used payload and session, excluding padding. Nonzero eight-bit sequences
advance through 255 then 1; exact duplicates can repeat. Altered requests with
the same sequence are refused. Shared-register address holes are preserved by
`tiku_sdio_diag_address()`.

Each owner profile supplies separate positive half-range clock intervals:

| Parameter | Driver/service obligation |
|-----------|---------------------------|
| `burst` | Finish one started bulk exchange or begin recovery |
| `detect` | Report loss when ready/liveness ceases |
| `abort` | Confirm final local access or report FAILED with quarantined stages |
| `boot` | Reset into the selected mode and reach its ready indication |
| `negotiate` | Bind that mode to the fresh local session |

CONTROL and UPDATE jobs retain the absolute deadline admitted at submission.
The P4 diagnostic client submits a two-second control deadline. A bulk job
also has a submission deadline; its active exchange is bounded by `burst`.

Callbacks perform bounded non-dispatching steps. The owner passes a full
64-bit session/serial token to start, poll and abort. A delayed old token cannot
finish current work. Quiescence precedes peripheral reset or ownership release;
an uncertain engine is not reset by the service. A failed owner can retain
stages, report that quarantine once and release them after exact late final
access. Recovery into LOADER uses hardware reset or watchdog recovery and
requires no native acknowledgement. Selecting a native image is a separate
driver operation; a loader-ready state does not imply native-ready.

`tiku_ipc_sdio_packet` adapts bulk jobs to the packet engine. Its upper domain
names CPU copy-out; the owner's separate domain describes physical engine
visibility. Cancelling the upper operation removes its future copy-out while
the owner retains any active physical stage. A physical-loss hook invalidates
attached virtual/wire sessions. The caller installs the hook and supplies the
port driver. The [P4/C6 port](../../arch/esp32c6/README.md) supplies the
hardware SDMMC and SDIO drivers, access domains and resident-loader update
path. The [RP2350 target ports](../../arch/arm-rp2350/IPC.md) connect SPI1
and I²C1 FIFO interrupts to the common target hooks.

## Software conformance

```sh
make -C TikuBench/tests/host/kernel ipc ipc-transports npu-async ipc-mutations
```

The wire tests transfer 600 KiB directly, through the packet model, and through
the SDIO owner/packet stack, with slow consumers, corrupt and dropped frames,
duplicates, resets and delayed output. Owner tests exercise control priority,
native/loader separation, stale tokens, failed abort, quarantine and clock
wrap. SPI/I²C tests connect controller exchanges to the common target hooks;
UART tests cover partial transfers and framing recovery. Mutation tests require
assertion failures when the matching ownership or integrity guard is removed.
Physical controller, target, cache and timing qualification is port-specific.

## P4/C6 radio service

`TIKU_C6_RADIO_SERVICE_ENABLE=1` on C6 and `TIKU_DRV_WIFI_IPC_ENABLE=1`
on P4 select three mux channels over the native mailbox: `c6/bulk` on 0,
`c6/radio` on 1 and `c6/wifi` on 2. Radio control is pumped before the two
data channels; bulk and Wi-Fi alternate their order. A diagnostic `IPR1`
request selects this profile before packet traffic starts. Its reply must
match before the P4 admits mux traffic. A C6 service image also accepts the
legacy `IPCE` profile when selected first. Changing packet profiles requires
a physical session restart.

The radio control header is 16 bytes, followed by at most 48 payload bytes.
Credential objects use a managed block and may reach 114 bytes.

| Offset | Bytes | Field |
|--------|-------|-------|
| 0 | 2 | ASCII `RW` |
| 2 | 1 | Version 1 |
| 3 | 1 | Operation; bit 7 marks a response |
| 4 | 4 | Nonzero request identifier, little-endian |
| 8 | 4 | Claim generation, little-endian |
| 12 | 1 | Service result; zero in requests |
| 13 | 1 | Payload length |
| 14 | 2 | Reserved, zero |

Operations 1 through 12 are discovery, claim, renew, release, status, power,
scan, AP result, credentials, connect, disconnect and forget. Responses use
service result numbers rather than native driver errors. The server caches
the last control request and response. Credentials are staged separately,
committed by a connect request with the same identifier, and wiped after
that call. SHA-256 fingerprints detect changed credential retries without
retaining their secret payload after commit. The wired channel and packet
CRC do not authenticate a peer.

Discovery returns capability bits 0 (open station) and 1 (WPA2-PSK station),
the C6 build, maximum Ethernet frame 1514, queue depth two and lease duration
ten seconds. A claim belongs to its control session; renewals occur every
two seconds. Status has two pages, and scan results carry a generation so
an AP index cannot silently refer to a later scan.

Ethernet frames use the existing 28-byte managed stream header with up to
484 payload bytes per 512-byte block. C6 has two whole-frame slots per
direction; the P4 application profile has four. A partial frame occupies one.
Reassembly publishes only a complete,
validated frame and expires partial input after two seconds. The sender
retains its managed loans through final completion or confirmed cancellation.
Transport staging is scrubbed after final access in the radio profiles.
Physical readiness or session loss invalidates the three logical wires before
their next packet pump. The SDIO owner retains the last explicitly requested
mode separately from its automatic loader recovery, so P4 can retry a failed
native service without interrupting a requested loader or factory session.

The [P4 wireless driver](../../drivers/wifi/ipc/README.md) describes the build,
application interface, ownership and qualification commands.
