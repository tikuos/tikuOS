/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - Apollo 510 CPU/SoC bring-up + clocks
 *
 * Clock facts (Apollo510, hardware-confirmed):
 *   - CPU core: 96 MHz in Low-Power mode (the SBL default), ~250 MHz in
 *     High-Performance "turbo" mode (the M55 runs from the free-running
 *     HFRC2; there is no HP frequency select -- CMSIS comments saying 192
 *     are stale Apollo4-era text). HP is a live feature now: `freq 250`
 *     performs the full SPOT bring-up; see tiku_cpu_freq_ambiq_init().
 *   - SysTick timer clock: 48 MHz = core/2 on this Cortex-M55. That is the OS
 *     tick + busy-delay timebase (see TIKU_MAIN_CPU_HZ in tiku.h, and the
 *     SysTick delay in tiku_cpu_common.c) — NOT the core clock.
 *   - The HFRC "free-run ~48 MHz" the SBL leaves running is a peripheral
 *     reference oscillator, unrelated to the core clock.
 * s_core_hz below reports the TRUE core clock (read from the perf-mode reg).
 *
 * SoC bring-up is fully bare-metal now (direct CMSIS register access, no
 * AmbiqSuite SDK): it enables the I/D caches + M55 prefetch unit and otherwise
 * inherits the power rails and clock tree exactly as the secure bootloader
 * (SBL) left them. Each dropped am_hal_* call is documented inline below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "apollo510.h"       /* CMSIS register defs (PWRCTRL/CLKGEN/MEMSYSCTL) -- register header only */
#include "tiku_cpu_freq_boot_arch.h"

/** True CPU core frequency in Hz; updated from the perf-mode register at boot */
static unsigned long s_core_hz = 96000000UL;  /* true CPU core; set from perf mode */

/**
 * @brief Bare-metal Apollo510 SoC bring-up (caches + prefetch)
 *
 * Enables the Cortex-M55 I/D caches and prefetch unit via CMSIS, then
 * inherits the power rails and clock tree exactly as the Secure Boot
 * Loader (SBL) left them. Each dropped am_hal_* call is documented
 * inline — a wrong drop would brown out the chip at boot.
 *
 * De-SDK steps performed here:
 *   - Step 3 (TEST): skip am_hal_pwrctrl_low_power_init (SBL provides
 *     a stable power state; brown-out = proof the call was load-bearing).
 *   - Step 1:  dropped SIMOBUCK_INIT and temp_update — efficiency
 *     upgrades only, not correctness.
 *   - Step 2a: dropped clkmgr XTAL bookkeeping and HFRC2 config.
 *   - Step 2b (TEST): drop am_hal_clkmgr_clock_config(HFRC) — the
 *     SBL HFRC already free-runs near 48 MHz; UART cleanness is the
 *     canary.
 */
static void tiku_ambiq_soc_init(void) {
    /* De-SDK step 3 (TEST): skip am_hal_pwrctrl_low_power_init. The SBL leaves
     * the chip in a usable power state -- our reset handler and all early boot
     * already ran on it before this call ever executed. If the system boots and
     * runs stably with a steady VDD_MCU, the SBL power suffices and we reach
     * ZERO am_hal calls; if it browns out (no boot / hang / instability), the
     * LDO/voltage config is load-bearing and we transcribe the essentials. */

    /* Enable the Cortex-M55 I/D caches bare-metal (CMSIS), replacing
     * am_hal_cachectrl_icache/dcache_enable(). The HAL versions are just the
     * CMSIS SCB_Enable*Cache() calls plus the M55 prefetch-unit tuning below
     * (Apollo RevB defaults: MAX_OS=6, MAX_LA=6, MIN_LA=4). */
    SCB_EnableICache();
    MEMSYSCTL->PFCR = (6u << MEMSYSCTL_PFCR_MAX_OS_Pos) |
                      (6u << MEMSYSCTL_PFCR_MAX_LA_Pos) |
                      (4u << MEMSYSCTL_PFCR_MIN_LA_Pos) |
                      (1u << MEMSYSCTL_PFCR_ENABLE_Pos);
    SCB_EnableDCache();
    SCB_CleanDCache();

    /* De-SDK step 1: dropped the optional power-OPTIMISATION calls
     * am_hal_pwrctrl_control(SIMOBUCK_INIT) and am_hal_pwrctrl_temp_update(25C).
     * The core already runs on the LDO that am_hal_pwrctrl_low_power_init set up
     * (boot reached here on it); SIMOBUCK is only a buck-vs-LDO efficiency
     * upgrade, and the spotmgr temperature defaults to a safe value. A wrong
     * drop here would brown the chip out and fail loudly at boot. */

    /* De-SDK step 2a: dropped am_hal_clkmgr_board_info_set (clkmgr XTAL
     * bookkeeping -- tikuOS enables the 32 kHz crystal directly in the htimer,
     * not via the clkmgr) and the HFRC2 (250 MHz) config (nothing uses HFRC2).
     *
     * De-SDK step 2b (TEST): also drop am_hal_clkmgr_clock_config(HFRC) -- the
     * reset/SBL HFRC already free-runs near 48 MHz, so re-configuring it may be
     * redundant. The UART (HFRC/2 = 24 MHz tap) is the canary: clean UART means
     * the config was redundant; a garbled UART means the HFRC needs explicit
     * setup and this returns (bare-metal via CLKGEN). */
}

