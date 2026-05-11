# TikuOS Drivers Architecture

This document describes how hardware-driver code is organised in
TikuOS. It complements the existing tikukits/ model — kits are
pure-software libraries, drivers touch silicon.

## Goals

- Keep core kernel small and Apache-2.0 clean. Drivers (which may
  carry vendor firmware blobs or non-permissive licences) live in a
  separate, optional sibling repo `tikudrivers/`.
- Each driver self-describes via a single descriptor struct so the
  kernel can iterate registered drivers without knowing their
  specifics.
- Each driver opts in via a single make flag; the kernel builds
  cleanly with the whole `tikudrivers/` repo absent.
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
└── tikudrivers/                   (separate repo, gitignored here)
    ├── README.md
    ├── tiku_drv_table.c           (the per-build descriptor list)
    ├── skeleton/                  (copy-paste template for new drivers)
    ├── wifi/    cyw43/ ...
    ├── sensors/ temperature/mcp9808/ ...
    ├── radio/   lora_sx126x/ ...
    ├── display/ epaper/pervasive_itc/ ...
    └── storage/ sdcard/ ...
```

`tikudrivers/` is listed in this repo's `.gitignore` so the user
can clone or `git init` it in place without polluting tikuOS's
history. The kernel detects its presence via Makefile probe
(`HAS_TIKUDRIVERS=1`).

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

A single file in the driver repo, `tikudrivers/tiku_drv_table.c`,
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

When `tikudrivers/` is absent, the kernel links against
`kernel/drivers/tiku_drv_empty_table.c` which provides a
zero-length table with weak-equivalent semantics — the kernel
boots, no drivers init, no link errors.

## Build integration

Top-level Makefile probes for the directory, exactly like
`tikukits`:

```make
HAS_TIKUDRIVERS ?= $(if $(wildcard $(PROJ_DIR)/tikudrivers),1,0)

SRCS += kernel/drivers/tiku_drv_registry.c
ifeq ($(HAS_TIKUDRIVERS),1)
CFLAGS += -DHAS_TIKUDRIVERS=1
SRCS   += tikudrivers/tiku_drv_table.c
include $(wildcard tikudrivers/*/*/build.mk)
include $(wildcard tikudrivers/*/*/*/build.mk)
else
SRCS   += kernel/drivers/tiku_drv_empty_table.c
endif
```

Each driver supplies its own `build.mk` that adds its sources and
defines its enable flag when the user passes it on the command
line:

```make
# tikudrivers/wifi/cyw43/build.mk
ifeq ($(TIKU_DRV_WIFI_CYW43_ENABLE),1)
SRCS   += $(wildcard tikudrivers/wifi/cyw43/*.c)
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

## VFS connection (planned)

The descriptor's `vfs_nodes` array is a contract: the kernel
splices it under `/dev/<class>/<vfs_mount>/` after `init()`
returns success. The exact splice helper (e.g.
`tiku_vfs_drv_mount(class, mount, nodes, count)`) is a small
extension to `kernel/vfs/` that lands once the first driver
actually ships VFS nodes.

For now the descriptor accepts the fields; runtime splicing is a
stub the registry will fill in when the first VFS-exposing
driver ships. Drivers can populate the fields today and the
contract will start enforcing once a node-mount helper lands.

## Adding a new driver — checklist

1. `cp -r tikudrivers/skeleton tikudrivers/<class>/<name>` and
   rename the files.
2. Rename `tiku_drv_skeleton` → `tiku_drv_<class>_<name>` in the
   header, source, and `build.mk`.
3. Implement `init()` and any VFS read/write handlers.
4. Edit `tikudrivers/tiku_drv_table.c`: add the `extern` and the
   guarded `&tiku_drv_<class>_<name>` entry.
5. Build with `make TIKU_DRV_<CLASS>_<NAME>_ENABLE=1 …`.

## Why this split, not just "more tikukits"

`tikukits/` exists for portable-C libraries that run anywhere
(maths, data structures, codecs, ML, crypto). They have no
hardware dependency and ship under Apache-2.0.

`tikudrivers/` exists for code that:

- Talks to a specific silicon block via the arch HAL.
- May carry vendor firmware blobs (CYW43, BLE softdevice, e-paper
  panel programs) under non-Apache licences.
- Has a different release cadence — driver fixes for one chip
  shouldn't force a tikukits version bump.
- Benefits from class-based organisation (wifi/, sensors/, …)
  that doesn't make sense for pure libraries.

Existing tikukits/sensors/ and tikukits/epaper/ are
driver-shaped and will migrate to tikudrivers/ over time.
