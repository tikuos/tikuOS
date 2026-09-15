# Recoverable configuration: experimental first slice

This implements **opt-in durable desired configuration**, not general VFS
transactions, execution checkpoints, or exactly-once physical actuation.
The two enrolled resource contracts are device name (ID 1, schema 1) and
CPU target frequency (ID 2, schema 1). Existing path/node IDs are unchanged;
these stable IDs belong to the separate configuration contract namespace.

## Build and enrollment

Build with `make MCU=nrf54l15 TIKU_VFS_CONFIG=1` (or an MSP430 target).
The feature defaults off. The direct FRAM/RRAM adapter reserves 800 bytes of
durable storage, a 400-byte RAM image plus engine bookkeeping, and uses
bounded scratch buffers during operations. The build selects 256-byte shell
lines so the complete checksummed request fits, including on MSP430.

On other platforms this build option exposes only
`/sys/config/status = v1:unsupported:-`. Existing settings continue to work.
In particular, two records inside one erase/rewrite flash mirror are NOT
advertised as independent crash-consistent banks.

On direct-NVM targets, `cat /sys/config/status` initially reports
`v1:unenrolled:-`. Enrollment is explicit, never a side effect of discovery.
TikuDesktop now offers **Settings > Devices Preferences > Configuration
recovery > Enable recovery…**, followed by an inline confirmation. It obtains
a fresh OS-random token, stores it locally before transmission, and verifies
the device's reported incarnation. Enrollment is only offered for writable
virgin storage. It does not restart the device or change its current settings.

For manual enrollment instead:
Generate a **fresh random 128-bit token for this device and this enrollment**
(for example `python3 -c 'import secrets; print(secrets.token_hex(16))'`) and use:

```text
write /sys/config/provision <32 lowercase hex digits>
cat /sys/config/status
```

Do not reuse a token on another device, after erasing storage, or as a static
firmware constant. Tokens identify storage incarnations, not authority.
Provisioning requires both SYS and FS capabilities. It only accepts virgin
(uniform zero/erased) banks or a retry of the already committed incarnation.
It does not erase, migrate, or silently reformat damaged/incompatible data.
An interrupted initial enrollment may require deliberate service recovery;
there is no automatic repair or factory-reset endpoint in this first version.
The banks live in `.persistent`, which a flasher does not clear, so a board
that has run earlier firmware usually holds unrelated bytes where they now
sit. Those banks are not virgin, `/sys/config/status` reports `fault`, and
enrollment is refused: the engine never reformats data it cannot read. Erase
the durable region before enabling recovery on a board that has been used.

Storage the engine cannot read leaves `/sys/device/name` and
`/sys/cpu/freq_target` writable as unversioned settings: no reconciliation
can run while the banks are unreadable, and nothing else would restore them.
`/sys/config/status` reports `fault` throughout. A poisoned instance (a
storage write that failed after its gate may have persisted) and an
exhausted counter still refuse those writes, because their journal is
intact and would put the old value back at the next boot.

Enrollment initially leaves both resources at revision zero, using their
existing values. The first managed/legacy write captures that resource's
desired value. Once enrolled, ordinary writes to `/sys/device/name` and
`/sys/cpu/freq_target`, including shell and BASIC callers, also advance their
durable revision. Direct internal HAL calls are outside this VFS contract.

## Interface

`/sys/config/manifest` maps stable IDs, schema versions, original paths,
managed paths, resource kinds and recovery policies. These are additive
endpoints; the existing global manifest format is not changed.

| Endpoint | Purpose |
| --- | --- |
| `/sys/config/status` | Version, readiness and incarnation |
| `/sys/config/provision` | Read readiness; explicit enrollment write; no overwrite/reset |
| `/sys/config/name` | Revisioned device name |
| `/sys/config/frequency` | Revisioned CPU target frequency |
| `/sys/config/history` | Four most recent accepted operations; non-consuming |

A managed resource read returns:

```text
v1:<incarnation32>:<revision8>:<state>:<desired-value-hex>
```

A managed write receives one whitespace-free, lowercase-hex token:

```text
<incarnation32>:<expected-revision8>:<request-token32>:<value-hex>:<crc32-8>
```

The trailing CRC32 is the standard IEEE CRC32 over all preceding ASCII bytes,
excluding its separating colon. It rejects incomplete console commands and
accidental corruption; it is **not authentication**. Names are at most 31
bytes; no embedded NUL/control bytes or silent truncation, including a
trailing 0x0a, which belongs to the shell's legacy line and never to a
managed value. Clock values must
match a rate currently advertised by the board's reboot-only clock policy.

