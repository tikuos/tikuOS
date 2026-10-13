# TikuOS
TikuOS is an operating system for ubiquitous computers, built first for microwatt computers that run for years on a coin cell or indefinitely on harvested energy. We designed it from scratch to interact with the physical world, work alongside AI agents that both use and program it, and sustain long-term operation. The software and networking stack supports sub-milliwatt wireless communication, IP networking, and on-device intelligence, and even runs on feeble energy harvested from the environment for devices with tiny or no batteries.

## Supported boards

### Microwatt computers

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

### Others

| Family | Board · core | RAM | NVM |
|--------|--------------|-----|-----|
| **Raspberry Pi**<br><sub>32-bit · RP2350 · Cortex-M33 @ 150 MHz</sub> | Pico 2 / Pico 2 W | 520 KB | 4 MB flash |
| **Renesas RA8**<br><sub>32-bit · Cortex-M85</sub> | EK-RA8P1 · M85 240/480/1000 MHz + Ethos-U55 | 1664 KB | 1 MB MRAM |
| **STMicro STM32N6**<br><sub>32-bit · Cortex-M55</sub> | Nucleo-144 N657X0-Q · up to 600 MHz | 3840 KB | 64 MB NOR |
| **Espressif ESP32-C61**<br><sub>32-bit · RISC-V @ 160 MHz</sub> | ESP32-C61-DevKitC · Wi-Fi 6 + BLE 5 | 320 KB + 2 MB PSRAM | 8 MB flash |

## Getting started

```bash
git clone --recurse-submodules https://github.com/tikuos/tikuOS.git
cd tikuOS
```

Then start a coding agent (Claude Code, Codex or similar) at the root of
the tree and ask for what you want:

```bash
claude      # or: codex
> Write a process that blinks LED 0 every second on the nRF54L15 and flash it
> Add a shell command that prints the battery voltage and test it on the FR5994
```

The agent reads `AGENTS.md`, builds, flashes and tests, and shows you the
commit before making it.

## License

Apache License 2.0; see [LICENSE](LICENSE).
[tiku-os.org](http://tiku-os.org) · ambuj@tiku-os.org
