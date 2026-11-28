/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dsi_arch.c - ESP32-P4 revision 3 DSI host and D-PHY.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_dsi_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_pmu_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_dwgdma_arch.h"
#include <hal/tiku_cpu.h>
#include <hal/tiku_clock_hal.h>

#define HOST(offset) (0x500a0000u + (offset))
#define HOST_POWER        HOST(0x04)
#define CLOCK_DIV         HOST(0x08)
#define PIXEL_FORMAT      HOST(0x10)
#define PACKET_CONTROL    HOST(0x2c)
#define MODE              HOST(0x34)
#define VIDEO_MODE        HOST(0x38)
#define COMMAND_MODE      HOST(0x68)
#define PACKET_HEADER     HOST(0x6c)
#define FIFO_STATUS       HOST(0x74)
#define LANE_CONTROL      HOST(0x94)
#define PHY_POWER         HOST(0xa0)
#define PHY_INTERFACE     HOST(0xa4)
#define PHY_STATUS        HOST(0xb0)
#define PHY_TEST_CLOCK    HOST(0xb4)
#define PHY_TEST_DATA     HOST(0xb8)
#define BRIDGE_ENABLE     HOST(0x804)
#define SYS_CLOCK         0x500e6018u
#define PHY_CLOCK_MUX     0x500e6038u
#define DSI_CLOCK         0x500e603cu
#define BRIDGE_RESET      0x500e60c0u
#define READ              TIKU_P4_REG_READ
#define WRITE             TIKU_P4_REG_WRITE

static void field(uintptr_t reg, uint32_t mask, uint32_t value)
{
    WRITE(reg, (READ(reg) & ~mask) | value);
}

static int wait_field(uintptr_t reg, uint32_t mask, uint32_t value)
{
    for (unsigned i = 0; i < 500000u; ++i) {
        if ((READ(reg) & mask) == value) return 0;
    }
    return -1;
}

static void phy_write(uint8_t reg, uint8_t value)
{
    WRITE(PHY_TEST_CLOCK, 0);
    WRITE(PHY_TEST_DATA, (1u << 16) | reg);
    WRITE(PHY_TEST_CLOCK, 2);
    WRITE(PHY_TEST_CLOCK, 0);
    WRITE(PHY_TEST_DATA, value);
    WRITE(PHY_TEST_CLOCK, 2);
    WRITE(PHY_TEST_CLOCK, 0);
}

int tiku_p4_dsi_init(void)
{
    if (tiku_p4_pmu_mipi_ldo_init(2500u) != 0) return -1;
    tiku_clock_arch_delay(200);
    field(SYS_CLOCK, 1u << 12, 1u << 12);
    field(BRIDGE_RESET, 1u << 26, 1u << 26);
    field(BRIDGE_RESET, 1u << 26, 0);
    field(PHY_CLOCK_MUX, 3u << 30, 0);
    /* Gate the PHY configuration and XTAL reference clocks. */
    field(DSI_CLOCK, (0x7ffu << 16) | 3u, 3u);
    WRITE(PHY_INTERFACE, 1);
    WRITE(HOST_POWER, 1);
    WRITE(PHY_POWER, 1);
    WRITE(PHY_POWER, 3);
    WRITE(PHY_POWER, 15);
    WRITE(BRIDGE_ENABLE, 2);
    WRITE(BRIDGE_ENABLE, 0);
    /* 40 MHz times 90 divided by 4, within the 900..949 Mbit/s range. */
    phy_write(0x44, 0x14);
    phy_write(0x22, 0x80);
    phy_write(0x19, 0x30);
    phy_write(0x17, 3);
    phy_write(0x18, 89 & 31);
    phy_write(0x18, 0x80 | (89 >> 5));
    if (wait_field(PHY_STATUS, 0x95, 0x95)) return -2;
    WRITE(MODE, 1);
    WRITE(LANE_CONTROL, 3);
    WRITE(HOST(0x98), (46u << 16) | 128u);
    WRITE(HOST(0x9c), (50u << 16) | 104u);
    WRITE(PACKET_CONTROL, 0x19);
    WRITE(CLOCK_DIV, (11u << 8) | 6u);
    WRITE(PHY_INTERFACE, (63u << 8) | 1u);
    WRITE(HOST(0x78), 0xffffu);
    WRITE(HOST(0xf4), 6000u);
    WRITE(COMMAND_MODE, (7u << 16) | (1u << 24));
    return 0;
}

