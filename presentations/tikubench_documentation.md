# TikuBench: Hardware-in-the-Loop Test Framework for TikuOS

## Overview

TikuBench is a Python-based interactive testing framework that automates
the full cycle of selecting, building, flashing, and verifying firmware
tests on MSP430 hardware. It bridges the gap between compile-time test
flags and on-target execution by managing the entire workflow through a
single command.

```
tikubench/runner.py
    |
    +-- Modifies tests/tiku_test_config.h (enable flags)
    +-- Runs: make clean && make MCU=msp430fr5969
    +-- Runs: make flash MCU=msp430fr5969
    +-- Opens serial port, monitors UART output
    +-- Parses machine-readable test markers
    +-- Displays colored pass/fail summary
```

---

## 1. Quick Start

```bash
# Interactive mode (menu-driven)
cd TikuBench
python3 tikubench/runner.py

# Non-interactive with specific category
python3 tikubench/runner.py --category scheduler \
    --no-interactive --mcu msp430fr5969

# List all available test categories
python3 tikubench/runner.py --list

# Loopback tests (host echoes bytes back)
python3 tikubench/runner.py --category uart-loopback \
    --no-interactive --port /dev/ttyACM1
```

---

## 2. Command-Line Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--mcu` | `msp430fr5969` | Target MCU for `make MCU=` |
| `--port` | auto-detect | Serial port path (`/dev/ttyACM*`) |
| `--baud` | `9600` | UART baud rate |
| `--timeout` | `60` | Seconds to wait for test completion |
| `--category` | interactive | Test category key (skip menu) |
| `--list` | — | Print all categories and exit |
| `--no-interactive` | — | Skip all prompts (CI/automation) |
| `--config-only` | — | Modify config only, skip build/flash |
| `--parse-file` | — | Parse saved UART log instead of live |

---

## 3. Test Categories (61 total)

### 3.1 UART Tests (10 categories)

| Key | Name | Echo? | Description |
|-----|------|:-----:|-------------|
| `uart` | UART Subsystem | No | Init, TX binary, overrun detection |
| `uart-loopback` | Binary Loopback | Yes | 256 bytes, ring buffer, SLIP bytes |
| `uart-stress` | Stress + Capacity | Yes | 2048-byte sustained, duplex |
| `uart-slip-frame` | SLIP Frame | Yes | End-to-end SLIP encode/decode |
| `uart-overrun-provoke` | Overrun Provocation | Yes | Trigger UCOE, verify detection |
| `uart-isr-contention` | ISR Contention | Yes | Loopback under 128 Hz clock ISR |
| `uart-full` | Full Suite | Yes | All 12 UART tests (~2 min) |
| `uart-edge-noecho` | Edge Cases (No Echo) | No | RX-no-data, reinit, ring buffer boundary |
| `uart-edge-echo` | Edge Cases (Echo) | Yes | Mixed traffic, interleaved R/W, ordering |
| `uart-baud-sweep` | Baud Rate Sweep | Yes | Tests at 6 baud rates (4800-115200) |

### 3.2 Core OS Tests (19 categories)

