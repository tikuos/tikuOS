/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - Apollo510 CPU/SoC bring-up and clocks.
 *
 * Boot enables the M55 caches and prefetch unit and powers down blocks the
 * secure bootloader leaves on.  The core runs in low-power (96 MHz) or
 * high-performance (~250 MHz) mode; HP needs the SIMO buck and INFO1 trims.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "apollo510.h"       /* CMSIS: PWRCTRL, CLKGEN, MCUCTRL, MEMSYSCTL */
#include "tiku_cpu_freq_boot_arch.h"

/*
 * The core clock is 96 MHz in low-power mode (the SBL default) and ~250 MHz
 * in high-performance mode; there is no other HP frequency, and the CMSIS
 * MCUPERFREQ comments that say 192 MHz do not apply to this part.  SysTick
 * counts the core clock (CLKSOURCE = processor).  The HFRC the SBL leaves
 * free-running near 48 MHz is a peripheral reference, not the core clock.
 */
/** @brief Core clock in Hz, read from the perf-mode status on each change. */
static unsigned long s_core_hz = 96000000UL;

/**
 * @brief Enable the M55 I/D caches and the prefetch unit.
 *
 * Power rails and the clock tree stay as the secure bootloader left them;
 * the HFRC free-runs near 48 MHz and the console UART uses its 24 MHz tap.
 */
static void tiku_ambiq_soc_init(void) {
    /* Prefetch-unit settings are the Apollo RevB defaults: MAX_OS=6,
     * MAX_LA=6, MIN_LA=4. */
    SCB_EnableICache();
    MEMSYSCTL->PFCR = (6u << MEMSYSCTL_PFCR_MAX_OS_Pos) |
                      (6u << MEMSYSCTL_PFCR_MAX_LA_Pos) |
                      (4u << MEMSYSCTL_PFCR_MIN_LA_Pos) |
                      (1u << MEMSYSCTL_PFCR_ENABLE_Pos);
    SCB_EnableDCache();
    SCB_CleanDCache();
}

/**
 * @brief Read the core clock from the perf-mode status (PWRCTRL.MCUPERFREQ).
 *
 * @return 96000000 in low-power mode, 250000000 in high-performance mode,
 *         where the core runs from HFRC2
 */
static unsigned long tiku_ambiq_core_hz(void) {
    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS ==
            PWRCTRL_MCUPERFREQ_MCUPERFSTATUS_HP) {
        return 250000000UL;
    }
    return 96000000UL;
}

/*
 * The boot tidy powers down blocks the SBL leaves on and an idle image does
 * not use: the CryptoCell domain, the OTP reader and the M55 trace unit.
 * Each user powers its block up itself:
 *   - OTP:    hp_trims_load() and tiku_cpu_freq_ambiq_hp_probe() raise
 *             PWRENOTP around their INFO1 reads and restore it after.
 *   - crypto: tiku_trng_arch_init() raises PWRENCRYPTO and waits PWRSTCRYPTO.
 *   - trace:  every DWT user sets DEMCR.TRCENA before reading CYCCNT.
 * The boot ROM and the upper MRAM bank stay powered: MRAM writes go through
 * the boot ROM, so powering them down faults the next NVM write.
 * TIKU_AMBIQ_BOOT_TIDY=0 leaves all three blocks on.
 */
#ifndef TIKU_AMBIQ_BOOT_TIDY
#define TIKU_AMBIQ_BOOT_TIDY 1
#endif

/**
 * @brief Power down the trace unit, CryptoCell and OTP reader; does nothing
 *        when TIKU_AMBIQ_BOOT_TIDY is 0.
 */
static void tiku_ambiq_boot_tidy(void) {
#if (TIKU_AMBIQ_BOOT_TIDY + 0)
    CoreDebug->DEMCR &= ~CoreDebug_DEMCR_TRCENA_Msk;   /* trace unit */
    PWRCTRL->DEVPWREN_b.PWRENCRYPTO = 0u;              /* CryptoCell */
    PWRCTRL->DEVPWREN_b.PWRENOTP    = 0u;              /* OTP reader */
    __DSB();
#endif
}

