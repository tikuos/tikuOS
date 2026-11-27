# services/mailbox/

The mailbox is a set of files two processors share on one serial FRAM:
`/mailbox` on each, with the same files on both.  The part is only for
sharing between them; neither keeps its own data there.  On the
TikuComputer-nRF it is the SPI FRAM (U12, FM25V10, 128 KB); the I2C FRAM is
not part of it.

```text
mailbox format
write /mailbox/job.in "read the sensor"
write /mailbox/keep/rate 60
ls /mailbox
read /mailbox/job.out
rm /mailbox/job.in
```

`TIKU_MAILBOX_ENABLE=1` builds it on a board that declares one (BOARD_CAPS
`MAILBOX`).  A part is formatted once, with `mailbox format`; an unformatted
part, or one whose bus is on the other side, answers `ENOTSUP`.

## What stays and what is reclaimed

The writer chooses, per file, when it writes it:

- **A file at the top** (`/mailbox/job.out`) is reclaimable once the other
  processor has read it: read it through to its end.  Reading does not
  remove it; it stays readable until a write needs its room.
- **A file under `keep/`** (`/mailbox/keep/rate`) is never reclaimed, read
  or not: shared configuration and state.
- **A write short of room**, in the table or in the blocks, first takes back
  blocks a cut left marked, then reclaims read files outside `keep/`, the
  oldest first, until the new file fits.  A file the other processor has
  not read is never reclaimed: when only those and kept files are left,
  the write fails with `E2BIG`, which tells the writer its reader is
  behind.
- **Writing a file again** gives it new content, unread.  Either processor
  can delete any file; a reader done with a file it read only in part
  deletes it.

What counts as read: the processor that did not write the file reads up to
its end; a read by the writer, a read that stops short, a listing and an
existence check do not.  The flag is the writer's: the reader cannot move
a file into or out of `keep/`.

## Files

```
tiku_mailbox.c      the layout on the part and the operations; no board code
tiku_mailbox_vfs.c  the board's part, this side of the bus, /mailbox, notices
```

The shell's `mailbox` command (`shell/commands/tiku_shell_cmd_mailbox.c`)
checks and formats the part and runs a write-and-read-back loop:
`mailbox stress <count> [bytes] [name]` leaves its result in
`<name>.out`, so the other processor can read it.

## The layout

| Offset | Holds |
|--------|-------|
| 0 | superblock: magic, version, the layout's offsets, CRC |
| 32 | journal: one replacement under way |
| 64 | the table: 255 entries of 48 bytes (CRC, sequence, first block, length, data CRC, writer, name) |
| after it | the marks: a byte per entry, 1 once the other processor read the file |
| next 16-byte boundary | the bitmap, a bit per 64-byte block |
| next 64-byte boundary | the blocks |

A name is 1 to 23 printable characters without a space, `keep/` counted;
`keep/` is the one folder.  A file is contiguous and as long as the largest
run of free blocks: 118,272 bytes on an empty 128 KB part.  A sequence
number is one more than any in the table, so it orders writes across both
processors.  A slot's mark is cleared before a new entry goes into the
slot.

What holds when an operation is cut at any byte, by a power loss or a bus
moved without the guard below:

- A file's data goes into free blocks before its entry is written, so a
  file is either its old content or its new one.
- A replacement writes the journal before the new entry and clears it after
  the old one is gone; the next operation, on either side, finishes a
  replacement a cut stopped.
- A bitmap bit is set before its block takes data and cleared only once no
  entry names the block, so the bits always cover every file.  A cut can
  leave a block marked that no file holds; a write that finds no room, and
  the check `mailbox` runs, give those back.
- A write that reclaims removes each read file as a delete does, so a cut
  leaves a reclaimed file gone or whole.  It reclaims at most one file per
  table entry: a medium that drops writes ends the write with `EIO`.

`TikuBench/tests/host/kernel/test_mailbox.c` checks the reclaim rules with
two processors on one modelled part, and cuts a write, a replacement, a
delete, a reclaiming write and a format at every byte they write; the
`mailbox` mutations in `mutations/interfaces.py` must each fail it.

## The bus

Each operation holds the shared bus from start to end, through the
medium's `take` and `give`.  On the TikuComputer-nRF only the MSP430 moves
the bus switches, and the nRF54L10 holds the bus by default:

- The MSP430's `tiku_mailbox_start()` turns the sensor rail on and makes the
  nRF the bus's default owner (`tiku_board_ctl_bus_default()`), so the bus
  goes to the nRF whenever it is powered, after a power-on, a reset and a
  borrow.
- The MSP430 borrows it (`tiku_board_ctl_borrow()`): it raises `M2N_IRQ`
  (P3.7), waits until the nRF's `N2M_IRQ` (P3.6) is low, then switches; at
  the end it switches back and drops `M2N_IRQ`.  With the nRF off it takes
  the bus without the handshake.
- The nRF raises `N2M_IRQ` (its P0.03) around each operation and reads
  `M2N_IRQ` (its P0.02) after 2 µs; while the MSP430 asks, it drops its
  line and waits.  Only the nRF backs off, so the two cannot deadlock.
- Each side gives up after 1 s: the MSP430 with `EBUSY` from the operation,
  the nRF likewise.

The sensor port is behind the same switches, so a borrow moves the sensors
with the mailbox.  While the MSP430 boots, the switch pull-downs give it the
bus, and the nRF's operations answer `ENOTSUP`.

Measured on the board (2026-11-27): the nRF writes and reads back a 64-byte
file in about 10 ms alone; with the MSP430 writing at the same time, 2000
copies on the nRF and 300 on the MSP430 all read back whole, about 27 ms a
copy on the nRF.

## Notices

A write or delete through `/mailbox` sends a notice on the peer's `ctl`
service (`TIKU_PEER_CTL_TOPIC_MAILBOX`); the other side posts a VFS event
on `/mailbox`, so `watch` and rules see the change.  The `mailbox stress`
loop writes through the library and sends none.
