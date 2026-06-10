/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - Apollo 510 console backend (SWO / ITM)
 *
 * Backs the printf-HAL contract (tiku_uart_*) onto SWO/ITM, which is the
 * transport the hello_world example uses and the one the user has already
 * verified. The bring-up sequence mirrors hello_world.c exactly:
 *   - power OTP + Crypto (needed for the one-time DCU SWO unlock)
 *   - ungate SWO at the debugger control
 *   - am_bsp_debug_printf_enable() -> ITM/TPIU @1MHz + bind stdio to
 *     am_hal_itm_print
 *   - power OTP/Crypto back down (the DCU unlock persists, SWO keeps
 *     working — exactly as hello_world prints after powering Crypto off)
 *
 * SWO is TX-only here, so RX entry points are inert. All SDK calls are
 * tagged @ambiq-sdk; a bare-metal ITM/TPIU bring-up (CoreSight registers)
 * replaces them in the de-SDK pass, and a real COM-UART backend (pins
 * 30/55) can be selected later.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "am_mcu_apollo.h"   /* @ambiq-sdk: MCUCTRL, am_hal_pwrctrl_*, AM_HAL_DCU_SWO */
#include "am_bsp.h"          /* @ambiq-sdk: am_bsp_debug_printf_enable */
#include "am_util.h"         /* @ambiq-sdk: am_util_stdio_* */
#include "tiku_uart_arch.h"

#include <stdarg.h>

void tiku_uart_init(void) {
    bool bOTPEnabled = false;

    /* OTP + Crypto must be powered for the DCU SWO unlock inside
     * am_bsp_debug_printf_enable() to take effect. @ambiq-sdk */
    am_hal_pwrctrl_periph_enabled(AM_HAL_PWRCTRL_PERIPH_OTP, &bOTPEnabled);
    if (!bOTPEnabled) {
        am_hal_pwrctrl_periph_enable(AM_HAL_PWRCTRL_PERIPH_OTP);
    }
    am_hal_pwrctrl_periph_enable(AM_HAL_PWRCTRL_PERIPH_CRYPTO);

    /* Ungate SWO at the debugger control. @ambiq-sdk */
    MCUCTRL->DEBUGGER &= ~AM_HAL_DCU_SWO;

    /* Enable ITM/TPIU (SWO @1MHz) and bind am_util_stdio to am_hal_itm_print. */
    am_bsp_debug_printf_enable();   /* @ambiq-sdk */

    /* The DCU unlock persists, so we can drop Crypto/OTP power again. */
    am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_CRYPTO);
    if (!bOTPEnabled) {
        am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_OTP);
    }
}

void tiku_uart_putc(char c) {
    char s[2] = { c, '\0' };
    am_util_stdio_printf("%s", s);   /* @ambiq-sdk */
}

void tiku_uart_puts(const char *s) {
    /* Pass as an argument (not the format string) so a stray '%' in s is
     * not interpreted. */
    am_util_stdio_printf("%s", s);   /* @ambiq-sdk */
}

void tiku_uart_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    am_util_stdio_vprintf(fmt, ap);  /* @ambiq-sdk */
    va_end(ap);
}

/* SWO is output-only at bring-up. */
uint8_t  tiku_uart_rx_ready(void)      { return 0; }
int      tiku_uart_getc(void)          { return -1; }
uint16_t tiku_uart_overrun_count(void) { return 0; }
void     tiku_uart_overrun_reset(void) { }