/**
 * @brief Boot-time CPU setup: caches and prefetch, the boot tidy, then the
 *        core clock from the perf-mode status.
 */
void tiku_cpu_boot_ambiq_init(void) {
    tiku_ambiq_soc_init();          /* caches + prefetch */
    tiku_ambiq_boot_tidy();         /* release the SBL's unused blocks */
    s_core_hz = tiku_ambiq_core_hz();
}

/*---------------------------------------------------------------------------*/
/* INFO1 FACTORY DATA                                                        */
/*---------------------------------------------------------------------------*/

/* INFO1 factory-data addresses, read by the HP sequence and `freq probe`.
 * INFO1 lives either in OTP (0x42006000) or in its MRAM shadow (0x42002000);
 * MCUCTRL->SHADOWVALID bit3 (INFO1SELOTP) says which copy is current.  For
 * word offsets >= 0x800, as all of these are, the MRAM copy sits at the OTP
 * offset + 0xA00 (the SDK's INFO1_xlateOTPoffsetToMRAM).  Offsets are from
 * the SDK OTP INFO1 register map (am_mcu_apollo510_otpinfo1.h). */
#define AMBIQ_INFO1_OTP_BASE     0x42006000UL
#define AMBIQ_INFO1_MRAM_BASE    0x42002000UL
#define AMBIQ_INFO1_MRAM_SHIFT   0xA00UL       /* MRAM extra offset >= 0x800  */
#define AMBIQ_INFO1_PATCH_TRK0_O 0x840UL
#define AMBIQ_INFO1_TRIM_REV_O   0x910UL
#define AMBIQ_INFO1_PGM_INFO_O   0x930UL
#define AMBIQ_INFO1_PWRSTATE_O   0x970UL       /* POWERSTATE0..19 (20 words)  */

/** @brief Read one INFO1 word by OTP offset from the copy @p in_otp names. */
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
 * High-performance mode follows the AmbiqSuite SPOT-manager sequence for one
 * operating point, on B2 silicon with PCM2.2 trims (TRIM_REV >= 2) or newer.
 * It assumes:
 *   - the CPU active and the console UART powered, so the SPOT power state
 *     moves between 5 (LP) and 13 (HP) in the 0..50 C bucket.  On PCM2.2
 *     parts those states differ only in the VDDF buck reference trim and the
 *     buck Ton timing; VDDC, the core LDO and VDDC_LV are identical;
 *   - the GPU off, so no GPU, PWRSW or ICACHE-gated step runs;
 *   - room temperature: no temperature is measured.  As the EVB BSP's
 *     AM_BSP_SET_ROOM_TEMPS does, a one-time synthetic 25 C report pins the
 *     0..50 C bucket (hp_temp_set_room()).
 *
 * Every voltage is a per-chip factory trim read from INFO1 at run time: the
 * POWERSTATE table, the TrimSubRev-0x5F VDDF boost from the E/L TRIMCODE
 * words, the buck Ton defaults, the VDDC_LV adjust and the MEMLDO config.  A
 * word that is unreadable or fails the checks refuses HP, leaving LP.
 *
 * HP needs the SIMO buck, so the first HP request enables it (the SDK's
 * SIMOBUCK_INIT with the PCM2.2 hooks); it stays on after a return to LP.
 * The kernel's idle is a plain WFI without SLEEPDEEP, and this file does
 * none of the SDK's HP-versus-deep-sleep PWRSW handling.  The power probe's
 * deep-sleep window (tiku_power_ambiq.c) sets SLEEPDEEP in LP and HP alike.
 */

/*
 * The regulator an LP (96 MHz) image runs on: 1 = SIMO buck, 0 = the LDOs the
 * SBL hands over on.  The LDOs drop 1.8 V to the core rails linearly, while
 * the buck converts and draws less current.  With TIKU_AMBIQ_ELP_STATE=1 the
 * buck draws slightly more idle current than the LDOs, though less under
 * load.  Set 0 for a board with no SIMO inductor.  An HP request enables the
 * buck whatever this says, and nothing returns to the LDOs before a reboot.
 */