/**
 * @brief Read the true CPU core clock from the MCU performance-mode register
 *
 * Returns 96 MHz for Low-Power mode or ~250 MHz for High-Performance
 * ("turbo") mode. In HP the M55 runs from HFRC2, which free-runs at
 * approximately 250 MHz -- there is no HP frequency select on Apollo5; the
 * CMSIS enum comments saying "192MHz" are stale Apollo4-era text (the SDK's
 * own delay scaling and AM_HAL_CLKGEN_FREQ_HP250_HZ both use 250 MHz).
 *
 * @return Core clock frequency in Hz (96000000 or 250000000)
 */
static unsigned long tiku_ambiq_core_hz(void) {
    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS ==
            PWRCTRL_MCUPERFREQ_MCUPERFSTATUS_HP) {
        return 250000000UL;
    }
    return 96000000UL;
}

/**
 * @brief Initialize the Apollo510 CPU at boot
 *
 * Runs bare-metal SoC bring-up (caches, prefetch) then reads the true
 * core clock from the performance-mode register into s_core_hz. Called
 * once from main() before any kernel subsystem starts.
 */
void tiku_cpu_boot_ambiq_init(void) {
    tiku_ambiq_soc_init();          /* caches + prefetch; power/clocks from SBL */
    s_core_hz = tiku_ambiq_core_hz();
}

/*---------------------------------------------------------------------------*/
/* INFO1 FACTORY DATA ACCESS (shared by the HP port + `freq probe`)          */
/*---------------------------------------------------------------------------*/

/* INFO1 factory-data addresses. INFO1 lives either in OTP (0x42006000) or in
 * its MRAM shadow (0x42002000); MCUCTRL->SHADOWVALID bit3 (INFO1SELOTP) says
 * which copy is current. For word offsets >= 0x800 the MRAM copy sits at the
 * OTP offset + 0xA00 (per the SDK's INFO1_xlateOTPoffsetToMRAM). Offsets from
 * the SDK OTP INFO1 register map (am_mcu_apollo510_otpinfo1.h). */
#define AMBIQ_INFO1_OTP_BASE     0x42006000UL
#define AMBIQ_INFO1_MRAM_BASE    0x42002000UL
#define AMBIQ_INFO1_MRAM_SHIFT   0xA00UL       /* MRAM extra offset >= 0x800  */
#define AMBIQ_INFO1_PATCH_TRK0_O 0x840UL
#define AMBIQ_INFO1_TRIM_REV_O   0x910UL
#define AMBIQ_INFO1_PGM_INFO_O   0x930UL
#define AMBIQ_INFO1_PWRSTATE_O   0x970UL       /* POWERSTATE0..19 (20 words)  */

/** Read one INFO1 word by OTP offset from whichever copy is current. */
static uint32_t ambiq_info1_word(uint32_t otp_off, uint8_t in_otp) {
    uint32_t addr = in_otp
        ? (uint32_t)(AMBIQ_INFO1_OTP_BASE + otp_off)
        : (uint32_t)(AMBIQ_INFO1_MRAM_BASE + otp_off + AMBIQ_INFO1_MRAM_SHIFT);
    return *(volatile const uint32_t *)addr;
}

/*---------------------------------------------------------------------------*/
/* HIGH-PERFORMANCE (TURBO, ~250 MHz) MODE                                   */
/*---------------------------------------------------------------------------*/