| Key | Name | Description |
|-----|------|-------------|
| `watchdog` | Watchdog Timer | Kick, pause/resume, interval timer |
| `watchdog-timeout` | Watchdog Timeout | **DESTRUCTIVE** - device resets |
| `watchdog-edge` | Watchdog Edge Cases | Resume-no-kick, config persistence, off-reinit |
| `cpuclock` | CPU Clock Output | Expose clock on P3.4 for scope |
| `clock-edge` | Clock Edge Cases | Seconds, delay_usec, wraparound macros |
| `process` | Process / Protothread | Lifecycle, events, yield, broadcast, poll |
| `process-edge` | Process Edge Cases | Double-start, exit-nonrunning, queue overflow |
| `channel` | Channel IPC | Init, put/get, wraparound, free-count |
| `scheduler` | Scheduler | Init, start, run_once, idle hook, autostart |
| `timer` | Timer Subsystem | Event timer, callback, periodic, stop |
| `timer-edge` | Timer Edge Cases | Restart, remaining, multi-expire, zero-interval |
| `htimer-edge` | HW Timer Edge Cases | NULL checks, guard-time, overwrite, run_next |
| `arena` | Arena Allocator | Create, alloc, alignment, peak, secure reset |
| `persist` | Persistent FRAM Store | Register, read/write, delete, wear, reboot |
| `mpu` | MPU Write Protection | Init, lock/unlock, scoped write, violation |
| `pool` | Pool Allocator | Alloc/free, exhaustion, LIFO, poisoning |
| `region` | Region Registry | Init, contains, claim/unclaim, type lookup |
| `memory-edge` | Memory Edge Cases | Init orchestration, unaligned arena, double-free |

### 3.3 TikuKits Library Tests (28 categories)

| Key | Name | Tests |
|-----|------|:-----:|
| `matrix` | Linear Algebra | 13+ |
| `statistics` | Statistics | 9+ |
| `distance` | Distance Metrics | 7+ |
| `sensor` | Sensor Interface | 2 |
| `sigfeatures` | Signal Features | 14+ |
| `textcompression` | Text Compression | 5 |
| `ml-linreg` | ML Linear Regression | 14 |
| `ml-logreg` | ML Logistic Regression | 15 |
| `ml-dtree` | ML Decision Tree | 14 |
| `ml-knn` | ML k-Nearest Neighbors | 15 |
| `ml-nbayes` | ML Naive Bayes | 15 |
| `ml-linsvm` | ML Linear SVM | 15 |
| `ml-tnn` | ML Tiny Neural Network | 17 |
| `ds-array` | DS Array | 10 |
| `ds-ringbuf` | DS Ring Buffer | 10 |
| `ds-stack` | DS Stack | 10 |
| `ds-queue` | DS Queue | 10 |
| `ds-pqueue` | DS Priority Queue | 10 |
| `ds-list` | DS Linked List | 10 |
| `ds-htable` | DS Hash Table | 10 |
| `ds-bitmap` | DS Bitmap | 10 |
| `ds-sortarray` | DS Sorted Array | 10 |
| `ds-btree` | DS B-Tree | 10 |
| `ds-sm` | DS State Machine | 10 |
| `ds-bloom` | DS Bloom Filter | 10 |
| `ds-circlog` | DS Circular Log | 10 |
| `ds-deque` | DS Deque | 10 |
| `ds-trie` | DS Trie | 10 |

### 3.4 Network Tests (4 categories)

| Key | Name | Description |
|-----|------|-------------|
| `net` | Networking (unit) | Byte-order, checksum, ICMP, SLIP, UDP, TFTP |
| `net-integration` | Full Network Stack | 27 tests over IPv4-over-SLIP |
| `udp-integration` | UDP Integration | UDP echo subset |
| `ntp-integration` | NTP Integration | Host NTP server simulation |

---

## 4. Architecture

### 4.1 Directory Structure

```
TikuBench/
  tikubench/
    runner.py                  Main test runner (1773 lines)
    ntp_server.py              NTP server for time sync tests
    slip_ping.py               SLIP ping utility
    common/
      colors.py                ANSI color codes + TTY detection
      serial_port.py           eZ-FET safe serial operations
      packet.py                IPv4/ICMP/UDP/TFTP packet builders
      slip.py                  SLIP codec (RFC 1055 + eZ-FET NUL)
    net/
      runner.py                Network test discovery + runner
      tests/                   27 network test modules
        test_basic_ping.py
        test_udp_echo.py
        test_tftp_transfer.py
        test_ntp.py
        ... (23 more)
```

**File counts:** 47 Python files, 5,629 lines total

### 4.2 Workflow Pipeline