#ifndef TIKU_AMBIQ_LP_BUCK
#define TIKU_AMBIQ_LP_BUCK 1
#endif

/* INFO1 words (OTP offsets) the HP sequence reads, beyond the probe's. */
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

/* Per-chip HP plan, filled once from INFO1 by hp_trims_load(). */
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
    /* Diagnostics for `freq probe`; the transition never reads them. */
    uint32_t dx_ltrim;      /* raw INFO1 L_TRIMCODE                          */
    uint32_t dx_etrim;      /* raw INFO1 E_TRIMCODE                          */
    uint32_t dx_mv_x10;     /* the formula's mV boost, x10                   */
    uint32_t dx_boost;      /* boost in trim codes, pre-clamp                */
    uint8_t  dx_ps5_raw;    /* TVRGF(state 5) before the boost               */
    uint8_t  dx_ps13_raw;   /* TVRGF(state 13) before the boost              */
    uint8_t  dx_clamped;    /* 1 = the clamp cut the LP or HP trim           */
} s_hp;

extern void tiku_cpu_ambiq_delay_us(unsigned int us);   /* tiku_cpu_common.c */

/** @brief Mask IRQs and return the previous PRIMASK. */
static inline uint32_t hp_irq_save(void) {
    uint32_t pm;
    __asm__ volatile ("mrs %0, primask\n\tcpsid i" : "=r"(pm) :: "memory");
    return pm;
}
/** @brief Restore the PRIMASK that hp_irq_save() returned. */
static inline void hp_irq_restore(uint32_t pm) {
    __asm__ volatile ("msr primask, %0" :: "r"(pm) : "memory");
}

/** @brief Clamp a VDDF trim code to the SDK's [0x8, 0x7F] window. */
static uint8_t hp_tvrgf_clamp(uint32_t code) {
    if (code < 0x8u)  { return 0x8u; }
    if (code > 0x7Fu) { return 0x7Fu; }
    return (uint8_t)code;
}

/**
 * @brief Read and check the per-chip HP trim plan from INFO1, once.
 *
 * Computes the TrimSubRev-0x5F VDDF boost as the SDK does (uint32 sums, float
 * rounding) and derives the state-5, 7 and 13 VDDF trims.
 *
 * @return 0 when the plan is usable; -1 for older silicon or trims, an OTP
 *         that does not power up, an unprogrammed word, states 5 and 13 that
 *         differ beyond VDDF, or an HP trim below the LP one
 */