/*
 * Apollo5 HP bring-up, bare-metal port of the AmbiqSuite "SPOT manager" slice
 * that applies to THIS OS's fixed operating point. HW-brought-up on a live
 * apollo510b EVB identified via `freq probe` as: revision B2, TRIM_REV 2
 * (PCM2.2 trim), TrimSubRev 0x5F, INFO1 OTP-resident. The port is a straight
 * transcription of the SDK paths that execute for exactly this configuration:
 *
 *   - CPU active, console UART always powered => the SPOT power-state matrix
 *     stays in the "CPU + peripherals" group: state 5 (LP) <-> state 13 (HP)
 *     at the room-temperature bucket (0..50 C). On PCM2.2 parts those two
 *     states differ ONLY in the VDDF buck reference trim (plus buck Ton
 *     timing) -- VDDC / core-LDO / VDDC_LV are identical -- which is what
 *     makes this narrow port tractable and safe.
 *   - GPU off always, so none of the GPU/PWRSW/ICACHE-gated sequences apply.
 *   - Temperature is never sensed: like the EVB BSP's AM_BSP_SET_ROOM_TEMPS,
 *     a one-time synthetic 25 C report pins the 0..50 C bucket (HP is refused
 *     by the SDK while the bucket is unknown, so this is a hard prerequisite).
 *
 * Every voltage value is a PER-CHIP factory trim read from INFO1 at runtime
 * (never hard-coded): the POWERSTATE table, the TrimSubRev-0x5F VDDF code
 * boost computed from the E/L TRIMCODE words, the buck Ton defaults, the
 * VDDC_LV adjust and the MEMLDO config. Anything unreadable or failing the
 * narrow-slice sanity checks refuses HP and stays in LP (fail-safe).
 *
 * HP hard-requires the SIMO buck (the SBL boots on LDOs), so the first HP
 * request also performs the SDK's SIMOBUCK_INIT with the PCM2.2 hooks. The
 * buck stays enabled after a return to LP (the SDK never disables it either).
 *
 * Deep sleep: tikuOS "deep" idle on Ambiq is a plain WFI (SCB SLEEPDEEP is
 * never set), so the SDK's HP-vs-deepsleep PWRSW handling does not apply.
 */

/* INFO1 words (OTP offsets) consumed by the HP slice, beyond the probe's. */
#define AMBIQ_INFO1_L_TRIMCODE_O   0x91CUL
#define AMBIQ_INFO1_E_TRIMCODE_O   0x920UL
#define AMBIQ_INFO1_DEFAULTTON_O   0x9CCUL
#define AMBIQ_INFO1_VDDCLVADJ_O    0x9D0UL
#define AMBIQ_INFO1_MEMLDOCFG_O    0x9E0UL

/* POWERSTATE word field decode (SDK am_hal_spotmgr_trim_settings_t). */
#define HP_PS_TVRGF(w)          ((w) & 0x7Fu)               /* VDDF buck ref  */
#define HP_PS_CORELDOACT(w)     (((w) >> 7)  & 0x3FFu)      /* core LDO act   */
#define HP_PS_CORELDOTEMPCO(w)  (((w) >> 17) & 0xFu)        /* core LDO tempco*/
#define HP_PS_TVRGC(w)          (((w) >> 21) & 0x7Fu)       /* VDDC buck ref  */

/* Cached per-chip HP plan, filled once from INFO1 on the first HP request. */
static struct {
    uint8_t  trims_ok;      /* INFO1 read + validated                        */
    uint8_t  temp_set;      /* synthetic 25 C bucket applied                 */
    uint8_t  tvrgf_lp;      /* state-5 VDDF trim, TrimSubRev boost applied   */
    uint8_t  tvrgf_hp;      /* state-13 VDDF trim, boost applied             */
    uint32_t ps7;           /* state-7 word (buck-enable voltage preload)    */
    uint8_t  tvrgf_ps7;     /* state-7 VDDF trim, boost applied              */
    uint32_t defaultton;    /* buck Ton defaults (HP->LP restore)            */
    uint32_t vddclvadj;     /* VDDC_LV per-bucket trims                      */
    uint32_t memldocfg;     /* MEMLDO trim + reference select                */
} s_hp;

extern void tiku_cpu_ambiq_delay_us(unsigned int us);   /* tiku_cpu_common.c */

static inline uint32_t hp_irq_save(void) {
    uint32_t pm;
    __asm__ volatile ("mrs %0, primask\n\tcpsid i" : "=r"(pm) :: "memory");
    return pm;
}
static inline void hp_irq_restore(uint32_t pm) {
    __asm__ volatile ("msr primask, %0" :: "r"(pm) : "memory");
}

/** Clamp a VDDF trim code to the SDK's [0x8, 0x7F] window. */
static uint8_t hp_tvrgf_clamp(uint32_t code) {
    if (code < 0x8u)  { return 0x8u; }
    if (code > 0x7Fu) { return 0x7Fu; }
    return (uint8_t)code;
}

/**
 * @brief Read + validate the per-chip HP trim plan from INFO1 (once).
 *
 * Computes the TrimSubRev-0x5F VDDF code boost exactly as the SDK does
 * (uint32 arithmetic + float rounding), then pins the state-5/13 trims.
 * Refuses (returns -1, HP stays declined) if any word is unprogrammed or the
 * narrow-slice assumption (states 5 and 13 differ only in VDDF) is violated.
 */
