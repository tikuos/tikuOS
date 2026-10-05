# TikuOS Drivers Architecture

This document describes how hardware-driver code is organised in
TikuOS. It complements the existing tikukits/ model — kits are
pure-software libraries, drivers touch silicon.

## Goals

- Keep core kernel small and Apache-2.0 clean. Drivers (which may
  carry vendor firmware blobs or non-permissive licences) live in a
  separate, optional sibling repo `drivers/`.
- Each driver self-describes via a single descriptor struct so the
  kernel can iterate registered drivers without knowing their
  specifics.
- Each driver opts in via a single make flag; the kernel builds
  cleanly with the whole `drivers/` repo absent.
- VFS contributions (e.g. `/dev/wifi/wifi0/rssi`) are declared in
  the descriptor so a driver gains observability for free.

## Repo layout

```
tikuos/                            (this repo, Apache-2.0)
├── kernel/drivers/                (driver registry — core, always built)
│   ├── tiku_drv.h                 (descriptor + class enum)
│   ├── tiku_drv_registry.h        (public API: init_all / iterate)
│   ├── tiku_drv_registry.c        (dispatch implementation)
│   └── tiku_drv_empty_table.c     (zero-driver fallback)
└── drivers/                   (separate repo, gitignored here)
    ├── README.md
    ├── tiku_drv_table.c           (the per-build descriptor list)
    ├── skeleton/                  (copy-paste template for new drivers)
    ├── wifi/    cyw43/ ...
    ├── sensors/ temperature/mcp9808/ ...
    ├── radio/   lora_sx126x/ ...
    ├── display/ epaper/pervasive_itc/ ...
    └── storage/ sdcard/ ...
```

`drivers/` is listed in this repo's `.gitignore` so the user
can clone or `git init` it in place without polluting tikuOS's
history. The kernel detects its presence via Makefile probe
(`HAS_DRIVERS=1`).

## Descriptor

Every driver provides one `const tiku_drv_t`:

```c
typedef enum {
    TIKU_DRV_CLASS_SENSOR,
    TIKU_DRV_CLASS_RADIO,
    TIKU_DRV_CLASS_WIFI,
    TIKU_DRV_CLASS_BLE,
    TIKU_DRV_CLASS_DISPLAY,
    TIKU_DRV_CLASS_STORAGE,
    TIKU_DRV_CLASS_INPUT,
    TIKU_DRV_CLASS_OTHER,
} tiku_drv_class_t;

typedef struct tiku_drv {
    const char            *name;        /* "wifi-cyw43" */
    tiku_drv_class_t       class;
    int                  (*init)(void);
    int                  (*deinit)(void);
    const tiku_vfs_node_t *vfs_nodes;
    uint8_t                vfs_node_count;
    const char            *vfs_mount;   /* "wifi0" or NULL */
} tiku_drv_t;
```

The descriptor's symbol name convention is
`tiku_drv_<class>_<name>` (e.g. `tiku_drv_wifi_cyw43`,
`tiku_drv_sensor_mcp9808`). Class prefix prevents name collisions
across categories.

## Registration mechanism (Option A: hand-edited table)

A single file in the driver repo, `drivers/tiku_drv_table.c`,
lists every available driver behind a per-driver `#if`:

```c
#include "kernel/drivers/tiku_drv.h"

#if TIKU_DRV_WIFI_CYW43_ENABLE
extern const tiku_drv_t tiku_drv_wifi_cyw43;
#endif
#if TIKU_DRV_SENSOR_MCP9808_ENABLE
extern const tiku_drv_t tiku_drv_sensor_mcp9808;
#endif

const tiku_drv_t *const tiku_drv_table[] = {
#if TIKU_DRV_WIFI_CYW43_ENABLE
    &tiku_drv_wifi_cyw43,
#endif
#if TIKU_DRV_SENSOR_MCP9808_ENABLE
    &tiku_drv_sensor_mcp9808,
#endif
};
const uint8_t tiku_drv_table_count =
    sizeof(tiku_drv_table) / sizeof(tiku_drv_table[0]);
```