static int hp_trims_load(void) {
    uint8_t  in_otp, otp_was_on = 0u;
    uint32_t ps0, ps5, ps13, ps19, ltrim, etrim, pgm, trimrev, rev;
    uint32_t tmp1, tmp2, boost = 0u, spin;
    /* mv stays 0 (no boost) unless the TrimSubRev-0x5F path sets it; the
     * diagnostic below records it either way. */
    float    mv = 0.0f;

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
                if (spin-- == 0u) {
                    PWRCTRL->DEVPWREN_b.PWRENOTP = 0u;
                    return -1;
                }
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
    trimrev = ambiq_info1_word(AMBIQ_INFO1_TRIM_REV_O, in_otp);
    s_hp.defaultton = ambiq_info1_word(AMBIQ_INFO1_DEFAULTTON_O, in_otp);
    s_hp.vddclvadj  = ambiq_info1_word(AMBIQ_INFO1_VDDCLVADJ_O,  in_otp);
    s_hp.memldocfg  = ambiq_info1_word(AMBIQ_INFO1_MEMLDOCFG_O,  in_otp);

    if (in_otp && !otp_was_on) {
        PWRCTRL->DEVPWREN_b.PWRENOTP = 0u;
    }

    /* Trim-scheme gate, as in the SDK's spotmgr dispatch
     * (g_bIsPCM2p2OrNewer): this is the PCM2.2 sequence, for B2 silicon with
     * TRIM_REV >= 2 or silicon newer than B2.  Older parts (B1, or B2 with
     * PCM2.0-2.1 trims) need a different sequence, so they get no HP. */
    rev = MCUCTRL->CHIPREV & 0xFFu;     /* [7:4] REVMAJ (2='B'), [3:0] REVMIN */
    if (!((rev == 0x23u && trimrev >= 2u && trimrev != 0xFFFFFFFFu) ||
          (rev > 0x23u && rev < 0xF0u))) {
        return -1;
    }

    /* An erased or zero INFO1 word refuses HP. */
    if (ps5 == 0u || ps5 == 0xFFFFFFFFu || ps13 == 0u || ps13 == 0xFFFFFFFFu ||
        s_hp.ps7 == 0u || s_hp.ps7 == 0xFFFFFFFFu ||
        s_hp.defaultton == 0u || s_hp.defaultton == 0xFFFFFFFFu ||
        s_hp.memldocfg == 0xFFFFFFFFu) {
        return -1;
    }

    /* This sequence moves VDDF alone between states 5 and 13.  A part whose
     * state 5/13 words also differ in VDDC or core-LDO trims needs the full
     * SDK sequence, so HP is refused. */
    if (HP_PS_TVRGC(ps5)         != HP_PS_TVRGC(ps13) ||
        HP_PS_CORELDOACT(ps5)    != HP_PS_CORELDOACT(ps13) ||
        HP_PS_CORELDOTEMPCO(ps5) != HP_PS_CORELDOTEMPCO(ps13)) {
        return -1;
    }

    /* TrimSubRev-0x5F VDDF boost, from the SDK's pcm2_2 init: uint32 sums (a
     * negative difference wraps, and the mv < 0 clamp zeroes the boost), then
     * mV to trim codes with +0.5 rounding.  Only TrimSubRev 0x5F parts run
     * it: elsewhere the E/L TRIMCODE words may be unprogrammed, and the
     * formula on zero words gives the largest boost, an over-voltage.  A 0x5F
     * part with unprogrammed E/L words refuses HP. */
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

    /* Record the boost computation for `freq probe`; nothing reads these
     * back to decide anything. */
    s_hp.dx_ltrim      = ltrim;
    s_hp.dx_etrim      = etrim;
    s_hp.dx_mv_x10     = (uint32_t)(mv * 10.0f + 0.5f);
    s_hp.dx_boost      = boost;
    s_hp.dx_ps5_raw    = (uint8_t)HP_PS_TVRGF(ps5);
    s_hp.dx_ps13_raw   = (uint8_t)HP_PS_TVRGF(ps13);

    s_hp.tvrgf_lp  = hp_tvrgf_clamp(HP_PS_TVRGF(ps5)      + boost);
    s_hp.tvrgf_hp  = hp_tvrgf_clamp(HP_PS_TVRGF(ps13)     + boost);
    s_hp.tvrgf_ps7 = hp_tvrgf_clamp(HP_PS_TVRGF(s_hp.ps7) + boost);
    /* Note whether the [0x8, 0x7F] clamp cut the LP or HP trim, meaning the
     * boost asked for more than the trim field can hold. */
    s_hp.dx_clamped = ((HP_PS_TVRGF(ps5)  + boost) > 0x7Fu ||
                       (HP_PS_TVRGF(ps13) + boost) > 0x7Fu) ? 1u : 0u;
    if (s_hp.tvrgf_hp < s_hp.tvrgf_lp) {        /* HP must not lower VDDF */
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

    /* buck_ldo_override_init: force the buck and both LDOs to active
     * override.  The *OVER bit of each group is written last. */
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

    /* Enable the buck. */
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
 *        state-5/13 trims are valid for.
 *
 * Loads that bucket's VDDC_LV trim from the INFO1 VDDC_LV adjust word and
 * releases the ANALDO active override.
 */
static void hp_temp_set_room(void) {
    if (s_hp.temp_set) {
        return;
    }
    MCUCTRL->VREFGEN3_b.TVRGCLVVREFTRIM = (s_hp.vddclvadj >> 7) & 0x7Fu;
    MCUCTRL->VRCTRL_b.ANALDOOVER = 0u;
    s_hp.temp_set = 1u;
}

/**
 * @brief LP -> HP: raise the buck Ton trims and VDDF to the HP values, then
 *        switch the perf mode.
 *
 * @return 0, or -1 when the switch failed and the LP voltages were restored
 */
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

    /* VDDF double boost: the trim moves twice the LP-to-HP step, one step
     * past the HP target, to slew the rail fast; after 50 us it is set to
     * the HP target. */
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
        /* Switch failed: restore the LP voltages (SEQ_6), so the HP VDDF
         * does not stay applied at the LP frequency. */
        MCUCTRL->VREFGEN4_b.TVRGFVREFTRIM = s_hp.tvrgf_lp;
        MCUCTRL->SIMOBUCK2_b.VDDCACTLOWTONTRIM = s_hp.defaultton & 0x1Fu;
        MCUCTRL->SIMOBUCK7_b.VDDFACTLOWTONTRIM = (s_hp.defaultton >> 10) & 0x1Fu;
        MCUCTRL->SIMOBUCK4_b.VDDCLVACTLOWTONTRIM = 4u;
        rc = -1;
    }

    /* Release the HFRC2 force; in HP the CPU itself keeps HFRC2 running. */
    if (forced_hfrc2) {
        CLKGEN->MISC_b.FRCHFRC2 = 0u;
    }

    s_core_hz = tiku_ambiq_core_hz();
    hp_irq_restore(pm);
    return rc;
}

