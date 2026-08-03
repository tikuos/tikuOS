/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_otp_tool.c - one-shot provisioning: the VDDIO3_HSLV fuse.
 *
 * OTP is permanent, so this file compiles only under TIKU_N6_OTP_TOOL=1,
 * touches exactly one bit of one word, and refuses anything unexpected.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if defined(TIKU_N6_OTP_TOOL)

#include <stdint.h>

#include "tiku_stm32n6_regs.h"
#include "tiku_uart_arch.h"

/* The boot ROM decides the VDDIO3 pad range from this fuse. The rail on this
 * board is fixed 1.8 V, so without it the ROM probes the boot flash with
 * 3.3 V-range input thresholds and hears nothing; the register sequence
 * mirrors ST's HAL_BSEC_OTP_Read/Program exactly. */
#define OTP_FUSE_ID         124U
#define OTP_BIT_HSLV_VDDIO3 (1UL << 15)

#define BSEC_BASE           0x46009000UL
#define BSEC_FVR(w)         (BSEC_BASE + 0x000U + ((w) * 4U))
#define BSEC_SPLOCK(r)      (BSEC_BASE + 0x800U + ((r) * 4U))
#define BSEC_SRLOCK(r)      (BSEC_BASE + 0x880U + ((r) * 4U))
#define BSEC_OTPCR          (BSEC_BASE + 0xC04U)
#define BSEC_WDR            (BSEC_BASE + 0xC08U)
#define BSEC_OTPSR          (BSEC_BASE + 0xE44U)

#define OTPCR_ADDR_MSK      0x1FFUL
#define OTPCR_PROG          (1UL << 13)
#define OTPCR_PPLOCK        (1UL << 14)

#define OTPSR_BUSY          (1UL << 0)
#define OTPSR_PROGFAIL      (1UL << 16)
#define OTPSR_DISTURBF      (1UL << 17)
#define OTPSR_DEDF          (1UL << 18)
#define OTPSR_AMEF          (1UL << 22)
#define OTPSR_RELOAD_ERRS   (OTPSR_DISTURBF | OTPSR_DEDF | OTPSR_AMEF)

#define RCC_APB4HENR        (STM32N6_RCC_BASE + 0x278U)
#define APB4H_SYSCFGEN      (1UL << 0)
#define APB4H_BSECEN        (1UL << 1)

#define OTP_SPINS           4000000UL

/** @brief Wait for the controller to go idle; 0 on timeout. */
static int otp_wait_idle(void) {
    for (unsigned long spins = OTP_SPINS; spins > 0UL; spins--) {
        if ((TIKU_REG32(BSEC_OTPSR) & OTPSR_BUSY) == 0UL) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Reload one fuse word into its shadow and read it.
 *
 * @param out  Receives the fuse value
 * @return 0 on success, negative on lock, timeout or reload error
 */
static int otp_read(uint32_t *out) {
    if (TIKU_REG32(BSEC_SRLOCK(OTP_FUSE_ID / 32U)) &
        (1UL << (OTP_FUSE_ID % 32U))) {
        return -1;                              /* reload sticky-locked */
    }
    uint32_t cr = TIKU_REG32(BSEC_OTPCR);
    cr &= ~(OTPCR_ADDR_MSK | OTPCR_PROG | OTPCR_PPLOCK);
    cr |= OTP_FUSE_ID;
    TIKU_REG32(BSEC_OTPCR) = cr;
    if (!otp_wait_idle()) {
        return -2;
    }
    if (TIKU_REG32(BSEC_OTPSR) & OTPSR_RELOAD_ERRS) {
        return -3;
    }
    *out = TIKU_REG32(BSEC_FVR(OTP_FUSE_ID));
    return 0;
}

/**
 * @brief Burn VDDIO3_HSLV if it is not already set, then verify.
 *
 * Prints every step; a second run is a clean no-op because the bit reads
 * back set.
 */
void tiku_stm32n6_otp_burn_hslv(void) {
    TIKU_REG32(RCC_APB4HENR) |= APB4H_SYSCFGEN | APB4H_BSECEN;
    (void)TIKU_REG32(RCC_APB4HENR);

    uint32_t before = 0U;
    int rc = otp_read(&before);
    if (rc != 0) {
        tiku_uart_printf("otp: read failed (%d), nothing written\n", rc);
        return;
    }
    tiku_uart_printf("otp: word %u reads %08lx\n",
                     (unsigned)OTP_FUSE_ID, (unsigned long)before);

    if (before & OTP_BIT_HSLV_VDDIO3) {
        tiku_uart_puts("otp: VDDIO3_HSLV already set, nothing to do\n");
        return;
    }
    if (TIKU_REG32(BSEC_SPLOCK(OTP_FUSE_ID / 32U)) &
        (1UL << (OTP_FUSE_ID % 32U))) {
        tiku_uart_puts("otp: word is program-locked, nothing written\n");
        return;
    }

    uint32_t want = before | OTP_BIT_HSLV_VDDIO3;
    tiku_uart_printf("otp: programming %08lx ...\n", (unsigned long)want);

    TIKU_REG32(BSEC_WDR) = want;
    uint32_t cr = TIKU_REG32(BSEC_OTPCR);
    cr &= ~(OTPCR_ADDR_MSK | OTPCR_PROG | OTPCR_PPLOCK);
    cr |= OTP_FUSE_ID | OTPCR_PROG;             /* PPLOCK clear: normal program */
    TIKU_REG32(BSEC_OTPCR) = cr;

    if (!otp_wait_idle()) {
        tiku_uart_puts("otp: program timed out\n");
        return;
    }
    if (TIKU_REG32(BSEC_OTPSR) & OTPSR_PROGFAIL) {
        tiku_uart_puts("otp: PROGFAIL set\n");
        return;
    }

    uint32_t after = 0U;
    rc = otp_read(&after);
    if (rc != 0) {
        tiku_uart_printf("otp: verify read failed (%d)\n", rc);
        return;
    }
    tiku_uart_printf("otp: word %u now reads %08lx %s\n",
                     (unsigned)OTP_FUSE_ID, (unsigned long)after,
                     (after == want) ? "(verified)" : "(MISMATCH)");
}

#endif /* TIKU_N6_OTP_TOOL */
