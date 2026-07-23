/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dc_arch.c - Apollo510 display path: NemaDC + DSI host + CO5300 panel.
 *
 * From-scratch, register-level, no vendor code linked. Provenance of every
 * sequence below (three independent sources, cross-checked):
 *   [TSI]  open MIT-granted ThinkSi sources: nema_dc_regs.h/_intern/_mipi/_dsi
 *          register map + the fully-open Ambiq port layer nema_dc_hal.c
 *          (configure / command-send / frame-transfer orchestration).
 *   [DIS]  disassembly of the vendored blobs (lib_nema_apollo510_nemagfx.a
 *          nema_dc*.o primitives; libam_hal.a am_hal_dsi/clkgen/pwrctrl).
 *   [CAP]  J-Link register capture of the vendor demo running on this exact
 *          board (2026-07-23) -- the golden values quoted in comments. Where
 *          an algorithm was not recovered bit-exact (D-PHY timing), the
 *          captured words are carried as constants for the shipped frequency.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dc_arch.h"
#include "apollo510.h"            /* CMSIS: PWRCTRL/GPIO bases + GPIO_Type    */
#include <hal/tiku_cpu.h>         /* tiku_cpu_dcache_clean                    */
#include <kernel/cpu/tiku_common.h>   /* tiku_common_delay_ms / _us           */

/*---------------------------------------------------------------------------*/
/* Register access                                                           */
/*---------------------------------------------------------------------------*/

#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))

#define DC_RD(off)      REG32(DC_BASE  + (off))
#define DC_WR(off, v)   (REG32(DC_BASE  + (off)) = (v))
#define DSI_RD(off)     REG32(DSI_BASE + (off))
#define DSI_WR(off, v)  (REG32(DSI_BASE + (off)) = (v))

/* Blocks the CMSIS header names but whose members we address by offset so
 * every write maps 1:1 onto the recovered sequences. */
#define CLKGEN_DISPCLKCTRL   REG32(CLKGEN_BASE + 0x84u)
#define CLKGEN_CLKCTRL       REG32(CLKGEN_BASE + 0x120u)

/*---------------------------------------------------------------------------*/
/* NemaDC register map ([TSI] nema_dc_regs.h; offsets from DC_BASE)          */
/*---------------------------------------------------------------------------*/

#define DC_MODE          0x000u   /* NEMADC_ONE_FRAME=b17, OUTP_OFF=b3        */
#define DC_CLKCTRL       0x004u
#define DC_BGCOLOR       0x008u
#define DC_RESXY         0x00Cu
#define DC_PLAY          0x010u   /* written 0 after every MODE write [DIS]   */
#define DC_FRONTPORCH    0x014u
#define DC_BLANKING      0x018u
#define DC_BACKPORCH     0x01Cu
#define DC_STARTXY       0x024u
#define DC_IF_CFG        0x028u   /* MIPICFG_* word                           */
#define DC_GPIO          0x02Cu   /* b0=LP mode, b5=TE-int-en (Ambiq wrapper) */
#define DC_L0_MODE       0x030u
#define DC_L0_STARTXY    0x034u
#define DC_L0_SIZEXY     0x038u
#define DC_L0_BASEADDR   0x03Cu
#define DC_L0_STRIDE     0x040u
#define DC_L0_RESXY      0x044u
#define DC_IF_CMD        0x0E8u   /* command/data FIFO                        */
#define DC_CONFIG        0x0F0u
#define DC_IDREG         0x0F4u
#define DC_INTERRUPT     0x0F8u   /* b4 = frame-end enable                    */
#define DC_STATUS        0x0FCu
#define DC_L0_CDEC_XY    0x104u   /* IP > 0x220300 only (ours: 0x230601)      */
#define DC_L0_FORMAT     0x114u   /* IP > 0x2301FF only                       */
#define DC_IP_VERSION    0x180u
#define DC_FORMAT_CTRL   0x1A0u   /* DSI data-type / cmd-type                 */
#define DC_FORMAT_CTRL2  0x1A4u
#define DC_CLKCTRL_CG    0x1A8u