static int hp_trims_load(void) {
    uint8_t  in_otp, otp_was_on = 0u;
    uint32_t ps0, ps5, ps13, ps19, ltrim, etrim, pgm;
    uint32_t tmp1, tmp2, boost = 0u, spin;
    float    mv;

    if (s_hp.trims_ok) {
        return 0;
    }

    in_otp = (uint8_t)((MCUCTRL->SHADOWVALID >>
                        MCUCTRL_SHADOWVALID_INFO1SELOTP_Pos) & 1u);
    if (in_otp) {
        otp_was_on = (uint8_t)PWRCTRL->DEVPWRSTATUS_b.PWRSTOTP;
        if (!otp_was_on) {
            PWRCTRL->DEVPWREN_b.PWRENOTP = 1u;
            spin = 100000u;
            while (PWRCTRL->DEVPWRSTATUS_b.PWRSTOTP == 0u) {
                if (spin-- == 0u) { return -1; }
            }
        }
    }

    ps0   = ambiq_info1_word(AMBIQ_INFO1_PWRSTATE_O + 0u * 4u,  in_otp);
    ps5   = ambiq_info1_word(AMBIQ_INFO1_PWRSTATE_O + 5u * 4u,  in_otp);
    s_hp.ps7 = ambiq_info1_word(AMBIQ_INFO1_PWRSTATE_O + 7u * 4u, in_otp);
    ps13  = ambiq_info1_word(AMBIQ_INFO1_PWRSTATE_O + 13u * 4u, in_otp);
    ps19  = ambiq_info1_word(AMBIQ_INFO1_PWRSTATE_O + 19u * 4u, in_otp);
    ltrim = ambiq_info1_word(AMBIQ_INFO1_L_TRIMCODE_O, in_otp);
    etrim = ambiq_info1_word(AMBIQ_INFO1_E_TRIMCODE_O, in_otp);
    pgm   = ambiq_info1_word(AMBIQ_INFO1_PGM_INFO_O, in_otp);
    s_hp.defaultton = ambiq_info1_word(AMBIQ_INFO1_DEFAULTTON_O, in_otp);
    s_hp.vddclvadj  = ambiq_info1_word(AMBIQ_INFO1_VDDCLVADJ_O,  in_otp);
    s_hp.memldocfg  = ambiq_info1_word(AMBIQ_INFO1_MEMLDOCFG_O,  in_otp);

    if (in_otp && !otp_was_on) {
        PWRCTRL->DEVPWREN_b.PWRENOTP = 0u;
    }

    /* Unprogrammed INFO1 (erased or zero) => this chip cannot do HP safely. */
    if (ps5 == 0u || ps5 == 0xFFFFFFFFu || ps13 == 0u || ps13 == 0xFFFFFFFFu ||
        s_hp.ps7 == 0u || s_hp.ps7 == 0xFFFFFFFFu ||
        s_hp.defaultton == 0u || s_hp.defaultton == 0xFFFFFFFFu ||
        s_hp.memldocfg == 0xFFFFFFFFu) {
        return -1;
    }

    /* Narrow-slice sanity: this port only handles the PCM2.2 layout where the
     * LP<->HP transition moves VDDF alone. A part whose state 5/13 words also
     * differ in VDDC or core-LDO trims needs the full SDK sequence => refuse. */
    if (HP_PS_TVRGC(ps5)         != HP_PS_TVRGC(ps13) ||
        HP_PS_CORELDOACT(ps5)    != HP_PS_CORELDOACT(ps13) ||
        HP_PS_CORELDOTEMPCO(ps5) != HP_PS_CORELDOTEMPCO(ps13)) {
        return -1;
    }

    /* TrimSubRev 0x5F VDDF code boost, transcribed from the SDK pcm2_2 init:
     * uint32 sums (a negative difference wraps huge and zeroes the boost via
     * the mv<0 clamp), then float mV -> code conversion with +0.5 rounding.
     * STRICTLY gated on TrimSubRev 0x5F like the SDK: on any other part the
     * E/L TRIMCODE words may be unprogrammed, and running the formula on
     * zeroed words would yield the MAXIMUM boost -- an over-voltage. On a
     * 0x5F part with unprogrammed E/L words, refuse HP outright. */
    if ((pgm & 0xFFu) == 0x5Fu) {
        if (ltrim == 0u || ltrim == 0xFFFFFFFFu ||
            etrim == 0u || etrim == 0xFFFFFFFFu) {
            return -1;
        }
        tmp1 = (etrim & 0xFFFFu) + (etrim >> 16)
             - (ltrim & 0xFFFFu) - (ltrim >> 16);
        tmp2 = HP_PS_TVRGF(ps19) - HP_PS_TVRGF(ps0);
        mv = 220.0f - 0.5f * (float)tmp1;
        if (mv < 0.0f) {
            mv = 0.0f;
        }
        if (HP_PS_TVRGF(ps0) == 0u && tmp2 < 20u) {
            boost = (uint32_t)(mv * 20.0f / 45.0f + 0.5f);
        } else {
            boost = (uint32_t)(mv * 25.0f / 45.0f + 0.5f);
        }
    }

    s_hp.tvrgf_lp  = hp_tvrgf_clamp(HP_PS_TVRGF(ps5)      + boost);
    s_hp.tvrgf_hp  = hp_tvrgf_clamp(HP_PS_TVRGF(ps13)     + boost);
    s_hp.tvrgf_ps7 = hp_tvrgf_clamp(HP_PS_TVRGF(s_hp.ps7) + boost);
    if (s_hp.tvrgf_hp < s_hp.tvrgf_lp) {        /* HP must not LOWER VDDF */
        return -1;
    }
    s_hp.trims_ok = 1u;
    return 0;
}