int tiku_p4_dsi_command(uint8_t command, int parameter)
{
    if (parameter < -1 || parameter > 255 || wait_field(FIFO_STATUS, 2, 0))
        return -1;
    uint32_t header = (uint32_t)command << 8;
    header |= parameter < 0 ? 0x05u : 0x15u | ((uint32_t)parameter << 16);
    WRITE(PACKET_HEADER, header);
    return wait_field(FIFO_STATUS, 1, 1);
}

int tiku_p4_dsi_read(uint8_t command, uint8_t *value)
{
    if (!value || wait_field(FIFO_STATUS, 1, 1)) return -1;
    uint32_t mode = READ(MODE);
    WRITE(MODE, 1);
    field(PACKET_CONTROL, 4u, 4u);
    WRITE(PACKET_HEADER, 0x137u); /* one-byte maximum return packet */
    int rc = wait_field(FIFO_STATUS, 1, 1);
    if (!rc) {
        WRITE(PACKET_HEADER, ((uint32_t)command << 8) | 0x06u);
        rc = wait_field(FIFO_STATUS, 0x50, 0);
        if (!rc) *value = (uint8_t)READ(HOST(0x70));
    }
    field(PACKET_CONTROL, 4u, 0);
    WRITE(MODE, mode);
    return rc;
}

static void video_timing(unsigned divider, uint32_t sync, uint32_t back_porch,
                         uint32_t line)
{
    /* The DPI clock derives from 160 MHz. DSI horizontal periods use
     * 112.5 MHz lane-byte cycles. */
    field(DSI_CLOCK, 0xffe0u, ((divider - 1u) << 8) | (1u << 7) | (2u << 5));
    WRITE(PIXEL_FORMAT, 5);  /* 24-bit RGB */
    WRITE(HOST(0x3c), 1024);
    WRITE(HOST(0x40), 0);
    WRITE(HOST(0x44), 0);
    WRITE(HOST(0x48), sync);
    WRITE(HOST(0x4c), back_porch);
    WRITE(HOST(0x50), line);
    WRITE(HOST(0x54), 1);
    WRITE(HOST(0x58), 23);
    WRITE(HOST(0x5c), 12);
    WRITE(HOST(0x60), 600);
}

int tiku_p4_dsi_pattern(void)
{
    video_timing(3, 22, 346, 2929);
    WRITE(VIDEO_MODE, (1u << 16) | 2u);
    WRITE(MODE, 0);
    return (READ(PHY_STATUS) & 1u) && READ(VIDEO_MODE) == 0x10002u &&
           !(READ(HOST(0xbc)) | READ(HOST(0xc0))) ? 0 : -1;
}


#define FRAME_BYTES (1024u * 600u * 2u)
#define DISPLAY_DMA_CHANNEL 2u
static struct tiku_p4_dwgdma_block scan_block = {
    .dst = (void *)(uintptr_t)TIKU_P4_DWGDMA_DSI_FIFO,
    .beats = FRAME_BYTES / 8u, .dst_fixed = 1, .src_msize = 8, .dst_msize = 7,
    .flow = TIKU_P4_DWGDMA_FLOW_M2P_DMA, .dst_periph = TIKU_P4_DWGDMA_PERIPH_DSI,
    .src_outstanding = 5, .dst_outstanding = 2, .axi_len = 15
};
static const uint16_t *volatile front;
static const uint16_t *volatile queued;
static volatile uint32_t completions;
static volatile int scan_error;

