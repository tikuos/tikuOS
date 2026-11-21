# Transfer command lifecycle

`tiku_media_transfer_control.h` defines a service-owned command controller.
It separates configuration, admission, backend progression and retained
completion results. It allocates no memory and declares no mutable global
state. The caller supplies aligned storage using `tiku_mt_control_size()` and
`tiku_mt_control_align()`, a serialized-owner predicate and a backend step
callback. Controller storage and callback context outlive outstanding tokens.

## Configuration and admission

Provisioning supplies a nonzero TCP port, a nonzero 32-byte key and a nonzero
16-byte volume identity. The controller copies that profile while stopped and
after outstanding results have been taken. Configuration advances a 64-bit
generation. The public snapshot contains the generation, lifecycle state,
pending tokens, cleanup requirement, last backend error and free result slots;
it does not contain the key or a pointer to the profile.

Enable and Stop require the current generation. Admission performs no backend
step. Enable is accepted while stopped. Stop can cancel a pending Enable or
retry cleanup after failure. Repeating a pending Stop returns its original
token. Starting another Enable while one is active returns busy.

Two slots retain command results until `tiku_mt_control_take()` consumes them.
Taking a pending token returns `TIKU_MT_CONTROL_PENDING` without freeing its
slot or changing the result output. Taking a terminal token returns zero and
writes its operation result, which can be a failure. Taking it again returns
stale. A full set of retained results refuses additional commands. Token
sequence exhaustion refuses reuse; Enable reserves the final token for Stop.

`tiku_mt_control_forget()` erases the profile while stopped with no retained
results. At generation exhaustion it still erases credentials and enters the
unconfigured state; further configuration is refused. A controller lifetime
ends after its resources and consumer references retire. Reinitializing its
storage while a consumer retains tokens violates the lifetime contract.

## Backend progression

Each `tiku_mt_control_step()` calls at most one backend phase. The backend
returns zero for completion, pending or busy to retry, or a negative failure.
Callbacks must return within their declared work budget; the controller does
not preempt an unbounded callback. Allocate backing before admitting commands
and split initialization, retirement and clearing into backend steps.

| Phase | Required behavior |
|---|---|
| PREPARE | Validate the provisioned volume and prepare reserved session backing in bounded work units |
| ATTACH | Attach the prepared listener without synchronous network transmission or storage work |
| QUIESCE | Reject new work and cancel/drain pending PREPARE or ATTACH work |
| DETACH | Retire transports and their borrowed buffers before releasing session state |
| RELEASE | Release this service's writer/readers while preserving other clients and committed upload prefixes |
| WIPE | Clear and retire session resources; preserve the controller-owned profile for a later Enable |

Successful Enable advances PREPARE then ATTACH and enters ready. Stop advances
QUIESCE, DETACH, RELEASE and WIPE before reporting completion. QUIESCE must
also work when Stop arrives before the first PREPARE call. Cleanup phases
accept an empty resource set. Each new session resets backend wipe cursors. An Enable error
enters cleanup and retains its original failure result. Cancelling Enable
reports cancelled after successful cleanup. A cleanup error enters failed with
`cleanup_required` set; it does not imply that resources were released. Take
the failure result before submitting a Stop retry if the result slots are full.
Enable and profile replacement remain refused until cleanup succeeds.

The profile pointer is borrowed for a callback; backend resources must not
outlive their controller. Callbacks cannot reenter this controller. Wrong-owner calls are refused before
mutation. Observation reads state and does not progress cleanup. The error
field retains the latest backend fault; configuration or a new Enable clears it.
Consumer timeouts do not cancel a command or release its backend resources.

## Media and memory helpers

`tiku_mt_control_release_one()` releases one writer or one reader owned by the
specified client. An occupied or running store returns busy before submission,
so the helper does not consume another client's queued request. It uses CLOSE
and RELEASE, which do not acquire a medium session. These operations release
ownership; they do not finalize, delete or publish an upload. A partial atomic
upload retains its committed prefix and the old visible file remains available.

The helper returns pending after one successful release and zero when no owned
handles remain. A media error leaves further cleanup to the owner. Use the
mounted store whose lifetime and identity the backend holds; this helper does
not acquire a card or repair an invalidated store.

`tiku_mt_control_wipe()` clears at most 512 bytes per call and advances a cursor.
The cursor must remain outside the buffer being cleared. Zero size is accepted;
an out-of-range cursor is refused before mutation. This work bound does not
establish an execution-time bound on a specific memory tier.

## Listener integration

The existing `tiku_media_transfer_server_start()` and `stop()` entrypoints do
not call this controller. A listener backend must preserve its configuration
generation and resource ownership across the callback phases. The current TCP
abort sends a reset packet synchronously, and transfer packet execution can
perform synchronous media work. Those calls do not fulfill the bounded backend
contract by being invoked from a phase callback. Firmware composition selects
and schedules the controller after its backend and memory placement are admitted.