/**
 * @brief Enable the SIMO buck from the SBL's LDO-only state (SDK SIMOBUCK_INIT
 *        with the PCM2.2 hooks inlined). Idempotent; returns 0 when ACT.
 */
static int hp_simobuck_enable(void) {
    uint32_t pm, spin;

    if (PWRCTRL->VRSTATUS_b.SIMOBUCKST == PWRCTRL_VRSTATUS_SIMOBUCKST_ACT) {
        return 0;
    }

    pm = hp_irq_save();

    /* pcm2_2_simobuck_init_bfr_ovr: preload the buck references to the
     * boot-default power state's (state 7) trims so it wakes at the right
     * voltages, and pin the VDDC_LV active-low Ton. */
    MCUCTRL->SIMOBUCK4_b.VDDCLVACTLOWTONTRIM = 4u;
    MCUCTRL->VREFGEN4_b.TVRGFVREFTRIM = s_hp.tvrgf_ps7;
    MCUCTRL->VREFGEN2_b.TVRGCVREFTRIM = HP_PS_TVRGC(s_hp.ps7);

    /* buck_ldo_override_init: force buck + both LDOs to active override.
     * The *OVER bit of each group is deliberately written LAST. */
    MCUCTRL->VRCTRL_b.SIMOBUCKPDNB   = 1u;
    MCUCTRL->VRCTRL_b.SIMOBUCKRSTB   = 1u;
    MCUCTRL->VRCTRL_b.SIMOBUCKACTIVE = 1u;
    MCUCTRL->VRCTRL_b.SIMOBUCKOVER   = 1u;

    MCUCTRL->VRCTRL_b.CORELDOCOLDSTARTEN = 0u;
    MCUCTRL->VRCTRL_b.CORELDOACTIVE      = 1u;
    MCUCTRL->VRCTRL_b.CORELDOACTIVEEARLY = 1u;
    MCUCTRL->VRCTRL_b.CORELDOPDNB        = 1u;
    MCUCTRL->VRCTRL_b.CORELDOOVER        = 1u;

    MCUCTRL->VRCTRL_b.MEMLDOCOLDSTARTEN = 0u;
    MCUCTRL->VRCTRL_b.MEMLDOACTIVE      = 1u;
    MCUCTRL->VRCTRL_b.MEMLDOACTIVEEARLY = 1u;
    MCUCTRL->VRCTRL_b.MEMLDOPDNB        = 1u;
    MCUCTRL->VRCTRL_b.MEMLDOOVER        = 1u;

    MCUCTRL->SIMOBUCK15_b.TRIMLATCHOVER = 1u;

    MCUCTRL->SIMOBUCK0_b.VDDCRXCOMPEN   = 1u;
    MCUCTRL->SIMOBUCK0_b.VDDFRXCOMPEN   = 1u;
    MCUCTRL->SIMOBUCK0_b.VDDSRXCOMPEN   = 1u;
    MCUCTRL->SIMOBUCK0_b.VDDCLVRXCOMPEN = 1u;

    /* The actual buck enable. */
    PWRCTRL->VRCTRL_b.SIMOBUCKEN = 1u;

    /* pcm2_2_simobuck_init_aft_enable: hand the load to the buck -- reduce the
     * core LDO to its parallel trim, re-reference the MEM LDO, then confirm
     * the buck reached ACT. */
    MCUCTRL->LDOREG1_b.CORELDOACTIVETRIM = HP_PS_CORELDOACT(s_hp.ps7);
    MCUCTRL->LDOREG1_b.CORELDOTEMPCOTRIM = HP_PS_CORELDOTEMPCO(s_hp.ps7);
    tiku_cpu_ambiq_delay_us(100u);
    MCUCTRL->LDOREG2_b.MEMLDOACTIVETRIM = (s_hp.memldocfg >> 2) & 0x3Fu;
    MCUCTRL->D2ASPARE_b.MEMLDOREF       = s_hp.memldocfg & 0x3u;
    tiku_cpu_ambiq_delay_us(100u);

    spin = 100000u;
    while (PWRCTRL->VRSTATUS_b.SIMOBUCKST != PWRCTRL_VRSTATUS_SIMOBUCKST_ACT) {
        if (spin-- == 0u) { break; }
    }

    hp_irq_restore(pm);
    return (PWRCTRL->VRSTATUS_b.SIMOBUCKST ==
            PWRCTRL_VRSTATUS_SIMOBUCKST_ACT) ? 0 : -1;
}