```
  User selects category
         |
         v
  +------------------+
  | Config Phase     |  Modify tiku_test_config.h:
  | (runner.py)      |    TEST_ENABLE = 1
  |                  |    category flags = 1
  |                  |    all other flags = 0
  +--------+---------+
           |
           v
  +------------------+
  | Build Phase      |  make clean
  | (subprocess)     |  make MCU=msp430fr5969
  +--------+---------+
           |
           v
  +------------------+
  | Flash Phase      |  make flash MCU=msp430fr5969
  | (mspdebug)       |  (uses tilib driver for eZ-FET)
  +--------+---------+
           |
           v
  +------------------+
  | Monitor Phase    |  Open serial port at 9600 baud
  | (pyserial)       |  Wait for [TS:BEGIN]
  |                  |  If echo mode: echo every byte
  |                  |  Accumulate lines until [TS:END]
  +--------+---------+
           |
           v
  +------------------+
  | Parse Phase      |  Match [T:P:N] and [T:F:N] markers
  | (regex)          |  Group by [TG:BEGIN]/[TG:END]
  |                  |  Extract pass/fail counts
  +--------+---------+
           |
           v
  +------------------+
  | Display Phase    |  Colored terminal output
  | (ANSI colors)    |  Group summaries with [PASS]/[FAIL]
  |                  |  Total/Pass/Fail/Rate footer
  +------------------+
```

---

## 5. Machine-Readable Test Protocol

TikuBench parses a simple line-based protocol emitted by the firmware
over UART at 9600 baud:

### 5.1 Markers

```
[TS:BEGIN] TikuOS                          # Suite start (resets counters)
[TG:BEGIN] Group Name                      # Test group start
[T:P:1] assertion passed description       # Test #1 PASSED
[T:F:2] assertion failed description       # Test #2 FAILED
[TG:END] Group Name                        # Test group end
[TS:END] TikuOS total=N pass=N fail=N      # Suite end with summary
```

### 5.2 Firmware Macros

```c
TEST_SUITE_BEGIN("TikuOS");        // [TS:BEGIN] TikuOS
TEST_GROUP_BEGIN("My Tests");      // [TG:BEGIN] My Tests
TEST_ASSERT(cond, "description");  // [T:P:N] or [T:F:N]
TEST_GROUP_END("My Tests");        // [TG:END] My Tests
TEST_SUITE_END("TikuOS");         // [TS:END] TikuOS total=...
```

### 5.3 Counters

- `tests_run` increments on every `TEST_ASSERT`
- `tests_passed` / `tests_failed` track outcomes
- Counters are `int` (16-bit on MSP430, max 32767 tests per suite)

---

## 6. Config Modification

TikuBench modifies `tests/tiku_test_config.h` using regex replacement:

```python
pattern = r'^(\s*#define\s+)(TEST_\w+)(\s+)([01])(\s*.*)$'
```

**Rules:**
1. `TEST_ENABLE` is always set to 1
2. All flags in the selected category are set to 1
3. All other `TEST_*` flags (that match `[01]`) are set to 0
4. Auto-derived macros (using `||` expressions) are never touched
5. `#ifndef`-guarded flags (like `TEST_KITS_NET_TFTP_EXT`) are skipped

**Example:** Selecting `scheduler` category sets:
```c
#define TEST_ENABLE 1
#define TEST_SCHED_INIT 1
#define TEST_SCHED_START 1
#define TEST_SCHED_RUN_ONCE 1
// ... all other TEST_* = 0
```

---

## 7. Echo Mode

For UART loopback tests, the host must echo every byte back to the
device. TikuBench handles this automatically:

```python
if category.get("echo"):
    # Read any available byte, echo it back immediately
    while ser.in_waiting:
        byte = ser.read(1)
        ser.write(byte)
        echo_count += 1
```

**Sync Protocol:** The firmware sends `0xA5` (> 0x80) as a sync
marker after text output. Since all `TEST_PRINTF` text is ASCII
(< 0x80), the marker cannot collide with echoed text. The firmware
reads and discards bytes until `0xA5` echoes back, ensuring the
channel is clean before binary test phases.