#define DC_ID_MAGIC      0x87452365u
#define DC_STAT_PENDCMD  (1u << 11)
#define DC_STAT_DBI_BUSY 0x17C00u          /* b10..b14 | b16 [TSI]            */
#define DC_STAT_ACTIVE   0x00003u          /* b0 active | b1 framegen busy    */

/* Interface config for DBIDSI + RGB888-over-DBI16 + no TE:
 * DBI_EN | RESX | EXT_CTRL | BLANKING_EN | EN_STALL | MIPICFG_16RGB888_OPT0.
 * [TSI] composition, [CAP] golden 0x82203087. */
#define DC_IFCFG_DSI_RGB888   0x82203087u
#define DC_IFCFG_SPI_HOLD     (1u << 17)
#define DC_CMD_EXT            0x02000000u  /* EXT_CTRL cmd flag (DSI)         */
#define DC_CMD_DBI            0x40000000u  /* command (vs data) FIFO flag     */

/*---------------------------------------------------------------------------*/
/* DSI host registers ([DIS] am_hal_dsi.o; offsets from DSI_BASE)            */
/*---------------------------------------------------------------------------*/

#define DSI_DEVICEREADY     0x00u
#define DSI_INTRSTAT        0x04u          /* b28 INITDONE (RW1C), b19 LOWC   */
#define DSI_FUNCPRG         0x0Cu
#define DSI_HSTXTIMEOUT     0x10u
#define DSI_LPRXTO          0x14u
#define DSI_TURNARNDTO      0x18u
#define DSI_RESETTIMER      0x1Cu
#define DSI_LANESWCNT       0x44u
#define DSI_INITCNT         0x50u
#define DSI_CLKEOT          0x5Cu
#define DSI_LPBYTECLK       0x68u
#define DSI_DPHYPARAM       0x6Cu
#define DSI_CLKLANETIM      0x70u
#define DSI_RSTENBDFE       0x74u
#define DSI_AFETRIM0        0x78u
#define DSI_AFETRIM1        0x7Cu
#define DSI_AFETRIM2        0x80u
#define DSI_AFETRIM3        0x84u

/*
 * D-PHY timing words for FREQ_TRIM_X20 (240 MHz PLL, 480 Mbps/lane). The
 * vendor computes these from D-PHY spec targets in double-precision at run
 * time; the selection logic is not recovered bit-exact, so we carry the
 * words [CAP]tured from this board at this trim. Recompute/redump if the
 * trim ever changes.
 */
#define DSI_TRIM_X20            0x0Au
#define DSI_LPBYTECLK_X20       0x00000002u
#define DSI_DPHYPARAM_X20       0x05040783u
#define DSI_CLKLANETIM_X20      0x05040F02u

/*---------------------------------------------------------------------------*/
/* Board pins (Apollo510 EVB display kit; [DIS] libam_bsp.a)                 */
/*---------------------------------------------------------------------------*/

#define PIN_DISP_QSPI_CS   209u   /* driven high in DSI mode (strap/deassert) */
#define PIN_DISP_TE         33u   /* FNCSEL 9 = dedicated DISP_TE             */
#define PIN_DISP_RST        63u   /* panel reset, active low                  */
#define PIN_VDD18_SW        58u   /* MIPI 1.8 V rail switch                   */

#define PINCFG_GPIO_OUT    0x00000503u    /* FNCSEL=3 GPIO, push-pull, DS 0.5x */
#define PINCFG_DISP_TE     0x00000C09u    /* FNCSEL=9 DISP_TE, DS 3 (verbatim) */

/*---------------------------------------------------------------------------*/
/* State                                                                     */
/*---------------------------------------------------------------------------*/

