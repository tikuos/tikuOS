/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_target_arch.h - RP2350 SPI1 and I2C1 packet targets.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RP2350_IPC_TARGET_ARCH_H_
#define TIKU_RP2350_IPC_TARGET_ARCH_H_
#include <hal/tiku_ipc_target_hal.h>
#include <services/ipc/tiku_ipc_bus.h>
/** @brief Caller-owned fixed target; pin and peripheral state is restored at close. */
typedef struct {
    tiku_ipc_target_t target;
    tiku_ipc_packet_io_t io;
    uint32_t pads[4],routes[4],reset;
    uint8_t spi,opened,stopped;
} tiku_rp2350_ipc_target_t;
/**
 * @brief Own SPI1 on GP8=MISO, GP9=CS, GP10=SCLK, GP11=MOSI; mode 0, 8 bits.
 * @note Context and CPU-copy domain have static lifetime. Caller owns the pads.
 * The controller clocks exactly 320 bytes with CS low; calibrate IRQ service
 * and bus rate for the selected interrupt load before physical use.
 */
int tiku_rp2350_ipc_spi_target_open(tiku_rp2350_ipc_target_t *,
    const tiku_ipc_access_domain_t *,uint32_t peer,tiku_clock_time_t burst);
/**
 * @brief Own I2C1 on GP6=SDA/GP7=SCL with a seven-bit address and clock stretching.
 * @note One packet is a 320-byte write, repeated START, 320-byte read and STOP.
 * External pull-ups and a controller timeout covering IRQ service are required.
 */
int tiku_rp2350_ipc_i2c_target_open(tiku_rp2350_ipc_target_t *,
    const tiku_ipc_access_domain_t *,uint32_t peer,tiku_clock_time_t burst,uint8_t address);
/** @brief Mask target access, clear its borrowed pointers and restore its pads. */
void tiku_rp2350_ipc_target_close(tiku_rp2350_ipc_target_t *);
/** @brief SPI1 vector entry; moves at most eight RX and eight TX bytes. */
void tiku_rp2350_spi1_isr(void);
/** @brief I2C1 vector entry; drains at most sixteen bytes and one read request. */
void tiku_rp2350_i2c1_isr(void);
#endif