int tiku_p4_dsi_valid_frame(const uint16_t *pixels)
{
    return pixels && !((uintptr_t)pixels & 63u) &&
           tiku_p4_dwgdma_reachable(pixels, FRAME_BYTES);
}

static void scan_done(unsigned channel, uint32_t status, void *ctx)
{
    (void)ctx;
    if (status & TIKU_P4_DWGDMA_ST_ERRORS) {
        scan_error = -4;
        return;
    }
    ++completions;
    const uint16_t *next = queued ? queued : front;
    scan_block.src = next;
    if (tiku_p4_dwgdma_chan_start(channel, &scan_block, scan_done, 0)) {
        scan_error = -2;
        return;
    }
    front = next;
    queued = 0;
}

int tiku_p4_dsi_start(const uint16_t *pixels)
{
    if (front || !tiku_p4_dsi_valid_frame(pixels)) return -1;
    video_timing(4, 29, 462, 3906);
    WRITE(HOST(0x830), (600u << 16) | 636u);
    WRITE(HOST(0x834), (1u << 16) | 23u);
    WRITE(HOST(0x838), (1024u << 16) | 1389u);
    WRITE(HOST(0x83c), (10u << 16) | 160u);
    WRITE(HOST(0x80c), (1u << 31) | (FRAME_BYTES / 8u));
    WRITE(HOST(0x818), 2u); /* RGB565 input, RGB888 on the lanes. */
    WRITE(HOST(0x888), 1u << 4);
    WRITE(HOST(0x808), 256u);
    WRITE(HOST(0x88c), 768u);
    WRITE(BRIDGE_ENABLE, 1);
    WRITE(HOST(0x840), 636u << 4);
    WRITE(HOST(0x844), 1);
    tiku_cpu_dcache_clean(pixels, FRAME_BYTES);
    WRITE(HOST(0x854), 3);
    completions = 0;
    scan_error = 0;
    scan_block.src = pixels;
    front = pixels;
    if (tiku_p4_dwgdma_chan_start(DISPLAY_DMA_CHANNEL, &scan_block, scan_done, 0)) {
        front = 0;
        return -2;
    }
    WRITE(VIDEO_MODE, 2u);
    WRITE(MODE, 0);
    WRITE(HOST(0x840), (636u << 4) | 1u);
    WRITE(HOST(0x844), 1);
    return 0;
}

int tiku_p4_dsi_queue(const uint16_t *pixels)
{
    if (!tiku_p4_dsi_valid_frame(pixels)) return -1;
    uint32_t irq = TIKU_P4_IRQ_SAVE();
    int rc = -1;
    if (front && !queued && !scan_error) {
        uintptr_t a = (uintptr_t)pixels, b = (uintptr_t)front;
        if ((a >= b ? a - b : b - a) >= FRAME_BYTES) {
            queued = pixels;
            rc = 0;
        }
    }
    TIKU_P4_IRQ_RESTORE(irq);
    return rc;
}

int tiku_p4_dsi_underrun(void)
{
    return (READ(HOST(0x858)) & 1u) != 0;
}

int tiku_p4_dsi_showing(const uint16_t *pixels)
{
    if (scan_error || !front || !pixels) return -1;
    return front == pixels;
}

void tiku_p4_dsi_stop(void)
{
    uint32_t irq = TIKU_P4_IRQ_SAVE();
    WRITE(HOST(0x840), 636u << 4);
    WRITE(HOST(0x844), 1);
    if (front) tiku_p4_dwgdma_chan_stop(DISPLAY_DMA_CHANNEL);
    front = 0;
    queued = 0;
    TIKU_P4_IRQ_RESTORE(irq);
}

uint32_t tiku_p4_dsi_events(uint32_t *transfers, int *error)
{
    uint32_t events = READ(HOST(0x858)) & 3u;
    if (events) WRITE(HOST(0x854), events);
    *transfers = completions;
    *error = scan_error;
    return events;
}