/**
 * @brief One-time synthetic room-temperature report (the EVB BSP's
 *        AM_BSP_SET_ROOM_TEMPS equivalent): pins the 0..50 C bucket the
 *        state-5/13 trims are valid for. SDK walk 7->5 reduces to loading the
 *        bucket's VDDC_LV trim and releasing the ANALDO active override.
 */
static void hp_temp_set_room(void) {
    if (s_hp.temp_set) {
        return;
    }
    MCUCTRL->VREFGEN3_b.TVRGCLVVREFTRIM = (s_hp.vddclvadj >> 7) & 0x7Fu;
    MCUCTRL->VRCTRL_b.ANALDOOVER = 0u;
    s_hp.temp_set = 1u;
}

/** @brief LP -> HP: SPOT SEQ_3 voltage work, then the perf-mode switch. */
static int hp_enter(void) {
    uint32_t pm, spin, boost;
    uint8_t  forced_hfrc2 = 0u;
    int      rc = 0;

    pm = hp_irq_save();

    /* Ton adjust for the HP ton state: active-low Ton rises to the factory
     * active-high values (read live -- they are per-chip trims). */
    MCUCTRL->SIMOBUCK2_b.VDDCACTLOWTONTRIM =
        MCUCTRL->SIMOBUCK2_b.VDDCACTHIGHTONTRIM;
    MCUCTRL->SIMOBUCK7_b.VDDFACTLOWTONTRIM =
        MCUCTRL->SIMOBUCK6_b.VDDFACTHIGHTONTRIM;
    MCUCTRL->SIMOBUCK4_b.VDDCLVACTLOWTONTRIM = 4u;

    /* VDDF double boost: overshoot by 2x the step to slew the rail fast,
     * settle 50 us, then land on the HP target. */
    boost = (uint32_t)s_hp.tvrgf_hp * 2u - (uint32_t)s_hp.tvrgf_lp;
    MCUCTRL->VREFGEN4_b.TVRGFVREFTRIM = hp_tvrgf_clamp(boost);
    tiku_cpu_ambiq_delay_us(50u);
    MCUCTRL->VREFGEN4_b.TVRGFVREFTRIM = s_hp.tvrgf_hp;

    /* HFRC2 (the ~250 MHz HP clock source) must be ready before the switch;
     * force it on if nothing else holds it and wait for READY. */
    if (CLKGEN->MISC_b.FRCHFRC2 == 0u) {
        CLKGEN->MISC_b.FRCHFRC2 = 1u;
        forced_hfrc2 = 1u;
        tiku_cpu_ambiq_delay_us(1u);
        spin = 100000u;
        while (CLKGEN->CLOCKENSTAT_b.HFRC2READY == 0u) {
            if (spin-- == 0u) { break; }
        }
    }

    if (CLKGEN->CLOCKENSTAT_b.HFRC2READY != 0u) {
        PWRCTRL->MCUPERFREQ_b.MCUPERFREQ = PWRCTRL_MCUPERFREQ_MCUPERFREQ_HP;
        spin = 100000u;
        while (PWRCTRL->MCUPERFREQ_b.MCUPERFACK == 0u) {
            if (spin-- == 0u) { break; }
        }
    }

    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS !=
            PWRCTRL_MCUPERFREQ_MCUPERFSTATUS_HP) {
        /* Switch failed: bring the voltages back down (SEQ_6) -- never leave
         * the HP VDDF applied at the LP frequency indefinitely. */
        MCUCTRL->VREFGEN4_b.TVRGFVREFTRIM = s_hp.tvrgf_lp;
        MCUCTRL->SIMOBUCK2_b.VDDCACTLOWTONTRIM = s_hp.defaultton & 0x1Fu;
        MCUCTRL->SIMOBUCK7_b.VDDFACTLOWTONTRIM = (s_hp.defaultton >> 10) & 0x1Fu;
        MCUCTRL->SIMOBUCK4_b.VDDCLVACTLOWTONTRIM = 4u;
        rc = -1;
    }

    /* Release the HFRC2 force; in HP the CPU itself keeps HFRC2 alive. */
    if (forced_hfrc2) {
        CLKGEN->MISC_b.FRCHFRC2 = 0u;
    }

    s_core_hz = tiku_ambiq_core_hz();
    hp_irq_restore(pm);
    return rc;
}

