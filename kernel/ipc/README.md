# IPC endpoints and managed buffers

`TIKU_IPC_ENABLE=1` builds named endpoints, bounded message queues, managed
buffer loans and a two-chunk stream window. The default is 0. Disabled builds
have no IPC tables, timer, observers, VFS entries or scheduler hooks.

Boot initializes IPC after memory, processes and timers. The default table
in `boot/tiku_boot_ipc.c` pairs `local0` and `local1`. Enabling the P4 LP
payload adds `lp`; enabling RA8P1 CPU1 adds `cpu1`. Nordic FLPR adds
`flpr`, RA8P1 NPU adds `npu/inference`, and `TIKU_IPC_CONSOLE_ENABLE=1`
adds `console/ipc`. On FireBeetle P4,
`TIKU_SDIO_C6_ENABLE=1` adds the hardware-backed `c6/bulk` endpoint.
Endpoint names are relative paths of at most 30 bytes, including separators.
An endpoint cannot be an ancestor of another endpoint.

## Application interface

Include `kernel/ipc/tiku_ipc.h`. Open an endpoint from its receiving process;
each binding belongs to one process instance. A local endpoint sends to its
configured peer. A transport endpoint submits through its backend. Query
capabilities before choosing message or buffer sizes.

| Operation | Contract |
|-----------|----------|
| `tiku_ipc_send()` | Copies accepted bytes; retains no source pointer. `AGAIN` accepts nothing |
| `tiku_ipc_receive()` | Pops a copied message or a buffer receipt |
| `tiku_ipc_buffer_alloc()` | Allocates from the private TRANSIENT pool |
| `tiku_ipc_buffer_map()` | Maps caller storage while no loan or quarantine holds it; failed OUTPUT remains invalid |
| `tiku_ipc_submit()` | Retains a buffer independently of caller ownership; returns a submission handle |
| `tiku_ipc_loan_read()` / `loan_write()` | Maps a received INPUT / OUTPUT loan for its bound receiver |
| `tiku_ipc_ack()` | Attests that the receiver's final access has ended |
| `tiku_ipc_completion()` | Returns a terminal result and its quarantine flag |
| `tiku_ipc_cancel()` | Requests cancellation; it does not attest hardware quiescence |
| `tiku_ipc_buffer_release()` | Drops caller ownership; active operations retain their references |

Process-facing mutators run in kernel process context. Backend ingress,
completion and service-deadline publication are IRQ-safe. Caller-buffer
registration is absent: copy foreign or stack data into a managed buffer.
Keep INPUT bytes unchanged throughout a loan. A CPU pointer obtained before
submission still refers to the same storage; ownership is a caller contract.

Endpoint, buffer and submission handles have distinct C types and generation
namespaces. They retain the full 32-bit generation and 16-bit slot. Exhausted
slots retire. Closing a binding or exiting its process advances the logical
session, so delayed ingress cannot reach a replacement binding. Ack received
loans before closing their endpoint.

Readiness arrives as `TIKU_EVENT_IPC`, with NULL data. Drain receive and
completion queues from the handler. The event is coalesced per process
instance; a partial drain rearms it. Failed notification posts retain
readiness, and scheduler progress retries without another send. Reclaim-gated
owners retain their queues without busy retrying.

One kernel-owned callback timer tracks backend polling, retries and operation
timeouts. Future deadlines participate in tickless idle; immediate readiness
prevents idle. `tiku_sched_run_once_except(owner)` services progress while
excluding the waiting owner and preserving its process context. Reclaim keeps
its guard against polling from a current process.

## Resource bounds

Defaults are in `tiku_ipc_config.h`.

| Resource | Default |
|----------|---------|
| Endpoint slots | 8 |
| Receive entries per endpoint | 4 |
| Copied message bytes | 64 |
| Submission slots | 8 |
| Managed buffers | 4 × 512 bytes |
| Stream window | 2 outstanding chunks |
| Progress budget | 4 |

The buffer pool is created on first allocation. Each live block retains its
backing reservation. Tier reset, pool/arena reset and destruction, and forced
PSRAM detach refuse retained backing. Caller release, process exit, timeout
and cancellation do not release an active loan without quiescence. An
unconfirmed operation stays quarantined; an exact late completion can release
its backing without applying to another generation.

Ordinary and raw pool constructors trust caller-owned ranges and bypass region
claims. Raw arenas do too. These APIs may not target another subsystem's
carve or loan. They do not provide allocator-wide isolation.

## Streaming

`tiku_ipc_stream.h` provides caller-owned sender and reader state. Accepted
writes copy into managed buffers; the kernel retains neither the state object
nor the source pointer. Two pending submissions bound the sender window.
Account for completions with `tiku_ipc_stream_finish()`.

A chunk has a 28-byte little-endian header: version magic, source session,
object id, total length, offset, payload length, final/discontinuity flags and
CRC-32. With
512-byte buffers it carries at most 484 object bytes. Object ids increase
within a session. The reader validates lengths, sequence and checksum before
returning a view, then the application consumes and acks the chunk. Publishing
a whole object atomically requires application storage for that object.

`tiku_ipc_stream_offer()` selects preserve, drop-new or latest-object admission.
Preserve returns `AGAIN` while an object is active. Drop-new returns `CANCELLED`
and counts one drop per offered object id. Latest cancels the preceding
object's outstanding chunks; drain their completions before the replacement
is admitted. Its first chunk carries a discontinuity marker. Reader state
counts an incomplete object replaced by that marker.

## Shared-memory backend