#define DC_SPIN_MAX   4000000u   /* ~100 ms-class bound at scanout speeds     */

static uint32_t s_frames;        /* frames successfully presented            */
static uint32_t s_ipver;         /* DC IP_VERSION ([CAP] 0x00230601)          */

/*---------------------------------------------------------------------------*/
/* Small helpers                                                             */
/*---------------------------------------------------------------------------*/

static void
pin_config(uint32_t pad, uint32_t cfg)
{
    GPIO->PADKEY = 0x73u;                 /* GPIO_PADKEY unlock               */
    (&GPIO->PINCFG0)[pad] = cfg;
    GPIO->PADKEY = 0u;
}

static void
pin_write(uint32_t pad, uint32_t level)
{
    uint32_t mask = 1u << (pad & 31u);
    if (level) { (&GPIO->WTS0)[pad >> 5] = mask; }
    else       { (&GPIO->WTC0)[pad >> 5] = mask; }
}

/** Bounded poll: wait until (reg & mask) == want. 1 on success. */
static int
poll32(uintptr_t addr, uint32_t mask, uint32_t want)
{
    uint32_t spins = 0u;
    while ((REG32(addr) & mask) != want) {
        if (++spins > DC_SPIN_MAX) {
            return 0;
        }
    }
    return 1;
}

static int  dc_wait_pendcmd(void)  { return poll32(DC_BASE + DC_STATUS, DC_STAT_PENDCMD, 0u); }
static int  dc_wait_dbi_idle(void) { return poll32(DC_BASE + DC_STATUS, DC_STAT_DBI_BUSY, 0u); }

/** FIFO write with the mandatory pending-cmd poll before it. [DIS] */
static void
dc_fifo(uint32_t word)
{
    (void)dc_wait_pendcmd();
    DC_WR(DC_IF_CMD, word);
}

/*---------------------------------------------------------------------------*/
/* DSI command channel (LP mode) -- [TSI] nema_dc_hal.c dsi_dcs_write /      */
/* dsi_generic_write, reduced to the short-write forms the panel init uses.  */
/*---------------------------------------------------------------------------*/

/** DCS command with n parameter bytes (n may be 0). */
static void
dsi_dcs(uint8_t cmd, const uint8_t *p, uint32_t n)
{
    uint32_t cfg, i;

    DC_WR(DC_GPIO, DC_RD(DC_GPIO) | 0x1u);          /* LP (escape) mode       */
    DC_WR(DC_FORMAT_CTRL, 0u);                      /* DCS auto short/long    */
    (void)dc_wait_dbi_idle();
    cfg = DC_RD(DC_IF_CFG);
    (void)dc_wait_pendcmd();
    DC_WR(DC_IF_CFG, cfg | DC_IFCFG_SPI_HOLD);      /* pack into one packet   */
    dc_fifo(DC_CMD_DBI | DC_CMD_EXT | cmd);
    for (i = 0u; i < n; i++) {
        dc_fifo(DC_CMD_EXT | p[i]);
    }
    (void)dc_wait_pendcmd();
    DC_WR(DC_IF_CFG, cfg);                          /* release -> transmit    */
    (void)dc_wait_dbi_idle();
}

/** Generic (non-DCS) 2-byte write: register address + one value byte. */
static void
dsi_generic2(uint8_t reg, uint8_t val)
{
    uint32_t cfg;
    uint8_t  b[2];
    uint32_t i;

    b[0] = reg;
    b[1] = val;

    DC_WR(DC_GPIO, DC_RD(DC_GPIO) & ~0x6u);         /* DSI virtual channel 0  */
    DC_WR(DC_GPIO, DC_RD(DC_GPIO) | 0x1u);          /* LP mode                */
    DC_WR(DC_FORMAT_CTRL, 0xC0000000u);             /* generic data/cmd type  */
    (void)dc_wait_dbi_idle();
    cfg = DC_RD(DC_IF_CFG);
    DC_WR(DC_GPIO, DC_RD(DC_GPIO) & ~0x8u);         /* short (non-overlong)   */
    (void)dc_wait_pendcmd();
    DC_WR(DC_IF_CFG, cfg | DC_IFCFG_SPI_HOLD);
    for (i = 0u; i < 2u; i++) {                     /* n<3: cmd-flag each byte */
        dc_fifo(DC_CMD_DBI | DC_CMD_EXT | b[i]);
    }
    (void)dc_wait_pendcmd();
    DC_WR(DC_IF_CFG, cfg);
    (void)dc_wait_dbi_idle();
}