/** @brief HP -> LP: perf-mode switch FIRST, then the voltage drop (SEQ_6). */
static void hp_exit(void) {
    uint32_t pm, spin;

    pm = hp_irq_save();

    PWRCTRL->MCUPERFREQ_b.MCUPERFREQ = PWRCTRL_MCUPERFREQ_MCUPERFREQ_LP;
    spin = 100000u;
    while (PWRCTRL->MCUPERFREQ_b.MCUPERFACK == 0u) {
        if (spin-- == 0u) { break; }
    }

    /* Only lower VDDF once the core is confirmed back at 96 MHz. */
    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS ==
            PWRCTRL_MCUPERFREQ_MCUPERFSTATUS_LP) {
        MCUCTRL->VREFGEN4_b.TVRGFVREFTRIM = s_hp.tvrgf_lp;
        MCUCTRL->SIMOBUCK2_b.VDDCACTLOWTONTRIM = s_hp.defaultton & 0x1Fu;
        MCUCTRL->SIMOBUCK7_b.VDDFACTLOWTONTRIM = (s_hp.defaultton >> 10) & 0x1Fu;
        MCUCTRL->SIMOBUCK4_b.VDDCLVACTLOWTONTRIM = 4u;
    }

    s_core_hz = tiku_ambiq_core_hz();
    hp_irq_restore(pm);
}

/**
 * @brief Select the CPU operating frequency (perf mode).
 *
 * Apollo510 has Low-Power (96 MHz) and High-Performance "turbo" (~250 MHz,
 * HFRC2 free-run) modes. An HP request (`freq 250`) performs the full
 * bring-up on demand -- INFO1 trim load, SIMO buck enable, room-temperature
 * bucket pin, VDDF raise, perf switch -- each step idempotent and fail-safe:
 * any refusal leaves the core in LP at the LP voltages, and the shell reports
 * "not applied". A subsequent `freq 96` drops back to LP (voltage lowered
 * only after the frequency). The OS tick lives on the STIMER and the busy
 * delay re-reads the live core clock, so timekeeping is undisturbed.
 *
 * @param cpu_freq  Requested core frequency in MHz.
 */
void tiku_cpu_freq_ambiq_init(unsigned int cpu_freq) {
    uint32_t spin;

    if (cpu_freq > 96u) {
        if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS ==
                PWRCTRL_MCUPERFREQ_MCUPERFSTATUS_HP) {
            s_core_hz = tiku_ambiq_core_hz();   /* already in HP */
            return;
        }
        if (hp_trims_load() != 0) {
            return;                             /* trims unusable: stay LP */
        }
        if (hp_simobuck_enable() != 0) {
            return;                             /* buck never reached ACT  */
        }
        hp_temp_set_room();
        (void)hp_enter();                       /* reports via s_core_hz   */
        return;
    }

    /* LP request: drop out of HP if we are in it (frequency before voltage),
     * else ensure Low-Power mode (the SBL default, so usually a no-op). */
    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS ==
            PWRCTRL_MCUPERFREQ_MCUPERFSTATUS_HP) {
        if (s_hp.trims_ok) {
            hp_exit();
        }
        return;
    }
    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS != PWRCTRL_MCUPERFREQ_MCUPERFSTATUS_LP) {
        PWRCTRL->MCUPERFREQ_b.MCUPERFREQ = PWRCTRL_MCUPERFREQ_MCUPERFREQ_LP;
        spin = 100000u;
        while (PWRCTRL->MCUPERFREQ_b.MCUPERFACK == 0u) {
            if (spin-- == 0u) break;
        }
    }
    s_core_hz = tiku_ambiq_core_hz();
}

/**
 * @brief Enter CPU idle using the WFI (Wait For Interrupt) instruction
 *
 * Suspends the core until the next interrupt fires. Used by the kernel
 * scheduler when no process is ready to run.
 */
void tiku_cpu_boot_ambiq_power_wfi_enter(void) {
    __asm__ volatile ("wfi");
}

/*---------------------------------------------------------------------------*/
/* HP-TURBO IDENTITY PROBE (`freq probe`)                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Measure the live SysTick tick rate against the 32.768 kHz STIMER.
 *
 * Counts SysTick decrements (wrap-aware) across a 4096-STIMER-tick window
 * (125 ms) and returns the measured SysTick frequency in Hz. SysTick runs
 * with CLKSOURCE = processor, so this IS the core clock -- the one number
 * that proves an LP<->HP switch actually changed the CPU speed (the STIMER
 * crystal timebase is independent of the core clock). Blocks for 125 ms.
 */
unsigned long tiku_cpu_freq_ambiq_measured_hz(void) {
    volatile uint32_t *cvr = (volatile uint32_t *)0xE000E018UL; /* SYST_CVR  */
    volatile uint32_t *rvr = (volatile uint32_t *)0xE000E014UL; /* SYST_RVR  */
    uint32_t reload = (*rvr & 0x00FFFFFFu) + 1u;
    uint32_t st0, last, now, step;
    uint64_t count = 0u;

    if (reload <= 1u) {
        return 0u;                       /* SysTick not configured */
    }

    st0  = STIMER->STTMR;
    last = *cvr & 0x00FFFFFFu;
    while ((uint32_t)(STIMER->STTMR - st0) < 4096u) {
        now  = *cvr & 0x00FFFFFFu;       /* down-counter, wraps to reload-1 */
        step = (now <= last) ? (last - now) : (last + reload - now);
        count += step;
        last = now;
    }
    return (unsigned long)((count * 32768u) / 4096u);
}

