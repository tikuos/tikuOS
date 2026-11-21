# Paced transfer listener

`tiku_media_transfer_listener` binds the queued packet engine to the command
controller. Its caller reserves aligned storage, supplies a mounted store and
nonzero client identity, and provides serialized transport callbacks. Media
owners and the network owner run separately. Initialization clears fixed
metadata; preparation and cleanup clear session backing in 512-byte steps.

Enable verifies the provisioned volume before preparing a keyed session and
attaching its endpoint. The profile remains immutable until Stop completes.
Each received frame has a four-byte little-endian length followed by one TMT1
packet. Transport calls copy at most 512 bytes. The packet engine retains input
through request retirement; the listener copies its completed reply before
reusing that input buffer. Send backpressure preserves unsent bytes.

Stop removes the endpoint, cancels the session's queued request through the
provided media-owner callback, retires its connection, releases its store
handles and erases session storage. Foreign queue requests remain owned by
their clients. Cleanup retries retain backing. Interrupted append recovery
uses the store's failed-mount and committed-prefix contracts.

Peer disconnect, malformed framing and 120 seconds of inactivity initiate
cleanup followed by preparation and relisten. The live snapshot reports not
ready during recovery. A recovery error leaves resources available for Stop.
The controller records command completion separately from live readiness;
consumers read both. Stop preempts automatic recovery.

## TCP binding

`tiku_media_transfer_tcp` supplies callbacks using the deferred TCP API. Its
caller owns the controller/listener reservation and schedules media progression.
One callback binding can exist. Configure, submit, take and observe expose typed
commands and copied state; credentials stay in the controller and session.
Returning from a GUI application does not unbind the service.

Accepted connections remain reserved through cancellation. A link-loss callback
can arrive after TCP released its slot; the binding records that release and
does not later retire a different connection occupying the same address.
Rejected connections are quiesced and retired in bounded steps. Endpoint removal
scans one table slot per call for inherited listener callbacks, including passive
handshakes that have not reported CONNECTED. Other listeners remain attached.

The TCP binding performs no link output from application progression. The
network owner schedules deferred output and retransmission timers. Its link
provider can have a separate latency bound. Unbind requires Stop, consumed
command completions and Forget. The caller keeps storage and the mounted store
alive through unbind and must not reset live TCP tables while reservations exist.

## Independent S31 service

`TIKU_MEDIA_TRANSFER_SERVICE_ENABLE=1` supplies a PSRAM reservation, queued media
owners and a dedicated process. Console provisioning reserves backing and
initializes fixed metadata. Enable performs paced preparation and attaches the
listener. The process runs at most 32 continuation turns or 2000 microseconds
before yielding. Its shell pump wakes the process; GUI visibility does not own
service progression.

`media profile PORT KEY_HEX` provisions a stopped endpoint. `media transfers`
prints copied lifecycle and progress state; `media ready` returns zero for a
ready listener and one while starting or recovering. `media serve` and
`media off` can return pending command admission. The host transfer tool waits
for readiness after pending startup. `media forget` erases provisioning after
Stop and consumed command outcomes, then releases backing. Allocation release
retries retain metadata outside the released arena.

GUI callers use configure/submit/take/observe through typed bindings. Their
completion tokens remain caller-owned. Console-created tokens are consumed by
the service. Legacy console transport and the TCP service exclude simultaneous
ownership. TCP initialization precedes provisioning; a later network startup
uses the same initialized table.
