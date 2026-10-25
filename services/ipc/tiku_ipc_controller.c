/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_controller.c - CPU-copy SPI and I2C packet adapters.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_controller.h"
#include <interfaces/bus/tiku_spi_bus.h>
#include <interfaces/bus/tiku_i2c_bus.h>
#if TIKU_IPC_ENABLE
/** @brief Finish the blocking transaction before relinquishing either pointer. */
static tiku_ipc_err_t start(void *context, const void *tx, void *rx, uint16_t length)
{
    tiku_ipc_controller_t *c = context;
    int result;
    if (c->pending) return TIKU_IPC_AGAIN;
    if (c->spi) {
        c->select(c->select_context,1);
        result = tiku_spi_write_read(tx,rx,length);
        c->select(c->select_context,0);
    } else result = tiku_i2c_write_read(c->address,tx,length,rx,length);
    c->result = result == 0 ? TIKU_IPC_OK : TIKU_IPC_IO;
    c->pending = 1;
    return TIKU_IPC_OK;
}
/** @brief The blocking bus operation retains no pointer after start returns. */
static tiku_ipc_err_t poll(void *context)
{
    tiku_ipc_controller_t *c = context;
    if (!c->pending) return TIKU_IPC_INVALID;
    c->pending = 0;
    return c->result;
}
/** @brief End the CPU-copy reservation; bus calls have already returned. */
static uint8_t abort_transfer(void *context)
{ ((tiku_ipc_controller_t *)context)->pending = 0; return 1; }
/** @brief Populate the CPU-copy engine descriptor for a statically owned bus. */
static int init(tiku_ipc_controller_t *c, const tiku_ipc_access_domain_t *domain,
                 uint32_t peer, tiku_clock_time_t burst_ticks)
{
    if (!c || !domain || domain->engine != 0 || domain->peer != peer ||
        !burst_ticks || burst_ticks > TIKU_CLOCK_MAX_INTERVAL) return -1;
    *c = (tiku_ipc_controller_t){0};
    c->io = (tiku_ipc_packet_io_t){start,poll,abort_transfer,c,domain,0,peer,burst_ticks,NULL};
    return 0;
}
int tiku_ipc_controller_spi(tiku_ipc_controller_t *c,
    const tiku_ipc_access_domain_t *domain, uint32_t peer,
    tiku_clock_time_t burst_ticks, void (*select)(void *, uint8_t), void *context)
{
    if (!select || init(c,domain,peer,burst_ticks)) return -1;
    c->spi = 1; c->select = select; c->select_context = context;
    return 0;
}
int tiku_ipc_controller_i2c(tiku_ipc_controller_t *c,
    const tiku_ipc_access_domain_t *domain, uint32_t peer,
    tiku_clock_time_t burst_ticks, uint8_t address)
{
    if (!address || address >= 0x78 || init(c,domain,peer,burst_ticks)) return -1;
    c->address = address;
    return 0;
}
#endif