void tiku_cpu_freq_ambiq_hp_probe(tiku_ambiq_hp_probe_t *out) {
    uint8_t  otp_was_on = 0u;
    uint32_t i, spin;

    if (out == (tiku_ambiq_hp_probe_t *)0) {
        return;
    }

    out->chiprev      = MCUCTRL->CHIPREV;
    out->shadowvalid  = MCUCTRL->SHADOWVALID;
    out->vrstatus     = PWRCTRL->VRSTATUS;
    out->mcuperfreq   = PWRCTRL->MCUPERFREQ;
    out->devpwrstatus = PWRCTRL->DEVPWRSTATUS;
    out->info1_in_otp = (uint8_t)((out->shadowvalid >>
                                   MCUCTRL_SHADOWVALID_INFO1SELOTP_Pos) & 1u);
    out->info1_ok     = 1u;

    /* INFO1 in OTP: the OTP block must be powered to read it. Power it on for
     * the read and restore the previous state after (never leave it changed). */
    if (out->info1_in_otp) {
        otp_was_on = (uint8_t)((out->devpwrstatus >>
                                PWRCTRL_DEVPWRSTATUS_PWRSTOTP_Pos) & 1u);
        if (!otp_was_on) {
            PWRCTRL->DEVPWREN_b.PWRENOTP = 1u;
            spin = 100000u;
            while (PWRCTRL->DEVPWRSTATUS_b.PWRSTOTP == 0u) {
                if (spin-- == 0u) { out->info1_ok = 0u; break; }
            }
        }
    }

    if (out->info1_ok) {
        out->patch_tracker0 = ambiq_info1_word(AMBIQ_INFO1_PATCH_TRK0_O,
                                               out->info1_in_otp);
        out->trim_rev       = ambiq_info1_word(AMBIQ_INFO1_TRIM_REV_O,
                                               out->info1_in_otp);
        out->pgm_info       = ambiq_info1_word(AMBIQ_INFO1_PGM_INFO_O,
                                               out->info1_in_otp);
        for (i = 0u; i < 20u; i++) {
            out->powerstate[i] = ambiq_info1_word(
                AMBIQ_INFO1_PWRSTATE_O + (i * 4u), out->info1_in_otp);
        }
    } else {
        out->patch_tracker0 = 0xFFFFFFFFu;
        out->trim_rev       = 0xFFFFFFFFu;
        out->pgm_info       = 0xFFFFFFFFu;
        for (i = 0u; i < 20u; i++) { out->powerstate[i] = 0xFFFFFFFFu; }
    }

    if (out->info1_in_otp && !otp_was_on) {
        PWRCTRL->DEVPWREN_b.PWRENOTP = 0u;      /* restore OTP power state */
    }
}

/**
 * @brief Return the main CPU core clock frequency
 *
 * @return Core frequency in Hz as captured at boot from the perf-mode
 *         register (96 MHz LP or 192 MHz HP)
 */
unsigned long tiku_cpu_ambiq_clock_get_hz(void) { return s_core_hz; }

/**
 * @brief Return the SMCLK-equivalent sub-module clock frequency
 *
 * On Apollo510 there is no dedicated SMCLK; this returns the same value
 * as the core clock for callers that query the peripheral reference.
 *
 * @return Core frequency in Hz
 */
unsigned long tiku_cpu_ambiq_smclk_get_hz(void) { return s_core_hz; }

/**
 * @brief Return the ACLK-equivalent auxiliary/low-frequency clock
 *
 * Maps to the 32.768 kHz crystal used by the real-time clock and the
 * htimer STIMER counter.
 *
 * @return 32768 Hz
 */
unsigned long tiku_cpu_ambiq_aclk_get_hz(void)  { return 32768UL; }

/**
 * @brief Report whether the main clock has a fault
 *
 * Always returns 0 on Apollo510 (no oscillator-fault detection is
 * implemented yet).
 *
 * @return 0 (no fault)
 */
int           tiku_cpu_ambiq_clock_has_fault(void) { return 0; }

/**
 * @brief Apollo510 (M55) data-cache maintenance (routed from tiku_cpu_dcache_*).
 *
 * The M55 has architectural L1 I/D caches (enabled in soc_init), so coherency
 * with out-of-band MRAM writes needs by-address SCB ops: clean the staging
 * buffer before the bootrom reads it, invalidate the programmed page after.
 */
void tiku_cpu_ambiq_dcache_clean(const void *addr, unsigned long len) {
    SCB_CleanDCache_by_Addr((void *)(uintptr_t)addr, (int32_t)len);
}

void tiku_cpu_ambiq_dcache_invalidate(const void *addr, unsigned long len) {
    SCB_InvalidateDCache_by_Addr((void *)(uintptr_t)addr, (int32_t)len);
}