/*---------------------------------------------------------------------------*/
/* Bring-up stages                                                           */
/*---------------------------------------------------------------------------*/

/** Display pins ([DIS] am_bsp_disp_pins_enable, DSI order; touch INT skipped). */
static void
dc_pins_init(void)
{
    pin_config(PIN_DISP_QSPI_CS, PINCFG_GPIO_OUT);
    pin_write(PIN_DISP_QSPI_CS, 1u);
    pin_config(PIN_DISP_TE, PINCFG_DISP_TE);
    pin_config(PIN_DISP_RST, PINCFG_GPIO_OUT);
    pin_write(PIN_DISP_RST, 1u);
    pin_config(PIN_VDD18_SW, PINCFG_GPIO_OUT);      /* rail switched in dsi   */
}

/** DISPPHY power + DSI/DC clock tree ([DIS] am_hal_dsi_init). */
static tiku_dc_err_t
dsi_clocks_init(void)
{
    uint32_t spins = 0u;

    PWRCTRL->DEVPWREN |= (1u << 20);                /* PWRENDISPPHY           */
    while ((PWRCTRL->DEVPWRSTATUS & (1u << 20)) == 0u) {
        if (++spins > DC_SPIN_MAX) { return TIKU_DC_ERR_POWER; }
    }

    DSI_WR(DSI_RSTENBDFE, 0u);                      /* hold DPHY in reset     */
    pin_write(PIN_VDD18_SW, 1u);                    /* MIPI 1.8 V rail on     */

    /* CLKGEN, in the exact vendor RMW order. [CAP] final DISPCLKCTRL=0x192. */
    CLKGEN_DISPCLKCTRL = (CLKGEN_DISPCLKCTRL & ~0xE0u) | (4u << 5); /* HFRC96 */
    CLKGEN_DISPCLKCTRL &= ~0x800u;                  /* DBICLKDIV2EN off       */
    CLKGEN_DISPCLKCTRL &= ~0x600u;                  /* DBICLKSEL = DBIB       */
    CLKGEN_CLKCTRL     |= 0x100u;                   /* DISPCTRLCLKEN          */
    CLKGEN_DISPCLKCTRL |= 0x100u;                   /* DCCLKEN                */
    CLKGEN_DISPCLKCTRL = (CLKGEN_DISPCLKCTRL & ~0xFu) | 0x2u; /* PLL ref HFRC12 */
    CLKGEN_DISPCLKCTRL |= 0x10u;                    /* PLLCLKEN               */
    return TIKU_DC_OK;
}

/** DSI PHY parameter config ([DIS] am_hal_dsi_para_config: 1 lane, DBI16,
 *  trim X20, no ULPS pattern). */