The caller freezes the ENTIRE request across retries. Operation identity
includes incarnation, resource and expected revision as well as the request
token. An identical retained request returns its receipt without applying it
again, even if another client has since changed the setting. Altering its
value/token at that revision conflicts. Evicted requests cannot re-execute:
their expected revision is old. There is no counter wrap; exhaustion refuses
further mutations. No finite journal promises indefinite receipt retention.

History lines contain `resource:revision:request-token:state:value-hex`, newest
first. Read status around history when correlating it across reconnects. The
C API also provides incarnation/payload-bound `tiku_cfg_lookup()`.

## Guarantees and failure boundaries

The engine commits desired value, revision and request identity together
before invoking a convergent setting handler. It then commits the observed
outcome. States distinguish `unset`, `accepted`, `applied`, `restart`, and
`blocked`. Success on the managed endpoint means **durably accepted**;
inspect state rather than assuming hardware changed.

Recovery validates the format, CRC and resource schemas, rechecks current
policy, and reconciles only the latest desired value of each resource. Saving
a CPU target never resets or retunes a running CPU. If a cut occurs before the
old platform clock-preference cell is updated, the first recovery boot may
need one further manual restart; the endpoint reports `restart` honestly.

The two-bank backend requires ordered durable writes and failure isolation
between banks. Target-bank invalidation, body and final gate are separate
ordered operations; the previous active bank is untouched. Failed storage
I/O poisons the instance until re-open/reboot, since a reported failure might
have occurred after the final gate persisted. CRCs detect accidental damage,
not malicious tampering; arbitrary post-commit media corruption is outside
the power-cut guarantee. Schema changes require an explicit future migration,
not reinterpretation of old native structs/pointers.
The first direct-NVM adapter uses linker-placed banks in `.persistent`;
power-cut tests assume a fixed firmware/storage layout. Preserving those bank
locations, or migrating them explicitly, is required across firmware layout
changes. This prototype does not claim transparent firmware-update migration.

The engine is bounded and synchronous, for the serialized cooperative kernel
context only. It rejects reentry; it is not a multi-thread/ISR API. Persistent
sensor samples, durable event cursors, dynamically created file identities,
non-idempotent commands and cross-resource transactions are not implemented.

## Desktop

The storage kit provides a strict v1 codec and snapshot/write helpers.
Device Preferences discovers enrollment and saves using the revision from
the displayed snapshot. Availability refreshes do not replace that revision.
Conflicts require resolution; an unreadable advertised recovery endpoint never triggers a
fallback to an unversioned write. Old, unsupported and unenrolled firmware
retain the previous preferences workflow: a local outbox that is missing,
misconfigured or unreadable reports itself, but only holds the editors of a
board that is actually using recovery, or one an unresolved record names.
Desktop reads never enroll a board.

The UI distinguishes a saved configuration from confirmed application and a
clock rate awaiting restart. The transport still uses existing VFS endpoints,
so no shared host/device C structure layout or new RPC ABI is needed.
Name and clock saves on enrolled devices use a persistent host outbox at
`$HOME/.config/tracker/config-requests` (override with an absolute
`TIKU_CONFIG_OUTBOX` path). One immutable request per live hardware UID is
published without overwriting an existing record. File and directory flushes
precede transmission, including on retry; a local storage failure prevents
sending. A private-directory lock serializes cooperating desktop processes.
The record has a version and checksum and binds the UID, resource, incarnation,
original revision, request token and exact payload. Names and serial ports
are not accepted as recovery identities.

Opening the panel or refreshing only reads the device and outbox. It never
enrolls, retries, deletes an unresolved request, or resets a board implicitly.
After a disconnect or desktop restart, Configuration recovery offers:

- **Retry saved request** when the original revision still holds. The worker
  rechecks the identity and receipt, then sends the identical request if safe.
- **Dismiss record** when a matching receipt confirms acceptance. A newer
  setting does not invalidate that historical receipt.
- **Forget request…**, with inline confirmation, for an unresolved/conflicting
  request. This removes only the local record, not a device operation.

New name/clock saves are blocked while a local request remains unresolved.
A changed storage incarnation makes the old request ineligible for retry.
If history has expired, the UI says acceptance is unknown, rather than inferring
success from a similar current value or an empty transport acknowledgement.
Corrupt local records are retained and require deliberate local inspection.
Enrollment tokens are never replayed on virgin storage: that could reuse an
old incarnation after a factory erase. Forget an uncertain enrollment record
explicitly and use a fresh token instead.

The outbox is local to this computer/user, not a shared multi-host transaction
log or an authentication mechanism. It requires a local filesystem supporting
private directories, advisory locks, atomic hard-link publication and durable
file/directory sync. It is not qualified for network filesystems. Linux uses
`fsync`; the Darwin system adapter also issues `F_FULLFSYNC`. macOS runtime and
physical host/device power-cut qualification remain required. A host cut may
leave harmless `.request-*` staging files; these are never treated as pending
commands or replayed.