/** @brief HP -> LP: perf-mode switch first, then the voltage drop (SEQ_6). */
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
 * @brief Select the CPU perf mode: LP (96 MHz) or HP (~250 MHz, HFRC2).
 *
 * A request above 96 MHz runs the HP bring-up: INFO1 trim load, buck enable,
 * temperature-bucket pin, VDDF raise, perf switch.  A lower request leaves
 * HP, frequency before voltage, or else enables the buck (TIKU_AMBIQ_LP_BUCK).
 *
 * @note A refused step leaves the core in LP at LP voltages; s_core_hz shows
 *       the result.  The kernel tick runs from STIMER, so it does not change.
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

    /* LP request: drop out of HP when in it (frequency before voltage), else
     * ensure Low-Power mode (the SBL default, so usually a no-op). */
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

#if (TIKU_AMBIQ_LP_BUCK + 0)
    /* Hand the load to the SIMO buck at 96 MHz.  On failure the core stays
     * on the LDOs, the SBL's own working state. */
    (void)tiku_cpu_freq_ambiq_simobuck_enable();
#endif

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
 * @brief Measure the core clock against the 32.768 kHz STIMER.
 *
 * Counts SysTick decrements (wrap-aware) over 4096 STIMER ticks, 125 ms.
 * SysTick runs at the core clock (CLKSOURCE = processor).
 *
 * @return Measured core clock in Hz, or 0 if SysTick is not configured
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

/**
 * @brief Enable the SIMO buck without changing the perf mode.
 *
 * Called by tiku_cpu_freq_ambiq_init() at 96 MHz with TIKU_AMBIQ_LP_BUCK set,
 * and by the `power buck` command.
 *
 * @note Loads the factory trims first (the buck sequence needs them); the
 *       frequency is untouched.
 * @return 0 when the buck reports ACT, negative if trims are unusable or it
 *         never reached ACT.
 */
