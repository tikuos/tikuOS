# Paced transfer packets

`tiku_media_transfer_async.h` implements the TMT1 operations over the queued
media owners. It uses the wire format in `media-transfer.md`, including exact
retry replies, keyed HELLO, atomic uploads, durable prefix resume, snapshot
reads and namespace-versioned listing. Its entrypoints do not call the
synchronous packet handler or execute storage requests inline.

The caller supplies separate packet-control and session storage, binds a mounted
store and a nonzero client ID, and schedules protocol and media progression in
serialized owner context. Initialization clears fixed control fields. The caller
wipes reused session backing before initialization; staging remains unread until
its valid byte count is set. The mounted generation is captured at initialization.
Packets from a replaced mount are dropped and authentication is invalidated.

Submission borrows one immutable packet and returns before checksum validation
or storage work. Repeated submission while pending returns BUSY. Steps validate
at most 64 checksum/comparison bytes, copy at most 512 payload/cache bytes, or
advance one protocol or queue phase. Queue backpressure preserves other clients'
requests. A completed reply remains session-owned until the next submission or
cancellation. Malformed frames and conflicting retries have no reply; accepted
operation errors have normal checksummed replies.

BEGIN performs a versioned lookup and submits the expected destination ID and
namespace epoch with its upload request. OPEN uses the same checked lookup and
snapshot acquisition sequence. A namespace change between those phases refuses
the request. RESUME loads the identified incomplete object, checks its atomic
policy, name, expected length and digest, and releases a mismatched writer.
The transfer client has READ and WRITE permissions; it does not need ADMIN.

DATA stages bytes in 64 KiB batches. APPEND and SYNC run as separate queued
requests before the protocol acknowledges a new durable boundary. FINISH
appends any remaining bytes, verifies the digest and publishes the final file.
Failed upload progression requires release and resume. Downloads use the same
staging reservation for read-ahead when no upload owns it. A concurrent snapshot
read during upload writes directly to the reply buffer. LIST and NEXT retain the
visible namespace epoch; private upload prefix commits do not invalidate it.

Cancellation returns BUSY while its request remains queued or running. Use
`tiku_media_transfer_async_request()` to obtain that request, retire it through
the matching media owner, then cancel the packet. Cancellation invalidates
retry state and authentication. Completed filesystem publication is retained,
and service-owned handles remain available for explicit cleanup. Asynchronous
media builds accept RELEASE on a failed store; it drops writer ownership without
I/O and preserves the failed-mount state. CLOSE retains its reader cleanup role.

The synchronous transfer server remains a separate consumer. Integrating a
listener requires transport admission, bounded reads/sends/retirement, service
memory placement and the command lifecycle in `transfer-control.md`.
