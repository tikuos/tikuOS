# kernel/db/

Tables of fixed-size records in place on byte-addressable NVM (FRAM, MRAM,
RRAM).  One API, `tiku_db.h`; the medium under it is the kernel's
descriptor (`kernel/memory/tiku_nvm_media.h`), which `tiku_db_media.h`
names for the engine; the extent layout and the write order are
`tiku_db_inplace.c`; the verbs, cursors and the transaction rule are
`tiku_db.c`; the board's medium and extent are `tiku_db_board.c`.  Nothing
here includes `shell/`, `basic/` or `services/`.  `make TIKU_DB_ENABLE=1`
builds it, with the `db` shell command; a sector-flash port refuses it.

## The medium

`tiku_db_media_t` is `tiku_nvm_media_t {unit, tear, endurance,
erase_unit}`.  `unit` is the program granule (a 16-byte line on RRAM and
MRAM, 32 on the RA8P1, the architecture word on FRAM); a record is padded to
whole units and never shares a unit with another record.  The engine builds
where `erase_unit` is 0.  `tear` says what a power cut during a program
leaves in the unit being written:

- `TIKU_DB_TEAR_NONE`: the line lands whole or stays as it was.  The gate
  and the echo share lines with columns; a record of one line is one
  program.
- `TIKU_DB_TEAR_BYTE`: a prefix of the bytes written has landed; no line is
  atomic.  The gate has the first unit (`max(line, 4)` bytes) to itself and
  the echo the last.
- `TIKU_DB_TEAR_LINE`: every byte of the line may read as anything.  As
  `TEAR_BYTE`, and the echo's line also holds the echo's complement; an
  echo without it is not a sequence number.  `unit` is at least 8.

`endurance` is the programs one unit is rated for (the nRF54L RRAM 10⁴,
the Apollo and RA8P1 MRAM 10⁵); 0 means no limit.  `tiku_db_create()` refuses a table whose ring,
at its declared `rate` (inserts per hour) over its declared `life` (days),
would program a line more often than that, and names the capacity that
would do.

## The extent

```
header   64 B    magic last; version, line, tear, limits, identity, size, FNV check
tables   8 x 64 B  gate last; name, column count, record size, capacity, block, FNV check
blocks   per table, 64-aligned: commit ring | schema | ring of records
```

A record is `[gate u32 = seq][columns][echo u32 = seq]`.  `seq` counts
from 1 per table and never repeats a committed number; the slot is
`seq mod capacity`.  A record is live when gate and echo agree, are neither
0 nor ~0, and the value's slot is the record's.  There is no head pointer:
mount finds the highest number handed out by reading every slot's echo (a
deleted or cut slot still carries one), so an insert rewrites nothing but
its own slot.

An insert writes the echo's unit, then the columns between, then the
gate's unit.  Once the echo holds the new number the slot's old record is
unreadable; once the gate holds it the new record is readable; a cut
between the two leaves the slot empty, so a slot is never cleared before
it is written again.  A delete clears the gate.  Each is a commit of its
own.  Mount scans gates and echoes and never writes.

A transaction is one entry in the table's commit ring,
`[n][base][kind][check]`, written body then check into the line after the
last; the valid entry with the greatest `n` is the state.  `OPEN(base)`
hides every record above `base`; `CLOSED` shows them.  An `OPEN` entry
found at mount belongs to a session that ended: readers see nothing above
its base, and the next `begin`, `insert` or `abort` deletes those records
and writes `CLOSED` before it does its own work.  `commit` refuses it.

`ring` retention overwrites the record in the next slot; `keep` refuses
the insert while that slot is live (`TIKU_DB_ERR_FULL`).

## On a board

`tiku_db_board()` mounts the database at its first call and again when the
extent moves.  On the nRF54L, Apollo and RA8P1 parts the extent is the
region's top that the layout service carves with `nvm.db`
(`kernel/memory/AGENTS.md`), written through `tiku_tier_nvm_write()`; the
layout service formats it whenever it rewrites the store and counts its
tables as loss.  On MSP430 it is a `TIKU_DURABLE` array of
`TIKU_DB_ARRAY_BYTES` (16 KB), formatted at the first call when every byte
is 0x00 or every byte 0xFF.  The medium is the port's descriptor from
`kernel/memory/tiku_nvm_media.h`; a port whose descriptor has an erase unit
does not build the engine.

```
db status                               the medium, the extent, the tables
db create temps t:t64 temp:q3 src:i32 cap=4096 ring rate=1/s life=10y
db insert temps now 21.5 2              t64 takes "now": microseconds since boot
db last temps 10 | db scan temps 10 | db where temps temp > 30 [last 5]
db range temps t <lo> <hi> | db agg temps temp [src = 2]
db delete temps <seq> | db delete temps where src = 2
db begin|commit|abort|schema|stat|drop temps
db bench [n] [pad]                      [BM] lines named by record size
```

Rows print one a line, tab-separated, sequence number first; q3 prints with
three decimals.

## Proof

Host: `make -C TikuBench/tests/host/kernel db` runs `test_db.c` on four
media profiles (32-bit word, 16-bit word, a 16-byte line that cannot tear,
a 16-byte line that can, the last with the torn line old, new, mixed and
garbage): the API contract; every argument, type and name at its bounds;
each field of the header, a table entry and the schema corrupted with its
check recomputed, refused at mount; a random run against a reference model
with remounts; and every operation (create over a zero and a used block, the
first insert, an insert that drops a record, into a deleted slot, into a
keep table, delete, delete through a cursor, begin, commit, abort, an
insert that closes an abandoned transaction, drop, format over two
tables) cut at every byte of every write, remounted, and required to be
the state before, the state after, or for an insert the slot empty; then
an insert after the cut must take a number above every one committed and
add exactly itself.  The test is built with UBSan, so an index past an
array stops it.  `mutations/kernel.py` lists the source mutations it must
catch (`python3 mutate.py --port kernel --model db`); `mutate.py --generate
kernel/db/tiku_db.c --binary test_db` lists the generated ones it does not.
`make db-shell` runs the `db` command over the engine on a host array;
`make memory-layout` runs the layout service with the `nvm.db` knob
(`test_layout_db`, every write of an extent change cut) and the shell,
`/data` and the extent together (`test_layout_integration_db`).

Board: `python -m tikubench run db --board nrf54l15 --port <console>` makes
tables through the shell, reboots across an open transaction, and records
`db bench` as `[BM]` metrics.  Where the board has no extent and `/data`
holds no files it stages `nvm.db=64K` first.

No VFS node, BASIC word or MCP resource calls the engine.
