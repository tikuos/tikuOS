# TikuOS
TikuOS is an operating system for ubiquitous computers, built first for microwatt computers that run for years on a coin cell or indefinitely on harvested energy. We designed it from scratch to interact with the physical world, work alongside AI agents that both use and program it, and sustain long-term operation. The software and networking stack supports sub-milliwatt wireless communication, IP networking, and on-device intelligence, and even runs on feeble energy harvested from the environment for devices with tiny or no batteries.

## Supported boards

### Microwatt computers

Boards whose durable memory is non-volatile and written like RAM: FRAM,
MRAM or RRAM.

| Family | Board · core | RAM | NVM |
|--------|--------------|-----|-----|
| **MSP430**<br><sub>16-bit · FRAM</sub> | FR5994 LaunchPad | 8 KB | 256 KB |
| | FR6989 LaunchPad | 2 KB | 128 KB |
| **Ambiq Apollo**<br><sub>32-bit · Cortex-M</sub> | Apollo510 EVB · M55 96/250 MHz | 512 KB | 4 MB MRAM |
| | Apollo4 Lite EVB · M4F 96/192 MHz | 384 KB | 2 MB MRAM |
| | Apollo4 Plus EVB · M4F 96/192 MHz | 384 KB | 2 MB MRAM |
| | Apollo510 Blue EVB · M55 + EM9305 | 512 KB | 4 MB MRAM |
| **Nordic**<br><sub>32-bit · Cortex-M33</sub> | nRF54L15 DK | 256 KB | 1.5 MB RRAM |
| | nRF54LM20-DK · 128 MHz + Axon NPU | 512 KB | 2 MB RRAM |
| **Renesas RA8**<br><sub>32-bit · Cortex-M85</sub> | EK-RA8P1 · M85 240/480/1000 MHz + Ethos-U55 | 1664 KB | 1 MB MRAM |

### Other boards

Boards that keep their durable store in flash or NOR.

| Family | Board · core | RAM | NVM |
|--------|--------------|-----|-----|
| **Raspberry Pi**<br><sub>32-bit · RP2350 · Cortex-M33 @ 150 MHz</sub> | Pico 2 / Pico 2 W | 520 KB | 4 MB flash |
| **STMicro STM32N6**<br><sub>32-bit · Cortex-M55</sub> | Nucleo-144 N657X0-Q · up to 600 MHz | 3840 KB | 64 MB NOR |
| **Espressif ESP32-C61**<br><sub>32-bit · RISC-V @ 160 MHz</sub> | ESP32-C61-DevKitC · Wi-Fi 6 + BLE 5 | 320 KB + 2 MB PSRAM | 8 MB flash |

## Getting the source

```bash
git clone --recurse-submodules https://github.com/tikuos/tikuOS.git
cd tikuOS
```

`drivers/` is a submodule ([tikuos/drivers](https://github.com/tikuos/drivers));
without it the build falls back to an empty driver table.  The companion
checkouts live inside this tree but are versioned separately and are
gitignored here: `TikuBench/` (tests and the board runner), `tikukits/`
(IP stack, Wi-Fi, TLS, MQTT, maths, sensors), `applications/` (the desktop
and board GUI), `tikuConsole/`, and the private `applications/tracker/`.  A
public clone never fails on a repository the cloner cannot read.

## License

Apache License 2.0; see [LICENSE](LICENSE).
[tiku-os.org](http://tiku-os.org) · ambuj@tiku-os.org