static tiku_dc_err_t
dsi_phy_config(void)
{
    DSI_WR(DSI_RSTENBDFE,   0u);
    DSI_WR(DSI_FUNCPRG,     0x8000u | 1u);          /* DBI16 code 4, 1 lane   */
    DSI_WR(DSI_HSTXTIMEOUT, 0x00FFFFFFu);
    DSI_WR(DSI_LPRXTO,      0xFFFFu);
    DSI_WR(DSI_TURNARNDTO,  0x1Fu);
    DSI_WR(DSI_RESETTIMER,  0xFFu);
    DSI_WR(DSI_LANESWCNT,   0xFFFFu);
    DSI_WR(DSI_INITCNT,     2000u);
    DSI_WR(DSI_CLKEOT,      0x2u);                  /* non-continuous clock   */
    DSI_WR(DSI_CLKEOT,      DSI_RD(DSI_CLKEOT) | 0x1u);   /* EOT packets      */

    /* D-PHY timing for trim X20 ([CAP] golden words, see constants above). */
    DSI_WR(DSI_LPBYTECLK,   DSI_LPBYTECLK_X20);
    DSI_WR(DSI_DPHYPARAM,   DSI_DPHYPARAM_X20);
    DSI_WR(DSI_CLKLANETIM,  DSI_CLKLANETIM_X20);
    DSI_WR(DSI_AFETRIM1, (DSI_RD(DSI_AFETRIM1) & ~0x7Fu) | DSI_TRIM_X20);

    /* Analog front-end trims -- verbatim magic, do not "clean up". [DIS] */
    DSI_WR(DSI_AFETRIM2, 0x10000000u);
    DSI_WR(DSI_AFETRIM2, DSI_RD(DSI_AFETRIM2) | 0x480000u);   /* 1-lane       */
    DSI_WR(DSI_AFETRIM1, DSI_RD(DSI_AFETRIM1) | 0x2000u);
    if ((MCUCTRL->CHIPREV & 0xFFu) != 0x21u) {      /* [CAP] ours: 0x23       */
        DSI_WR(DSI_AFETRIM0, DSI_RD(DSI_AFETRIM0) | 0x20000u);
    }

    DSI_WR(DSI_RSTENBDFE, 1u);                      /* release DPHY reset     */
    DSI_WR(DSI_DEVICEREADY, DSI_RD(DSI_DEVICEREADY) | 1u);
    if (!poll32(DSI_BASE + DSI_INTRSTAT, 1u << 28, 1u << 28)) {  /* INITDONE  */
        return TIKU_DC_ERR_DSI;
    }
    if (DSI_RD(DSI_INTRSTAT) & (1u << 19)) {        /* W1C LP low-contention  */
        DSI_WR(DSI_INTRSTAT, DSI_RD(DSI_INTRSTAT) | (1u << 19));
    }
    return TIKU_DC_OK;
}

/** DC block init + DBIDSI configure ([TSI] nemadc_init/_configure + [CAP]). */
static tiku_dc_err_t
dc_core_init(void)
{
    uint32_t spins = 0u;

    PWRCTRL->DEVPWREN |= (1u << 19);                /* PWRENDISP              */
    while ((PWRCTRL->DEVPWRSTATUS & (1u << 19)) == 0u) {
        if (++spins > DC_SPIN_MAX) { return TIKU_DC_ERR_POWER; }
    }

    DC_WR(DC_INTERRUPT, 0u);
    DC_WR(DC_GPIO, 0x20u);                          /* TE-int path default    */
    if (DC_RD(DC_IDREG) != DC_ID_MAGIC) {
        return TIKU_DC_ERR_ID;
    }
    s_ipver = DC_RD(DC_IP_VERSION);                 /* [CAP] 0x00230601       */
    return TIKU_DC_OK;
}

static void
dc_timing_468(void)
{
    /* nemadc_timing(468,1,1,1, 468,1,1,1) -- [CAP]-verified words. */
    DC_WR(DC_RESXY,      (468u << 16) | 468u);
    DC_WR(DC_FRONTPORCH, (469u << 16) | 469u);
    DC_WR(DC_BLANKING,   (470u << 16) | 470u);
    DC_WR(DC_BACKPORCH,  (471u << 16) | 471u);
    DC_WR(DC_STARTXY,    (469u << 16) | 468u);
}