## Verification

TikuBench's normal host kernel suite includes the engine and production
adapter tests:

```sh
make -C TikuBench/tests/host/kernel run
```

Both TikuBench frontends (GTK and TikuDesktop) expose **Files & events (VFS) >
VFS configuration recovery (host tests)**. Expand it to choose any of seven
independent groups:

| Group | Coverage |
| --- | --- |
| `engine` | Durable journal, simulated power cuts, duplicate requests and corruption |
| `adapter` | Production VFS adapter, enrollment, permissions, wire format and clock policy |
| `codec` | Desktop protocol validation, truncation and original revisions |
| `outbox` | Host crash/restart, publication failures, UID binding and receipt resolution |
| `preferences` | Device Preferences capabilities, recovery states and UI controls |
| `frontend` | Desktop Bench catalogue, selection, results, cancellation and device handoff |
| `host-sync` | Host durable file-flush contract |

`vfs-recovery-host` is selected by name, not by a board sweep: `tikubench
all` qualifies a board, and this suite needs the applications checkout that
a farm host need not have. Use `--include vfs-recovery-host` to add it.

No board is required. Firmware, serial, peripheral and MCU options are ignored
for this suite; it never opens, releases, flashes, enrolls or resets a device.
Mixed host/device selections still acquire the board for their device work.
Host results are recorded as `host`, not as qualification of the selected MCU.
Each group builds its test binary before running it, and a missing checkout,
build failure or test failure is a failed group, not a skip.

```sh
cd TikuBench
python3 -m tikubench run vfs-recovery-host
python3 -m tikubench run vfs-recovery-host --only engine,adapter,outbox
```

The engine tests inject cuts
at 809 update boundaries (intent and outcome) and 405 initial-enrollment
boundaries, including loss of the final commit acknowledgement. Tests cover
duplicate requests, interleaving writers, receipt eviction, capability denial,
incarnation/schema mismatch, invalid storage, legacy writes, wire truncation,
clock restart policy and a torn legacy name cell.

Desktop tests: `test_config`, `test_config_outbox`, `test_deviceprefs`,
`test_bench` and `test_sys`, included in the normal applications test build
and the selectable suite above. These cover strict
codecs, original-revision writes, actual writer-process exit, disk flush and
publication failures, lost acknowledgements, reconnect/receipt resolution,
UID/incarnation mismatch, expired history, enrollment, stale local dismissal,
symlink/corruption rejection, writer locking, permission checks and UI gestures.

```sh
cd applications
make build/tests/test_config build/tests/test_config_outbox \
     build/tests/test_deviceprefs build/tests/test_bench build/tests/test_sys
build/tests/test_config
build/tests/test_config_outbox
build/tests/test_deviceprefs
build/tests/test_bench
build/tests/test_sys
```

An additional, explicitly selected **VFS recovery contract (enrolled device)**
suite (`vfs-recovery-device`) checks an actual device's metadata, rejection
behavior and duplicate receipts. It requires exclusive access to existing
recovery-enabled, enrolled firmware. It never builds/flashes, enrolls or
reboots the device, even if the GUI's Build + flash option is set.

Its `metadata` group is read-only. The `reject` group attempts invalid and
read-only writes and checks that values, revisions and receipts stay unchanged.
The `duplicate` group requires the name to have been saved at least once after
enrollment; it saves exactly the same name, verifies the receipt, then retries
the identical request and checks that it has no second effect. It does not
change setting values, but **advances the name revision once and consumes one
receipt-history slot**. Do not run it while another client is configuring the
device. A lost acknowledgement fails the run without reconnecting/resending.
Remaining groups are skipped after a device-contract failure.

```sh
# From TikuBench; replace board and port with the already-enrolled device.
python3 -m tikubench run vfs-recovery-device --board nrf54l15 \
    --port /dev/your-device --skip-build --only metadata
# Omit --only metadata to include the invalid-write and no-op-save checks.
```

This suite is excluded from default `all` and farm sweeps. Its host-side tests
use simulated devices to verify safety preconditions, failure detection,
byte-exact names and no automatic retries. Runner/GUI integration regressions:

```sh
cd TikuBench
PYTHONPATH=. python3 -m unittest discover -s tests/host/python -p 'test_*.py'
```

Physical power-cut qualification is still required on FRAM/RRAM hardware.
Compilation and simulated byte cuts do not establish controller behavior
during brownout. Flash/MRAM-mirror adapters need independent durable bank
storage before opting in. Keep the build flag off in release firmware until
the relevant backend has passed hardware qualification.
