# RP2350 packet targets

`TIKU_IPC_ENABLE=1 TIKU_IPC_PERIPHERAL_TARGET_ENABLE=1` compiles the SPI1
and I²C1 target ports and connects their IRQ handlers. Opening a target
leases its fixed pads and an unused peripheral; closing masks access,
clears borrowed staging pointers and restores the pad configuration.

| Port | Pads | Transaction |
|------|------|-------------|
| SPI1 | GP8 MISO, GP9 CS, GP10 SCLK, GP11 MOSI | Mode 0, 8 bits, exactly 320 bytes with CS low |
| I²C1 | GP6 SDA, GP7 SCL | Seven-bit address; 320-byte write, repeated START, 320-byte read, STOP |

`tiku_rp2350_ipc_spi_target_open()` and
`tiku_rp2350_ipc_i2c_target_open()` initialize a caller-owned
`tiku_rp2350_ipc_target_t`. Its `io` field opens a `tiku_ipc_bus` link for
the common wire backend. The context and CPU-copy access domain have
static lifetime. Serialize open/close with the bus owner's changes; the
caller owns the chosen pads. SPI0 and I²C0 retain their controller APIs.

The SPI handler moves at most eight receive and eight transmit bytes per
entry. The I²C handler drains at most sixteen receive bytes and handles
one read request. I²C stretches the clock while servicing a read request.
Completion waits for CS release or STOP and an idle peripheral. These
ports copy FIFO bytes in CPU context and use no DMA.

`make -C TikuBench/tests/host/kernel ipc-rp2350-target` runs the production
register/IRQ models. They exercise whole packets, short transactions,
FIFO reset, abort, interrupt masking and pad restoration. Physical
qualification needs a wired controller, external I²C pull-ups and a
clock/timeout profile calibrated for the configured interrupt load.
