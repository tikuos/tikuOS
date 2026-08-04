/*
 * R1b: hello from the RA8P1 over the kit's VCOM.
 *
 * Every constant below is from the RA8P1 hardware manual (R01UH1064EJ0130)
 * or measured on this board today; nothing is inherited from a vendor SDK.
 *
 *   VCOM       EK-RA8P1 UM Table 13: PD02 = TXD, PD03 = RXD
 *   Function   HW UM Table 21.20: PORTD PSEL=00100b -> TXD8_C / RXD8_C
 *   SCI8_B     datasheet Table: base 0x4035_8800, clocked by PCLKA
 *   MSTPCRB23  HW UM 11.2.7: Serial Communication Interface 8 Module Stop
 *   PFS        HW UM 21.2.5: 0x4040_0800 + 0x40*m + 4*n, PSEL[28:24], PMR[16]
 *   PWPR_S     HW UM 21.2.8: offset 0x514, PFSWE bit 6, B0WI bit 7
 *   Baud       MEASURED: SCKDIVCR reads 0 after reset, so PCLKA = MOCO =
 *              8 MHz.  HW UM Table 39.11 at 8 MHz: 9600 -> CKS 0, BRR 25.
 *              38400+ is unachievable at this clock, which is why the first
 *              console is 9600 and not the house 115200 -- the clocks are
 *              R4's milestone, not this one.
 */
#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define MSTPCRB   REG32(0x40203004u)
#define MSTPB_SCI8 (1u << 23)

#define PWPR_S    REG32(0x40400D14u)
#define PFS_PD02  REG32(0x40400B48u)
#define PFS_PD03  REG32(0x40400B4Cu)
#define PFS_SCI   ((0x04u << 24) | (1u << 16))   /* PSEL=SCI, PMR=peripheral */

#define SCI8      0x40358800u
#define SCI_TDR   REG32(SCI8 + 0x04u)
#define SCI_CCR0  REG32(SCI8 + 0x08u)
#define SCI_CCR2  REG32(SCI8 + 0x10u)
#define SCI_CSR   REG32(SCI8 + 0x48u)
#define CCR0_TE   (1u << 4)
#define CSR_TDRE  (1u << 29)
#define CSR_TEND  (1u << 30)

/* MDDR and BCP keep their reset values; only BRR and CKS carry the rate. */
#define CCR2_9600 (0xFFu << 24 | 25u << 8 | 0x04u)

/* R1a's observation words stay, so "did it run" and "did the UART work"
 * remain separable when the console says nothing. */
#define MARK  REG32(0x22100000u)
#define COUNT REG32(0x22100004u)

extern uint32_t __stack_top;
void reset_handler(void);

__attribute__((section(".vectors"), used))
void *const vectors[] = { (void *)&__stack_top, (void *)reset_handler };

static void uart_init(void)
{
    MSTPCRB &= ~MSTPB_SCI8;          /* ungate SCI8 before touching it */

    PWPR_S = 0x00u;                  /* clear B0WI, then arm PFSWE     */
    PWPR_S = 0x40u;
    PFS_PD02 = PFS_SCI;
    PFS_PD03 = PFS_SCI;
    PWPR_S = 0x80u;                  /* re-protect                     */

    SCI_CCR0 = 0u;                   /* TE/RE off while reconfiguring  */
    while (SCI_CCR0 != 0u) { }
    SCI_CCR2 = CCR2_9600;            /* CCR1/CCR3/CCR4 keep reset =    */
    SCI_CCR0 = CCR0_TE;              /* async 8N1, internal clock      */
}

static void uart_putc(char c)
{
    while ((SCI_CSR & CSR_TDRE) == 0u) { }
    SCI_TDR = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n') { uart_putc('\r'); }
        uart_putc(*s++);
    }
    while ((SCI_CSR & CSR_TEND) == 0u) { }
}

void reset_handler(void)
{
    MARK = 0x8511A801u;
    uart_init();
    for (;;) {
        uart_puts("hello from RA8P1 -- Cortex-M85, TikuOS R1\n");
        COUNT++;
        for (volatile uint32_t d = 0; d < 200000u; d++) { }
    }
}