`services/ipc/tiku_ipc_shared.c` stages transfers through a boot-claimed carve.
Its 1,536-byte layout has two 512-byte payload slots, separate request and reply
lines, and an independent copied-control exchange. Control is serviced before
bulk slots. The peer consumes INPUT, generates a sequence pattern for OUTPUT,
and echoes copied control messages. Application-specific peer processing can
replace the diagnostic peer loop while preserving its ownership protocol.

| Port | Mapping and claim |
|------|-------------------|
| ESP32-P4 LP | Ring at `0x5010f000` in the existing LP page; a SHARED region row covers LP SRAM and a boot claim protects the ring |
| RA8P1 CPU1 | Ring at `0x22001ac0`; a boot claim covers the 16 KiB CPU1 area |

The LP accesses its page uncached. CPU1 marks the legacy mailbox and ring
non-cacheable; the M85 cleans host-written ranges and invalidates peer-written
ranges. Writers occupy separate 64-byte lines, including control, replies and
payload ownership boundaries. Both toolchains assert layout and carve bounds.

A boot marker makes a restarted peer wait for a new host epoch. The host
invalidates old core sessions and waits for the epoch acknowledgement before
reusing shared slots. The shared backend never exposes private pool addresses
to a peer and uses no DMA. Cancellation disables future copy-out before
returning private backing; orphaned fixed slots remain held until acknowledged.

The LP services IPC between I²C address probes and between agent jobs. Its
service-gap counter reports peer cycles; the pacing-loop comment is not a
latency bound. CPU1's cycle-gap field is zero because that payload does not
sample its cycle counter.

## Status, watches and reset

`/sys/ipc/<name>/` has eight files: `state`, `caps`, `queue`, `credits`,
`counters`, `buffers`, `transport` and `reset`. Boot builds stable nodes so
VFS watches retain their identities. Progress compares status snapshots and
notifies its observable files when an endpoint snapshot changes. `TIKU_IPC_VFS_NODES_MAX` bounds the complete tree,
including intermediate directories. Insufficient capacity returns `E2BIG`
before publication. The default is `1 + 10 * TIKU_IPC_ENDPOINTS_MAX`; the P4
firmware profile selects 40 nodes and permits an explicit override.

Queue credits describe local receive capacity. Buffer counts describe the
shared private pool. `invalid_handles_global` is a saturating count of rejected
endpoint, submission and buffer identity lookups across the system. The wire
fault counters in `transport` belong to that endpoint's wire context.

Writing `1` to `reset` requires the SYS capability. The advertised reset scope
is endpoint, connection or engine; unsupported backends return `ENOTSUP`.
Shared-memory reset starts an epoch negotiation without power-cycling the
peer. The NPU reset confirms engine abort before releasing its transfer.
A reset request cannot attest that hardware has stopped accessing memory.

## Native compatibility and accelerators

With IPC enabled, the coprocessor interface uses the default endpoint's
synchronous native-control provider. It preserves the driver's message cap,
latest-unread replacement, opaque reply marker, flags and lifecycle. These
native calls retain no caller pointers. Queued IPC messages keep their own
admission and delivery contract. The provider registration belongs to boot;
control calls from backend progress or worker-thread context are refused.

`interfaces/npu/tiku_npu.h` exposes asynchronous submit, poll and cancel.
`tiku_npu_run()` submits a job and waits through the scheduler with its owner
excluded. The adapter calls the driver engine interface, stages through its
validated tensor spans and fences model generations. Small INPUT/OUTPUT loans
copy through those spans. Large models use the engine-owned buffers whose
lengths are independent of the managed IPC block size. An unconfirmed abort
keeps the engine occupied, refuses reload/stop and reports failure to the
caller. Output becomes readable after coherent success.

The whole-message wire protocol, console, UART, packet-bus adapters and SDIO
owner contracts are in [services/ipc/README.md](../../services/ipc/README.md).

## Build and checks

For the FireBeetle 2 P4, use the configured RISC-V toolchain and esptool paths:

```sh
make MCU=esp32p4 P4_PROFILE=firmware BOARD=firebeetle2_esp32p4 \
  TIKU_SHELL_ENABLE=1 TIKU_IPC_ENABLE=1 TIKU_LPCORE_ENABLE=1
```

For RA8P1, select `TIKU_DRV_CPU1_ENABLE=1` with `TIKU_IPC_ENABLE=1`. The
IPC-enabled CPU1 layout places working state after the ring and leaves a
2 KiB stack floor. Disabled builds retain the original placement.

```sh
make -C TikuBench/tests/host/kernel ipc ipc-transports npu-async ipc-mutations
make -B -C TikuBench/tests/host/kernel ipc ipc-transports npu-async \
  EXTRA_CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
```

The host suites exercise narrow clock/memory fields, generation retirement,
reclaim gating, nested waits, failed posts, restart, clock wrap, timer lifetime,
loans and quarantine. Cached and same-address uncached peer models check
ownership and publication order. The transport tests cover fragmentation,
corruption, duplicates, slow consumers, delayed completion and reset. Mutation
checks remove guards and require the corresponding assertions to fail.

On an installed IPC image:

```text
ipc
ipc test lp
cat /sys/ipc/lp/transport
```

`ipc test` consumes 600 KiB through two 512-byte buffers while exchanging
control messages, checks the peer's byte count and CRC, and reports backing
return. Its declared control threshold is 128 kernel ticks. The TikuBench
`ipc` suite runs the object twice in the same session and checks both reports:

```sh
cd TikuBench
python -m tikubench run ipc --board firebeetle2_p4 --port <explicit-port>
```

The RA8P1 suite uses `--board ra8p1` and endpoint `cpu1`. `ipc echo console/ipc
<ticks>` echoes copied wire messages for a bounded interval while servicing
other processes. Board qualification and timing profiles are recorded
separately from source contracts and successful cross-builds.