---

## 8. Baud Rate Sweep

The `uart-baud-sweep` category tests UART at 6 different baud rates
by modifying the MSP430 board header:

```
arch/msp430/boards/tiku_board_fr5969_launchpad.h
```

**Baud rate register values (eUSCI_A, BRCLK=8MHz, UCOS16=1):**

| Baud | BRW | BRF | BRS |
|-----:|:---:|:---:|:---:|
| 4800 | 104 | 2 | 0xD6 |
| 9600 | 52 | 1 | 0x49 |
| 19200 | 26 | 0 | 0xB6 |
| 38400 | 13 | 0 | 0x84 |
| 57600 | 8 | 10 | 0xF7 |
| 115200 | 4 | 5 | 0x55 |

For each baud rate, the runner:
1. Patches the board header with new BRW/MCTLW values
2. Rebuilds and reflashes the firmware
3. Opens the serial port at the matching baud rate
4. Runs the echo-loopback test
5. Records pass/fail results
6. Restores 9600 baud when done

---

## 9. Network Integration Tests

Network categories use a separate test runner (`tikubench/net/runner.py`)
that communicates with the device over IPv4-over-SLIP:

```
  Host (Python)                   Device (MSP430)
       |                                |
       |  <-- SLIP frame (IPv4/ICMP) -- |
       |  -- SLIP frame (reply) ------> |
       |                                |
```

### 9.1 Test Discovery

Tests are discovered via Python's `pkgutil`:

```python
@net_test(1, "Basic Ping", "Send one ICMP echo request")
def run(ser, results):
    # Send ICMP echo, verify reply
    ...
```

**27 network tests** covering:
- ICMP echo request/reply (6 tests)
- SLIP encoding/decoding (4 tests)
- UDP echo and validation (5 tests)
- IPv4 header validation (3 tests)
- TFTP packet builders (3 tests)
- MTU boundary, malformed packets, stability (6 tests)

### 9.2 Packet Building

```python
from tikubench.common.packet import (
    build_icmp_echo_request,
    build_ipv4_packet,
    build_ipv4_udp_packet,
    build_tftp_rrq,
    inet_checksum,
)
```

### 9.3 NTP Server

The `ntp-integration` category starts a lightweight NTP server:

1. Device boots and sends NTP request (UDP port 123)
2. Python NTP server responds with current Unix timestamp
3. Device parses response and sets internal time
4. Test verifies device's reported time matches host time

---

## 10. eZ-FET Workarounds

The TI eZ-FET debug probe (onboard MSP430 LaunchPads) has USB-CDC
quirks that TikuBench works around:

1. **DTR must be False** on open to avoid triggering reset
2. **2-second wait** after open for USB enumeration
3. **Port must reopen** between mspdebug and serial monitor
4. **0x00 byte triggers reset** on some firmware versions
5. **SLIP NUL escape** (`0xDE`) used instead of raw `0x00`
6. **Sync marker** (`0xA5`) used to drain echoed text

---

## 11. Common Utilities

### 11.1 Serial Port (`common/serial_port.py`)

```python
auto_detect_port()     # Find /dev/ttyACM* with TI vendor ID
open_serial(port)      # DTR-safe open with 2s stabilization
reopen_serial(ser)     # Close + wait + reopen (eZ-FET workaround)
read_slip_frame(ser)   # Read one SLIP-encoded frame with timeout
```

### 11.2 SLIP Codec (`common/slip.py`)

```python
slip_encode(payload)   # RFC 1055 + eZ-FET NUL escape (0x00 -> ESC 0xDE)
slip_decode(data)      # Returns list of decoded frames
```

### 11.3 Packet Builders (`common/packet.py`)