int tiku_cpu_freq_ambiq_simobuck_enable(void) {
    if (PWRCTRL->VRSTATUS_b.SIMOBUCKST == PWRCTRL_VRSTATUS_SIMOBUCKST_ACT) {
        return 0;                                  /* idempotent */
    }
    if (!s_hp.trims_ok && hp_trims_load() != 0) {
        return -1;                                 /* trims unusable */
    }
    return hp_simobuck_enable();
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

    /* INFO1 in OTP: the OTP block must be powered to read it, so power it
     * on for the read and restore its previous state after. */
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

    /* The VDDF plan and the trim the hardware runs.  hp_trims_load() builds
     * the plan on the first HP request or buck enable; until a load
     * succeeds, vddf_plan_ok is 0. */
    out->vddf_applied  = (uint8_t)MCUCTRL->VREFGEN4_b.TVRGFVREFTRIM;
    out->vddf_plan_ok  = s_hp.trims_ok;
    out->vddf_ltrim    = s_hp.dx_ltrim;
    out->vddf_etrim    = s_hp.dx_etrim;
    out->vddf_mv_x10   = s_hp.dx_mv_x10;
    out->vddf_boost_codes = (uint8_t)s_hp.dx_boost;
    out->vddf_ps5_raw  = s_hp.dx_ps5_raw;
    out->vddf_ps13_raw = s_hp.dx_ps13_raw;
    out->vddf_lp       = s_hp.tvrgf_lp;
    out->vddf_hp       = s_hp.tvrgf_hp;
    out->vddf_clamped  = s_hp.dx_clamped;

    /* Raw regulator words for the LP-vs-HP diff. */
    out->r_vrefgen2 = MCUCTRL->VREFGEN2;
    out->r_vrefgen3 = MCUCTRL->VREFGEN3;
    out->r_vrefgen4 = MCUCTRL->VREFGEN4;
    out->r_ldoreg1  = MCUCTRL->LDOREG1;
    out->r_ldoreg2  = MCUCTRL->LDOREG2;
    out->r_vrctrl   = MCUCTRL->VRCTRL;
    out->r_d2aspare = MCUCTRL->D2ASPARE;
    out->r_sb[0]    = MCUCTRL->SIMOBUCK0;
    out->r_sb[1]    = MCUCTRL->SIMOBUCK2;
    out->r_sb[2]    = MCUCTRL->SIMOBUCK4;
    out->r_sb[3]    = MCUCTRL->SIMOBUCK6;
    out->r_sb[4]    = MCUCTRL->SIMOBUCK7;
    out->r_sb[5]    = MCUCTRL->SIMOBUCK15;

    if (out->info1_in_otp && !otp_was_on) {
        PWRCTRL->DEVPWREN_b.PWRENOTP = 0u;      /* restore OTP power state */
    }
}

/**
 * @brief Return the main CPU core clock frequency
 *
 * @return Core frequency in Hz as read after the last perf-mode change
 *         (96 MHz LP or 250 MHz HP)
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
 * The 32.768 kHz crystal that clocks the STIMER counter.
 *
 * @return 32768 Hz
 */
unsigned long tiku_cpu_ambiq_aclk_get_hz(void)  { return 32768UL; }

/**
 * @brief Report whether the main clock has a fault
 *
 * This port does no oscillator-fault detection.
 *
 * @return 0 (no fault)
 */
int           tiku_cpu_ambiq_clock_has_fault(void) { return 0; }

/**
 * @brief Apollo510 (M55) data-cache maintenance, for tiku_cpu_dcache_*().
 *
 * The M55's L1 caches are enabled in soc_init, so MRAM programming needs the
 * by-address SCB ops: clean the staging buffer before the boot ROM reads it,
 * invalidate the programmed range after.
 */
void tiku_cpu_ambiq_dcache_clean(const void *addr, unsigned long len) {
    SCB_CleanDCache_by_Addr((void *)(uintptr_t)addr, (int32_t)len);
}

void tiku_cpu_ambiq_dcache_invalidate(const void *addr, unsigned long len) {
    SCB_InvalidateDCache_by_Addr((void *)(uintptr_t)addr, (int32_t)len);
}

/**
 * @brief Apollo510 (M55) full instruction-cache invalidate.
 *
 * Needed after code is written behind the CPU, as the BASIC module loader
 * does, and before it runs.  The leading DSB/ISB orders the writes; the
 * trailing pair discards instructions already prefetched.
 */
void tiku_cpu_ambiq_icache_invalidate(void) {
    __DSB();
    __ISB();
    SCB->ICIALLU = 0UL;
    __DSB();
    __ISB();
}