static void
dc_configure(void)
{
    /* Divider: DISPCLKSEL=HFRC96 -> primary div 1; div2=1, prefetch=4.
     * [CAP] golden 0x02000401 (new-IP 7-bit-divider layout). */
    DC_WR(DC_CLKCTRL, 0x02000401u);
    DC_WR(DC_FORMAT_CTRL2, 0x2u << 30);             /* DBIB clk = fmt clk / 2 */
    (void)dc_wait_pendcmd();
    DC_WR(DC_IF_CFG, DC_IFCFG_DSI_RGB888);
    dc_timing_468();
    DC_WR(DC_CLKCTRL_CG, 1u);                       /* pixel-clock out enable */
}

/** CO5300 panel init over DSI ([DIS] BSP + open raydium driver, no-TE path). */
static void
panel_init(void)
{
    static const uint8_t colmod[]   = { 0x07 };     /* 24-bit RGB888          */
    static const uint8_t madctl[]   = { 0x00 };     /* no flip                */
    static const uint8_t caset[]    = { 0x00, 0x06, 0x01, 0xD9 };  /* 6..473  */
    static const uint8_t raset[]    = { 0x00, 0x00, 0x01, 0xD3 };  /* 0..467  */

    /* Hardware reset: high 5 ms, low 20 ms, high 150 ms. */
    pin_write(PIN_DISP_RST, 1u);  tiku_common_delay_ms(5u);
    pin_write(PIN_DISP_RST, 0u);  tiku_common_delay_ms(20u);
    pin_write(PIN_DISP_RST, 1u);  tiku_common_delay_ms(150u);

    dsi_generic2(0x51u, 0xFFu);   tiku_common_delay_ms(10u);  /* brightness   */
    dsi_dcs(0x3Au, colmod, 1u);   tiku_common_delay_ms(10u);  /* COLMOD       */
    dsi_generic2(0xFEu, 0x01u);   tiku_common_delay_ms(10u);  /* MCS page 1   */
    dsi_generic2(0x0Au, 0xF8u);   tiku_common_delay_ms(10u);  /* HSIFOPCTR    */
    dsi_generic2(0xFEu, 0x00u);   tiku_common_delay_ms(10u);  /* user cmd set */
    dsi_dcs(0x36u, madctl, 1u);   tiku_common_delay_ms(10u);  /* MADCTL       */

    /* CO5300 CMD2 unlock sequence (verbatim). */
    dsi_generic2(0xFEu, 0x20u);
    dsi_generic2(0xF4u, 0x5Au);
    dsi_generic2(0xF5u, 0x59u);
    dsi_generic2(0xFEu, 0x80u);
    dsi_generic2(0x00u, 0xF8u);
    dsi_generic2(0xFEu, 0x00u);

    dsi_dcs(0x11u, 0, 0u);        tiku_common_delay_ms(130u); /* sleep out    */
    dsi_dcs(0x29u, 0, 0u);        tiku_common_delay_ms(200u); /* display on   */

    dc_timing_468();
    dsi_dcs(0x2Au, caset, 4u);                                /* column window */
    dsi_dcs(0x2Bu, raset, 4u);    tiku_common_delay_ms(200u); /* row window   */
    dsi_dcs(0x34u, 0, 0u);        tiku_common_delay_ms(10u);  /* tearing off  */
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

tiku_dc_err_t
tiku_dc_init(void)
{
    tiku_dc_err_t err;

    s_frames = 0u;
    dc_pins_init();
    err = dsi_clocks_init();
    if (err != TIKU_DC_OK) { return err; }
    err = dc_core_init();
    if (err != TIKU_DC_OK) { return err; }
    err = dsi_phy_config();
    if (err != TIKU_DC_OK) { return err; }
    dc_configure();
    panel_init();
    return TIKU_DC_OK;
}

tiku_dc_err_t
tiku_dc_present(const void *fb, uint16_t w, uint16_t h,
                uint16_t stride_bytes, tiku_dc_fmt_t fmt)
{
    uint32_t cfg;
    uint32_t xy = ((uint32_t)w << 16) | h;
    int ok;

    /* The DC reads behind the M55 D-cache: push dirty lines out first. */
    tiku_cpu_dcache_clean((void *)(uintptr_t)fb,
                          (uint32_t)stride_bytes * (uint32_t)h);

    /* Layer 0 ([DIS] nemadc_set_layer no-scale path; [CAP] RGB24 goldens).
     * Our CONFIG (0x713B3159) has no layer-0 scaler and COLMOD has no YUV
     * low bits, so the SCALEX/Y and U/V writes are correctly absent. */
    DC_WR(DC_L0_BASEADDR, (uint32_t)(uintptr_t)fb);
    DC_WR(DC_L0_STARTXY,  0u);
    DC_WR(DC_L0_SIZEXY,   xy);
    DC_WR(DC_L0_RESXY,    xy);
    DC_WR(DC_L0_STRIDE,   stride_bytes);
    DC_WR(DC_L0_CDEC_XY,  0u);                      /* IP 0x230601 > 0x220300 */
    /* LAYER_ENABLE|AHBLOCK | alpha=0xFF | blend=SRC | format code. */
    DC_WR(DC_L0_MODE, 0x88000000u | (0xFFu << 16) | (0x01u << 8) |
                      ((uint32_t)fmt & 0x1Fu));
    DC_WR(DC_L0_FORMAT, (uint32_t)fmt);             /* IP 0x230601 > 0x2301FF */

    dc_timing_468();

    /* Prepare ([TSI] dc_transfer_frame, DSI path): HS mode, ungate clocks,
     * pixel-stream data type, then DCS write_memory_start held in the FIFO. */
    cfg = DC_RD(DC_IF_CFG);
    DC_WR(DC_GPIO, DC_RD(DC_GPIO) & ~0x1u);         /* high-speed mode        */
    DC_WR(DC_CLKCTRL_CG, 0xFFFFFFF1u);              /* clock gating off       */
    if (!dc_wait_dbi_idle()) { return TIKU_DC_ERR_TIMEOUT; }
    DC_WR(DC_FORMAT_CTRL, 0x3939u);                 /* DCS long write         */
    (void)dc_wait_pendcmd();
    DC_WR(DC_IF_CFG, cfg | DC_IFCFG_SPI_HOLD);
    dc_fifo(DC_CMD_DBI | DC_CMD_EXT | 0x2Cu);       /* write_memory_start     */
    if (!dc_wait_dbi_idle()) { return TIKU_DC_ERR_TIMEOUT; }

    /* Launch one frame ([DIS] nemadc_set_mode: MODE then PLAY=0). */
    DC_WR(DC_INTERRUPT, 1u << 4);                   /* frame-end (polled)     */
    DC_WR(DC_MODE, (1u << 17) | (1u << 3));         /* ONE_FRAME | OUTP_OFF   */
    DC_WR(DC_PLAY, 0u);

    /* Completion: vendor uses IRQ 29; we poll STATUS until the frame
     * generator and DBI bridge drain (bounded). */
    ok = poll32(DC_BASE + DC_STATUS, DC_STAT_DBI_BUSY | DC_STAT_ACTIVE, 0u);

    /* End-of-frame restore ([TSI] nemadc_transfer_frame_end). */
    DC_WR(DC_IF_CFG, cfg & ~DC_IFCFG_SPI_HOLD);
    DC_WR(DC_CLKCTRL_CG, 1u);
    DC_WR(DC_GPIO, DC_RD(DC_GPIO) | 0x1u);
    DC_WR(DC_INTERRUPT, 0u);

    if (!ok) { return TIKU_DC_ERR_TIMEOUT; }
    s_frames++;
    return TIKU_DC_OK;
}

uint32_t
tiku_dc_frame_count(void)
{
    return s_frames;
}

uint32_t
tiku_dc_status(void)
{
    return DC_RD(DC_STATUS);
}