```python
inet_checksum(data)                    # RFC 1071 ones-complement
build_icmp_echo_request(seq, ident)    # ICMP type 8
build_ipv4_packet(payload)             # IPv4 header + payload
build_ipv4_udp_packet(sport, dport, payload)
build_tftp_rrq(filename, mode)         # TFTP Read Request
hexdump(data)                          # Formatted hex display
```

### 11.4 Colors (`common/colors.py`)

```python
C.GREEN, C.RED, C.YELLOW, C.CYAN, C.BOLD, C.DIM, C.RESET
supports_color()  # True if stdout is a TTY
```

---

## 12. Adding a New Test Category

### Step 1: Add test flags to `tests/tiku_test_config.h`

```c
/** Enable my new test. */
#define TEST_MY_NEW_TEST 0
```

### Step 2: Write the test in C

```c
#if TEST_MY_NEW_TEST
void test_my_new_test(void)
{
    TEST_GROUP_BEGIN("My New Test");
    TEST_ASSERT(1 + 1 == 2, "basic math works");
    TEST_GROUP_END("My New Test");
}
#endif
```

### Step 3: Add dispatch in `tests/test_runner.c`

```c
#if TEST_MY_NEW_TEST
    test_my_new_test();
    tiku_common_delay_ms(TEST_DELAY_MS);
#endif
```

### Step 4: Add source to `Makefile`

```makefile
SRCS += tests/my_module/test_my_new.c
```

### Step 5: Add category to `TikuBench/tikubench/runner.py`

```python
{
    "key": "my-new",
    "name": "My New Tests",
    "description": "Tests for my new feature",
    "flags": ["TEST_MY_NEW_TEST"],
    "requires_tikukits": False,
},
```

### Step 6: Run

```bash
python3 tikubench/runner.py --category my-new \
    --no-interactive --mcu msp430fr5969
```

---

## 13. Test Statistics

| Metric | Count |
|--------|------:|
| Test categories | 61 |
| C test files | 82 |
| C test lines | 35,350 |
| TEST_ASSERT calls | 3,400 |
| Python files | 47 |
| Python lines | 5,629 |
| Network integration tests | 27 |
| Supported baud rates | 6 |

### Assertion distribution by subsystem

| Subsystem | Assertions |
|-----------|:----------:|
| Data structures | 1,210 |
| ML models | 517 |
| Networking | 361 |
| Maths | 337 |
| Memory | 312 |
| Signal features | 188 |
| Text compression | 144 |
| Process + channel | 118 |
| UART | 73 |
| Timer | 68 |
| Scheduler | 36 |
| Clock | 14 |
| Sensors | 14 |
| Watchdog | 8 |

---

## 14. Bugs Found by TikuBench

During development of the test suite, TikuBench caught three real OS
bugs and one design-level race condition:

### Bug 1: Premature GIE in `tiku_htimer_arch_init()`
- `__enable_interrupt()` called during timer hardware init
- Timer ISRs fired before the scheduler was ready
- **Fix:** `__set_interrupt_state(sr)` to restore caller's state

### Bug 2: Premature GIE in boot sequence
- `tiku_cpu_boot_msp430_init()` enabled GIE at end of boot
- Clock tick ISR flooded event queue during pre-interrupt tests
- **Fix:** Removed `__enable_interrupt()` from boot; GIE enabled
  by `tiku_sched_loop()` at the correct time

### Bug 3: Missing EXITED broadcast in `tiku_process_exit()`
- Timer cleanup handler listened for `TIKU_EVENT_EXITED` but
  `process_exit()` never broadcast it
- Timers of exited processes remained active (stale callbacks)
- **Fix:** Added `tiku_process_post(BROADCAST, EXITED, p)` to
  `process_exit()`

### Design Issue: Timer-Exit Race Condition
- Even with the EXITED fix, the timer can fire before the EXITED
  event is processed (FIFO queue ordering)
- Documented as a design limitation; applications must explicitly
  stop timers before exiting
- See `presentations/timer_exit_race_condition.md` for full analysis
