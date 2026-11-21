# Paced media uploads

The asynchronous mutation owner accepts BEGIN, RESUME, APPEND, SYNC and
FINALIZE. Requests use the media queue and retain their buffers until `done`.
The caller schedules `tiku_media_mutation_step()` with a polled transport;
this path does not invoke synchronous block callbacks.

BEGIN uses `id` as a destination precondition: zero requires an absent name,
and a nonzero value identifies the completed file being replaced. STREAM
requires an absent name. ATOMIC requires an expected length in `offset` and
SHA-256 in `digest`. Successful completion returns the new ID and assigns the
writer to `client`. A supplied `info` receives the initial object metadata.
The synchronous store's BEGIN retains its existing replacement policy.

RESUME loads an incomplete object by ID and assigns the writer after transport
retirement. A supplied `info` receives its committed length, expected digest
and name. Consumers that require those fields to match a transfer request must
validate them before accepting data and release a mismatched writer.
APPEND, SYNC and FINALIZE require that client's writer and refuse collection
or an active copy operation.

APPEND borrows 1..65536 immutable bytes. A step advances one extent phase,
requests one sector operation, or hashes at most 64 payload bytes. An append
can complete one partial chunk and start the following chunk. Existing prefix
bytes are checked against their stored slice checksums before the resulting
extent root can replace the draft root. Extent paths grow through twelve levels
with sixteen children per node. Hash state and extent metadata remain in the
writer draft until SYNC or FINALIZE publishes them.

SYNC commits the draft through the ordered root and transaction barriers while
retaining writer ownership. Private upload progress does not change the visible
namespace epoch. FINALIZE checks atomic length and SHA-256, publishes the
completed object, and releases the writer. A replacement changes the visible
binding in the same transaction; previously opened readers retain their object.
RELEASE discards the unsynced draft and preserves its committed prefix.

Cancellation retains a pending sector lease until the transport retires it.
Allocation or interrupted writes freeze the mount and require remount for
recovery. A completed transaction remains committed when cancellation arrives
during transport close. The queue records completed BEGIN/RESUME/FINALIZE writer
transitions even when that request subsequently reports cancellation. Cleanup
must inspect service ownership after retirement instead of inferring it from
the cancellation result.

These APIs use the caller-owned mutation storage returned by
`tiku_mfs_mutation_size()`. Extent traversal shares storage with catalog edits;
no file-sized allocation or mutable global state is added. The packet server's
synchronous entrypoints continue to use their existing execution path.