Enabling a driver therefore means exactly two things:
`-DTIKU_DRV_<X>_ENABLE=1` on the make command line (via that
driver's `build.mk`), and a guarded entry in `tiku_drv_table.c`.

When `drivers/` is absent, the kernel links against
`kernel/drivers/tiku_drv_empty_table.c` which provides a
zero-length table with weak-equivalent semantics — the kernel
boots, no drivers init, no link errors.

## Build integration

Top-level Makefile probes for the directory, exactly like
`tikukits`:

```make
HAS_DRIVERS ?= $(if $(wildcard $(PROJ_DIR)/drivers),1,0)

SRCS += kernel/drivers/tiku_drv_registry.c
ifeq ($(HAS_DRIVERS),1)
CFLAGS += -DHAS_DRIVERS=1
SRCS   += drivers/tiku_drv_table.c
include $(wildcard drivers/*/*/build.mk)
include $(wildcard drivers/*/*/*/build.mk)
else
SRCS   += kernel/drivers/tiku_drv_empty_table.c
endif
```

Each driver supplies its own `build.mk` that adds its sources and
defines its enable flag when the user passes it on the command
line:

```make
# drivers/wifi/cyw43/build.mk
ifeq ($(TIKU_DRV_WIFI_CYW43_ENABLE),1)
SRCS   += $(wildcard drivers/wifi/cyw43/*.c)
CFLAGS += -DTIKU_DRV_WIFI_CYW43_ENABLE=1
endif
```

So a build with the CYW43 WiFi driver is just:

```
make MCU=rp2350 HAS_TESTS=0 TIKU_DRV_WIFI_CYW43_ENABLE=1
```

## Init dispatch

`tiku_drv_init_all()` walks the table and calls each driver's
`init()` once, in declaration order. It's invoked from `main.c`
after `tiku_vfs_tree_init()` and before tests / examples / the
shell scheduler enters its loop.

```c
void tiku_drv_init_all(void) {
    for (uint8_t i = 0; i < tiku_drv_table_count; ++i) {
        const tiku_drv_t *d = tiku_drv_table[i];
        if (d != NULL && d->init != NULL) {
            (void)d->init();
        }
    }
}
```

Drivers report errors via their own return code; the registry
logs failures but does not abort the kernel boot. Individual
driver init() failures are recoverable.

## VFS connection

After `init()` succeeds, the registry publishes the descriptor's `vfs_nodes`
under `/dev/<class>/<vfs_mount>/`. Zero nodes means the driver contributes no
device endpoints. Failed drivers are not mounted. The arrays and strings must
remain valid for the whole boot; use static storage, normally `const`.

The boot-only `tiku_vfs_mount(parent_path, node)` API adds children without
copying or changing the existing constant tree. Lookup, listing, manifests,
reverse lookup and node statistics all include these children. It rejects
duplicate names, already attached nodes, malformed trees and full tables.
There is no replacement or unmount operation. `tiku_vfs_init()` clears mounts;
call it once before initializing drivers. Code that registers a root again,
as a test may, calls `tiku_drv_remount_all()` afterwards to put the
registry's mounts back.

The registry reserves `TIKU_DRV_REGISTRY_MAX` status/mount records (default 8).
Descriptors beyond this limit are reported as `capacity` and are not started.
`TIKU_VFS_MOUNT_MAX` defaults to 17, enough for the driver report, eight classes
and eight device instances. Increase both limits for a larger driver set.

Read `/sys/drivers/count`, `/sys/drivers/limit` and `/sys/drivers/entries` to
inspect initialization. Each entries row contains six tab-separated fields:
table index, name, class, state, init result, and mount result. `ready` means
initialization succeeded; it is not a continuous hardware-health check. A
nonzero mount result can accompany `ready`. Drivers should expose their own
live status when needed. These reports also exist with an empty driver table.

### Safe inspection

Add a descriptor to values that tools may refresh automatically. Manifest
revision 4 keeps the existing six columns and appends `;read=...` to typed
metadata. The policies are `poll`, `sample`, `consume`, `effect` and `none`.
Untyped nodes remain unknown. Secret descriptors also carry `;secret`.

Use `TIKU_VFS_DF_READ_CONSUMES` for reads that drain a queue or stream, and
`TIKU_VFS_DF_READ_EFFECT` for reads that change state or start an operation.
Live sampling and peripheral/bus costs imply `sample`. None of these reads
is eligible for `tiku_vfs_read_passive()`. The explicit `tiku_vfs_read()` API
retains its behavior. A manifest read never invokes value handlers.

The desktop's tools follow this policy when they load values on their own.
For firmware without read metadata they read only nodes known to be
passive, and never the console, the I2C scan, the event ring or the
lifetime counter. Opening a value explicitly still reads it. They do not
list `/data` on their own: it holds user files, and listing it can mount the
store.

### Files without a shell

`/data` is available even with `TIKU_SHELL_ENABLE=0`. Only the optional BASIC
child depends on the shell. `/sys/fs/data` reports state, backing type, file
count, payload bytes, allocated payload capacity, free payload capacity and
total payload capacity. Its usage queries do not mount or format a store:
before mounting, `state` is `not-mounted` and usage returns `ENOTSUP`.
`allocated_bytes + free_bytes = capacity_bytes`; `used_bytes` counts file
contents, so it can be smaller than allocated capacity. Store metadata is not
included in these payload-capacity values; the NVM map reports the full extent.

Run `make -C TikuBench/tests/host/kernel vfs-surface` for the kernel
regressions. The TikuBench **VFS discovery and safe inspection (host)** suite
adds desktop checks and is selectable in both frontends. This covers discovery
and read safety, guarded network settings, DNS configuration, sensor settings,
GPIO ownership, WiFi profiles, and power policies.

Startup-entry enable writes require both SYS and FS capabilities and accept
only `0` or `1`, with optional trailing whitespace. GPIO writes accept `0`,
`1`, `t`, `i` or `in`; malformed tokens are refused and a HAL refusal returns
`ENOTSUP`. Owned GPIO pins return `EBUSY`.

### Network status and settings

`/sys/net/enabled` reports whether the IPv4 kit was compiled in, not whether a
radio is connected. When it is `0`, the other network directories are absent,
apart from `wifi` in a build with a Wi-Fi driver. Every readable network
endpoint is passive: reading it does not start a service, send packets, poll a
protocol, or consume a response.

| Path below `/sys/net` | Access | Meaning |
| --- | --- | --- |
| `ipv4/address` | Read/write | Current local address. Writes are allowed only before a link is registered and while neither DHCP nor DNS owns an active operation. |
| `ipv4/link` | Read | Registered transport name, or `none`. Registration does not establish physical connectivity. |
| `ipv4/mtu`, `ipv4/ttl` | Read | Build-time packet-buffer capacity and default outgoing TTL. |
| `dns/state` | Read | `idle`, `sent`, `done`, or `error`. Reading does not advance the resolver. |
| `dns/server` | Read | Current query server, or `unset` before a query server is selected. |
| `dns/default_server` | Read | Resolver chosen for the next client that requests the default: explicit override, then DHCP-provided DNS, then `8.8.8.8`. |
| `dns/override` | Read/write | IPv4 address to prefer for default resolution, or `auto` to restore automatic selection. |
| `dns/cache_flush` | Write | Write `flush` to discard cached answers. |
| `dhcp/state` | Read | `idle`, `discover-sent`, `requesting`, `bound`, or `error`. |
| `dhcp/address`, `dhcp/netmask`, `dhcp/gateway` | Read | Values received in the bound DHCP lease; not routing-table controls. |
| `dhcp/server`, `dhcp/dns` | Read | DHCP server and first advertised DNS server. |
| `dhcp/lease_seconds` | Read | Lease duration advertised by the server, not the time remaining. The client does not renew or expire leases. |

DNS nodes require the resolver: the full network kit, or
`TIKU_KITS_NET_DNS_ENABLE=1` with `TIKU_KIT_NET_MIN=1`. DHCP nodes require
`TIKU_KITS_NET_DHCP_ENABLE=1`. Lease-value reads return `ENOTSUP` unless the
client is bound. This interface does not start or release DHCP leases.

All three write endpoints require `CAP_NET`. Address strings must contain four
decimal octets; trailing whitespace is accepted, but extra tokens, embedded
NUL bytes, and unspecified, loopback, multicast, or reserved addresses are
rejected. There is no subnet-aware broadcast validation because the stack has
no general static subnet configuration. DNS writes return `EBUSY` while a
query is pending. Source-address writes also return `EBUSY` while a link is
registered or DHCP is acquiring/holding a lease. Turning SLIP off does not
unregister its link, so configure a static address before first enabling it.

For example, on a freshly booted SLIP build with the network kit enabled:

```text
cat /sys/net/ipv4/link
write /sys/net/ipv4/address 172.16.7.20
write /sys/net/dns/override 192.168.1.1
cat /sys/net/dns/default_server
write /sys/net/dns/cache_flush flush
write /sys/net/dns/override auto
```

Use an address and resolver appropriate for your network. These writes affect
RAM only: they are not enrolled in the name/clock journal and do not survive
reboot. The DNS override does survive per-query `dns_init()`, which shell DNS,
NTP, and BASIC HTTPS clients use. An explicit per-query resolver still wins.
Changing a resolver discards cached answers from the old server. Enabling SLIP
preserves an address configured before transport registration. Starting DHCP
can subsequently replace that address as part of its normal operation.

Run `make -C TikuBench/tests/host/kernel vfs-network` for disabled, IPv4-only,
DNS-only, DHCP-only, combined-minimal, and full-network adapter profiles.
The TikuBench **VFS discovery and safe inspection (host)** suite exposes
separate **Network** and **DNS** groups in both frontends. Tests use the real
VFS and DNS resolver, simulated transport/DHCP state, permission failures,
malformed writes, busy operations, short read buffers, and SLIP address
preservation. These are not live-network or radio-hardware qualification.

### Sensor settings

`/dev/sensors/enabled` reports whether the temperature-sensor kit was built.
With `TIKU_KIT_SENSORS_ENABLE=1`, the tree includes the MCP9808 and ADT7410
adapters, and DS18B20 where the board declares 1-Wire support. These directories
name compiled drivers, not detected hardware. Each driver handles one sensor;
the DS18B20 driver assumes a single, externally powered device on its bus. A
board with a stub bus driver refuses initialization, and a 1-Wire pin another
owner holds makes `ds18b20/init` return `EBUSY`. The tree is `/dev/sensors`;
`/dev/sensor` is the driver registry's directory for the sensor class.

Listing the tree or reading `state` and `address` does not probe a bus.
Initialization and configuration require HW capability. Configuration setters
read the register back and report an error if the device did not accept the
change. A bus failure after a write can leave the hardware changed; read the
setting again after communication recovers instead of assuming rollback.

| Path below `/dev/sensors` | Meaning |
| --- | --- |
| `mcp9808/address` | Read the initialized address, or write decimal 24–31 to initialize that sensor. |
| `adt7410/address` | Read the initialized address, or write decimal 72–75 to initialize that sensor. |
| `mcp9808/resolution_bits` | Read/write 9, 10, 11, or 12. |
| `adt7410/resolution_bits` | Read/write 13 or 16. |
| `mcp9808/shutdown`, `adt7410/shutdown` | Read/write 0 for continuous conversion or 1 for shutdown. Other configuration bits are preserved. |
| `ds18b20/init` | Write `init` to initialize the bus and check presence. |
| `ds18b20/convert` | Write `start` to request a conversion without blocking the scheduler for its duration. |
| `ds18b20/resolution_bits` | Read/write 9–12; changes only scratchpad RAM, not EEPROM. |
| `<model>/temperature_mc` | Explicitly read the last conversion in millidegrees Celsius. May be stale in shutdown. |
| `<model>/state` | Cached initialization state; DS18B20 also reports conversion progress. |

Writing an I2C address opens an unconfigured bus at 100 kHz. It preserves an
already configured bus's speed. A failed identification makes that driver's
cached address zero; `initialized` means the last initialization succeeded,
not that the sensor is continuously monitored for disconnection.

For example, with an MCP9808 wired at address 0x18:

```text
write /dev/sensors/mcp9808/address 24
write /dev/sensors/mcp9808/resolution_bits 12
write /dev/sensors/mcp9808/shutdown 0
cat /dev/sensors/mcp9808/temperature_mc
```

Sensor register reads have `read=sample` metadata. Temperature reads have
`read=effect`, because reading temperature can acknowledge sensor status.
Neither kind is included in automatic passive inspection. The ADT7410 VFS
reading preserves 16-bit-mode precision and rounds to the nearest millidegree;
its `temp_t` C API returns sixteenths of a degree.

For DS18B20, wait for `state` to become `ready` before reading temperature.
The VFS waits at least 750 ms, including across a clock-counter wrap, and
refuses a second conversion, reinitialization, or scratchpad access while
its conversion is pending. Changing resolution requires another conversion
before VFS will return temperature. Applications using the low-level driver
directly must coordinate their bus use with VFS; this is not a multi-client
sensor scheduler. Settings are not restored by TikuOS after a sensor reset.

The register contracts follow the manufacturer's
[MCP9808 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/25095A.pdf),
[ADT7410 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ADT7410.pdf),
and [DS18B20 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS18B20.pdf).
Alarm thresholds, interrupt routing and calibration are not exposed as
unvalidated raw register writes.

### GPIO coverage and ownership

`/dev/gpio/<port>/<pin>` reads the level and accepts `0`, `1`, `t` (toggle), or
`i` or `in` (input). `/dev/gpio_dir/<port>` reports one character per pin: `I`,
`O`, or `?` for an owned or unreadable pin. `/dev/gpio_owner/<port>` holds one
`<pin> <owner>` line per pin, where the owner is `free`, `console`,
`peripheral`, `wifi`, `unreadable` (the mux could not be read), or a
registered owner name. One listing per port, rather than a file per pin, keeps
the node tables small on parts with hundreds of pins.

| Platform | Raw GPIO numbering |
| --- | --- |
| MSP430 | Device-declared P1–P9, with only available ports included; J is numeric port 255. Eight register bits per port. |
| nRF54L15 | Virtual ports 1–3 mean physical P0–P2; 32 bits per port. |
| nRF54LM20A/B | As above, plus virtual port 4 for P3. |
| Apollo4 Lite/Plus | Virtual ports 1–16, eight pads each. Pad = `(port - 1) * 8 + pin`. |
| Apollo510/510B | Virtual ports 1–28, eight pads each, using the same mapping. |
| RP2350 | Virtual ports 1–4 map GP0–GP29. Port 4 ends at pin 5; QSPI pins are not exposed. |
| ESP32-C61 | Virtual ports 1–4 map GPIO0–GPIO29 in banks of eight, as on the RP2350. |
| STM32N6 | Numeric 0–7 for A–H and 13–16 for N–Q, 16 register bits per port. I–M do not exist. |
| RA8P1 | Numeric 0–13 for ports 0–9 and A–D, 16 register bits per port. |

These are register-addressable bits, not a promise that every bit has a
bonded, accessible, or electrically safe board pin. Check the board schematic
before driving a pin. The STM32 port gaps are also checked against
[ST's device header](https://github.com/STMicroelectronics/cmsis-device-n6/blob/main/Include/stm32n657xx.h).

Both the `gpio` shell command and VFS refuse to modify an owned pin.
Architecture checks protect detectable alternate peripheral assignments. Board
console pins are protected; on MSP430 the active mux identifies them as
`peripheral`. An RP2350 build with only the USB console leaves GP0 and GP1
unreserved. A WiFi-enabled Pico 2 W also reserves its internal radio wiring. On
RA8P1 a pin set as an analog or IRQ input (ASEL, ISEL) reads as `peripheral`.
The 1-Wire interface claims its GPIO on supported MSP430/RP2350 boards and
releases it on close or failed initialization.

Other GPIO-based drivers can reserve pins with
`tiku_gpio_claim(port, pin, "driver-name")` and release them with the same
owner name. Claims are bounded to 16 entries by default and require static-
lifetime names of at most 23 printable, non-whitespace bytes. Repeated claims
by the same owner are idempotent; another owner, an exhausted table, or a
reserved/peripheral pin is refused. Call these APIs from scheduler context.

Ownership is cooperative, not a security boundary: trusted code, BASIC's
`PINMODE` and `DIGWRITE` included, can still call the raw HAL. On Nordic
parts a pin handed to another core or the GRTC through `PIN_CNF.CTRLSEL`
reads as `peripheral`, but a pin routed to a TWIM, SPIM or UARTE through
that peripheral's `PSEL` does not, so bus drivers there must claim their
pins. GPIO-controlled chip-select or control pins cannot be inferred from
hardware at all.

### WiFi profiles

`/sys/net/wifi` appears when a Wi-Fi driver (CYW43 or ESP32-C61) is enabled.
It provides cached `state`, joined `ssid`, `mac`, `rssi_dbm`, and
`scan_count`.
`scan_results/0` through `scan_results/15` expose cached AP records;
unused entries return `ENOENT`. Discovery and reads never start a scan,
join a network, or write NVM. SSIDs from beacons are escaped to keep them on
one printable line.

The two `profiles/0` and `profiles/1` directories are editable staging
profiles held in RAM. Each contains:

- `ssid`: 1–32 bytes.
- `auth`: `wpa2` or `wpa3`.
- `password`: write-only, with secret metadata; WPA2 accepts 8–63 printable
  bytes and WPA3 1–63. Spaces are preserved. A radio without WPA3, as the
  ESP32-C61 driver is, refuses a WPA3 profile at `connect`.
- `ready`: whether the profile has an SSID and password.
- `connect`: write `connect` to submit it to the radio.
- `clear`: write `clear` to clear that RAM slot, including its password.

Changing an SSID or authentication method clears that slot's password.
Malformed replacements leave the old value unchanged. Staging requires NET
capability; connecting additionally requires FS because the driver saves a
successful connection as its last-working profile. Queue acceptance does not
mean association succeeded: watch `state` for `connecting`, `joined`,
or `failed`. Submitting a profile while joined switches to that network.

```text
write /sys/net/wifi/profiles/0/ssid LabNetwork
write /sys/net/wifi/profiles/0/auth wpa2
write /sys/net/wifi/profiles/0/password replace-with-your-password
write /sys/net/wifi/profiles/0/connect connect
cat /sys/net/wifi/state
```

Use a trusted local connection for credentials; write-only VFS metadata does
not encrypt transport, terminal history, or the existing NVM record.

Write `start` to `scan` for an explicit scan, or `disconnect` to
`disconnect` to request teardown. Both require NET. Pending operations and
full queues return `EBUSY` without replacing an existing connection target.
The driver also prevents an automatic reconnect from racing an accepted scan.

`saved` reports the last-working NVM record's SSID, authentication,
validity and `last_store_result`; a radio that keeps no record, as the
ESP32-C61 driver does not, returns `ENOTSUP`, and `profile_lifetime` reads
`saved=none` there. It never returns the password. Zero means
no storage error has been reported in this boot; a negative result means the
last save/forget failed and must not be treated as durable success. Write
`forget` to `saved` (NET + FS) to clear that record and request
disconnection. Busy joins/scans and queue refusal are reported before the
record is changed. A flush failure is reported even if the RAM mirror has
already been cleared.

Forgetting clears the record and the runner's key bytes, not just the
record's validity marker. It is a logical deletion, not a secure erase of
older flash copies. It does not clear the RAM staging slots; use their
`clear` controls for that. After a reboot the staging slots are empty and the
radio rejoins its last working profile. Only that one profile persists, and
it is not stored encrypted.

### Power policies

`/sys/power/policy` sets the mode the scheduler enters when idle: `off`,
`light`, `deep` or `deepest`, as listed by `/sys/power/available`. `mode`
reads the platform's name for the current mode and `wake` reports which wake
sources are armed. A scheduler hook installed outside the policy reads as
`custom`. Writing `policy` needs the SYS capability; the shell's `sleep`
command writes the same node, so the same check applies to it.

```text
cat /sys/power/available
write /sys/power/policy light
cat /sys/power/mode
write /sys/power/policy off
```

A mode that stops console input needs the acknowledgement
`allow-console-loss`: `sleep deep allow-console-loss` in the shell, or
`write /sys/power/policy "deep allow-console-loss"`. On MSP430 that is LPM3
and LPM4, so a plain `sleep lpm3` is refused, and an init entry holding it
leaves the board in LPM0. The ESP32-C61's light sleep keeps console input
and needs no acknowledgement.

A port offers a deeper mode only when its HAL entry differs from the
shallower one: MSP430 offers all four, the ESP32-C61 offers off, light and
deep, and ports where every mode is WFI offer off and light. A mode is taken
only while a wake source it honours (`tiku_cpu_idle_mode_wakes()`) is armed.
The source is checked again before each sleep; when it has been disarmed,
the core sleeps in light mode instead.

The setting lasts one boot and is not written to NVM. It does not change
voltage rails, memory retention or peripheral power gating.

### Testing the device controls

```sh
make -C TikuBench/tests/host/kernel vfs-devices
make -C TikuBench/tests/host/nonkernel sensors
```

The TikuBench **VFS discovery and safe inspection (host)** suite has separate
GPIO, Sensors, WiFi and Power groups in both frontends. Tests compile the
production adapters and sensor drivers, and the CYW43 admission/persistence
code; buses, radio observations, event queues, NVM flushes and CPU hooks are
controlled doubles. Coverage includes eleven GPIO profiles over ten
geometries, absent STM32 ports, ownership exhaustion, readback mismatch,
DS18B20 timing/CRC, disabled sensor builds, secrets, overlong credentials,
queue refusal, flush failure, capability checks, the idle modes of three
modelled ports, and short buffers.

These are software regressions, not electrical or power-loss qualification
of connected hardware. No test in these groups flashes a board or changes
its settings.

## Adding a new driver — checklist

1. `cp -r drivers/skeleton drivers/<class>/<name>` and
   rename the files.
2. Rename `tiku_drv_skeleton` → `tiku_drv_<class>_<name>` in the
   header, source, and `build.mk`.
3. Implement `init()` and any VFS read/write handlers.
4. Edit `drivers/tiku_drv_table.c`: add the `extern` and the
   guarded `&tiku_drv_<class>_<name>` entry.
5. Build with `make TIKU_DRV_<CLASS>_<NAME>_ENABLE=1 …`.

## Why this split, not just "more tikukits"

`tikukits/` exists for portable-C libraries that run anywhere
(maths, data structures, codecs, ML, crypto). They have no
hardware dependency and ship under Apache-2.0.

`drivers/` exists for code that:

- Talks to a specific silicon block via the arch HAL.
- May carry vendor firmware blobs (CYW43, BLE softdevice, e-paper
  panel programs) under non-Apache licences.
- Has a different release cadence — driver fixes for one chip
  shouldn't force a tikukits version bump.
- Benefits from class-based organisation (wifi/, sensors/, …)
  that doesn't make sense for pure libraries.

Existing tikukits/sensors/ and tikukits/epaper/ are
driver-shaped and will migrate to drivers/ over time.

## Vendor firmware blobs (WiFi/Bluetooth on RP2350)

The CYW43439 radio on the Pico W / Pico 2 W needs Infineon's chip
firmware uploaded at every boot. Those binaries are **not tracked in
any tikuOS repository** — they are binary-only vendor software under
the Infineon **Permissive Binary License 1.0** (redistribution of the
unmodified binaries is permitted, but the licence is not OSI
open-source, so the repos ship only Apache-2.0 content plus the licence
text).

To build with `TIKU_DRV_WIFI_CYW43_ENABLE=1`, download the blobs once
into `drivers/wifi/cyw43/firmware/` — that directory's `README.md`
carries the official download commands (embassy-rs mirror of Infineon's
binaries, the same files pico-sdk/MicroPython/Zephyr ship) and SHA-256
sums to verify them. The build stops with a pointer to that README if
the blobs are missing.
