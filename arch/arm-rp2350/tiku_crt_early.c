/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - RP2350 (Cortex-M33) startup.
 *
 * The minimum path from boot ROM to main(): a .boot2 placeholder, the IMAGE_DEF
 * block the ROM scans for, an 80-entry vector table of weak handlers, and a
 * reset handler that masks IRQs, sets VTOR, copies .data and zeroes .bss.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* LINKER-SCRIPT SYMBOLS                                                     */
/*---------------------------------------------------------------------------*/

extern uint32_t __data_load;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern uint32_t __stack;
extern uint32_t __uninit_start;
extern uint32_t __uninit_end;

/*---------------------------------------------------------------------------*/
/* ENTRY POINT AND VECTOR TABLE DECLARATION                                  */
/*---------------------------------------------------------------------------*/

extern int main(void);

/** @brief ISR function-pointer type used throughout the vector table. */
typedef void (*rp2350_isr_t)(void);
#define RP2350_NUM_EXT_IRQS  64
extern const rp2350_isr_t tiku_rp2350_vectors[16 + RP2350_NUM_EXT_IRQS];

/*---------------------------------------------------------------------------*/
/* DEFAULT HANDLERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default ISR handler: park the core in a WFE loop.
 *
 * Every weak exception and IRQ stub aliases it, so an unhandled exception
 * stops here, at a PC a debugger recognises, with the core in low power.
 */
static void rp2350_default_handler(void) {
    while (1) {
        __asm__ volatile ("wfe");
    }
}

/**
 * @defgroup rp2350_exception_stubs Cortex-M33 weak exception/IRQ stubs
 * @brief Weak aliases that default to rp2350_default_handler.
 *
 * A non-weak definition of the same name in any file replaces a stub at link
 * time.  The vector table wires these, SysTick, and five IRQs: TIMER0 alarm 0,
 * DMA_IRQ_0, PIO0_IRQ_0, IO_IRQ_BANK0 and UART0.
 */
void tiku_rp2350_nmi_handler(void)        __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_hard_fault_handler(void) __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_mem_fault_handler(void)  __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_bus_fault_handler(void)  __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_usage_fault_handler(void) __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_secure_fault_handler(void) __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_svc_handler(void)        __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_pendsv_handler(void)     __attribute__((weak, alias("rp2350_default_handler")));

/* tiku_timer_arch.c defines the non-weak SysTick handler. */
void tiku_rp2350_systick_handler(void) __attribute__((weak, alias("rp2350_default_handler")));

/* External IRQ stubs wired up here: TIMER0 alarm 0, UART0, IO_BANK0,
 * PIO0 IRQ 0 (bit-bang completion), DMA IRQ 0 (memcpy completion). */
void tiku_rp2350_timer0_alarm0_isr(void) __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_uart0_isr(void)         __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_io_bank0_isr(void)      __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_pio0_irq0_handler(void) __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_dma_irq0_handler(void)  __attribute__((weak, alias("rp2350_default_handler")));

/*---------------------------------------------------------------------------*/
/* RESET HANDLER                                                             */
/*---------------------------------------------------------------------------*/

void tiku_rp2350_reset_handler(void) __attribute__((section(".text"), used));

/**
 * @brief RP2350 reset handler: C runtime init and entry to main().
 *
 * Runs with SP already set by the boot ROM.  Masks all maskable IRQs, writes
 * VTOR, copies .data from flash to SRAM, zeros .bss and calls main(); .uninit
 * is not zeroed.
 */
void tiku_rp2350_reset_handler(void) {
    /* Mask all maskable IRQs first.  Cortex-M resets with PRIMASK = 0
     * (IRQs enabled), and a source programmed during kernel init, such
     * as SysTick.TICKINT in tiku_clock_arch_init(), would otherwise run
     * its handler before tiku_sched_init() has built the process queue,
     * dereferencing NULL.
     *
     * The scheduler re-enables IRQs at the top of tiku_sched_loop(). */
    __asm__ volatile ("cpsid i" ::: "memory");

    /* Set VTOR to the vector table.  The boot ROM also sets it from the
     * IMAGE_DEF VECTOR_TABLE item.  VTOR.TBLOFF needs the 512-byte
     * alignment the linker script gives .vectors. */
    *(volatile uint32_t *)0xE000ED08U = (uint32_t)tiku_rp2350_vectors;

    /* Copy .data from flash to SRAM. */
    uint32_t *src = &__data_load;
    uint32_t *dst = &__data_start;
    while (dst < &__data_end) {
        *dst++ = *src++;
    }

    /* Zero .bss. */
    dst = &__bss_start;
    while (dst < &__bss_end) {
        *dst++ = 0U;
    }

    /* .uninit is not zeroed: it holds the durable state (boot counter,
     * device name, persist cells), restored from flash by
     * tiku_mem_arch_init(). */

    (void)main();

    /* main() does not return; if it does, park the core. */
    while (1) {
        __asm__ volatile ("wfe");
    }
}

/*---------------------------------------------------------------------------*/
/* VECTOR TABLE                                                              */
/*---------------------------------------------------------------------------*/

/*
 * Cortex-M33 vector table for RP2350.
 *
 * Placed in .vectors so the linker aligns it to the VTOR.TBLOFF requirement.
 * The boot ROM sets VTOR from the IMAGE_DEF VECTOR_TABLE item, loads SP from
 * entry 0 and jumps to entry 1.
 *
 * RP2350 exposes IRQs 0..51 (datasheet 3.6.1).  The array holds 16 system
 * exceptions + 64 external IRQs = 80 entries.  Every slot without a driver
 * handler holds rp2350_default_handler; a NULL slot would hard-fault when
 * taken.
 *
 * rp2350_isr_t and RP2350_NUM_EXT_IRQS are declared near the top of this file
 * so the reset handler can reference the array before it is defined textually.
 */
const rp2350_isr_t tiku_rp2350_vectors[16 + RP2350_NUM_EXT_IRQS]
__attribute__((section(".vectors"), used)) = {
    /* System exceptions ------------------------------------------------ */
    (rp2350_isr_t)(&__stack),                  /*  0  Initial SP        */
    tiku_rp2350_reset_handler,                 /*  1  Reset             */
    tiku_rp2350_nmi_handler,                   /*  2  NMI               */
    tiku_rp2350_hard_fault_handler,            /*  3  HardFault         */
    tiku_rp2350_mem_fault_handler,             /*  4  MemManage         */
    tiku_rp2350_bus_fault_handler,             /*  5  BusFault          */
    tiku_rp2350_usage_fault_handler,           /*  6  UsageFault        */
    tiku_rp2350_secure_fault_handler,          /*  7  SecureFault (v8M) */
    rp2350_default_handler,                    /*  8  Reserved          */
    rp2350_default_handler,                    /*  9  Reserved          */
    rp2350_default_handler,                    /* 10  Reserved          */
    tiku_rp2350_svc_handler,                   /* 11  SVC               */
    rp2350_default_handler,                    /* 12  DebugMon          */
    rp2350_default_handler,                    /* 13  Reserved          */
    tiku_rp2350_pendsv_handler,                /* 14  PendSV            */
    tiku_rp2350_systick_handler,               /* 15  SysTick           */

    /* External interrupts (RP2350 datasheet §3.6.1) ------------------- */
    [16 +  0] = tiku_rp2350_timer0_alarm0_isr, /* IRQ  0  TIMER0_IRQ_0 */
    [16 +  1] = rp2350_default_handler,        /* IRQ  1  TIMER0_IRQ_1 */
    [16 +  2] = rp2350_default_handler,        /* IRQ  2  TIMER0_IRQ_2 */
    [16 +  3] = rp2350_default_handler,        /* IRQ  3  TIMER0_IRQ_3 */
    [16 +  4] = rp2350_default_handler,        /* IRQ  4  TIMER1_IRQ_0 */
    [16 + 10] = tiku_rp2350_dma_irq0_handler,  /* IRQ 10  DMA_IRQ_0    */
    [16 + 15] = tiku_rp2350_pio0_irq0_handler, /* IRQ 15  PIO0_IRQ_0   */
    [16 + 21] = tiku_rp2350_io_bank0_isr,      /* IRQ 21  IO_IRQ_BANK0 */
    [16 + 33] = tiku_rp2350_uart0_isr,         /* IRQ 33  UART0_IRQ    */

    /* The remaining slots would be NULL, which hard-faults when taken;
     * these ranges fill them with rp2350_default_handler. */
    [16 +  5 ... 16 +  9] = rp2350_default_handler,
    [16 + 11 ... 16 + 14] = rp2350_default_handler,
    [16 + 16 ... 16 + 20] = rp2350_default_handler,
    [16 + 22 ... 16 + 32] = rp2350_default_handler,
    [16 + 34 ... 16 + 63] = rp2350_default_handler,
};

/*---------------------------------------------------------------------------*/
/* RP2350 IMAGE_DEF BLOCK                                                    */
/*---------------------------------------------------------------------------*/

/*
 * Authoritative reference: pico-sdk
 *   src/common/boot_picobin_headers/include/boot/picobin.h
 *   src/rp2_common/pico_crt0/embedded_start_block.inc.S
 *
 * Block layout (every 32-bit word is little-endian on disk):
 *
 *   word 0  marker_start = 0xffffded3
 *
 *   word 1  IMAGE_TYPE item, all of it in one word:
 *             byte 0 = type 0x42
 *             byte 1 = item size in words = 1
 *             bytes 2-3 = 16-bit IMAGE_TYPE flags
 *
 *           IMAGE_TYPE flags (per picobin.h):
 *             [3:0]   IMAGE_TYPE = 1 (EXE)
 *             [5:4]   SECURITY    = 2 (SECURE)        -> 0x20
 *             [10:8]  CPU         = 0 (ARM)           -> 0x00
 *             [14:12] CHIP        = 1 (RP2350)        -> 0x1000
 *           value = 0x1021 — the SDK's `PICO_RP2350 ARM Secure EXE`
 *           default for the standard flash crt0.
 *
 *   word 2  VECTOR_TABLE item header:
 *             byte 0 = type 0x03
 *             byte 1 = item size in words = 2 (header + address)
 *             bytes 2-3 = pad 0
 *   word 3  vector_table_addr = &tiku_rp2350_vectors
 *
 *           This tells the boot ROM where the M33 vector table lives.
 *           Without it, the ROM defaults to looking at the very start
 *           of the image — which in this layout is the .boot2 padding
 *           region, not a vector table.
 *
 *   word 4  LAST item header:
 *             byte 0 = type 0xff
 *             bytes 1-2 = "size" = sum of word counts of all items
 *                         between marker_start and LAST (exclusive),
 *                         i.e. IMAGE_TYPE (1 word) + VECTOR_TABLE (2)
 *                         = 3
 *             byte 3 = pad 0
 *
 *   word 5  next_block_offset = 0 (single-block IMAGE_DEF)
 *   word 6  marker_end = 0xab123579
 */

/**
 * @brief RP2350 IMAGE_DEF block layout as a C struct.
 *
 * The boot ROM scans the first 4 KB of flash for a block from marker_start
 * (0xFFFFDED3) to marker_end (0xAB123579): here the minimal "executable, ARM,
 * secure" descriptor and a VECTOR_TABLE item, encoded per pico-sdk picobin.h.
 */
struct rp2350_image_def {
    uint32_t marker_start;     /**< Block start magic: 0xFFFFDED3 */
    uint32_t image_type_word;  /**< IMAGE_TYPE item: type+size+flags packed */
    uint32_t vector_table_hdr; /**< VECTOR_TABLE item header */
    uint32_t vector_table_addr;/**< Address of tiku_rp2350_vectors */
    uint32_t last_word;        /**< LAST item with item-word count */
    uint32_t next_block_offset;/**< 0 for a single-block IMAGE_DEF */
    uint32_t marker_end;       /**< Block end magic: 0xAB123579 */
};

/*
 * Combined IMAGE_TYPE flags for "ARM secure executable on RP2350".
 *
 * Per picobin.h LSB layout:
 *   bits [3:0]  IMAGE_TYPE = 1 (EXE)
 *   bits [5:4]  SECURITY   = 2 (SECURE)
 *   bits [10:8] CPU        = 0 (ARM)
 *   bits [14:12] CHIP      = 1 (RP2350)
 * Produces 0x1021, matching the SDK's default for a standard flash crt0.
 */
#define TIKU_IMAGE_TYPE_FLAGS  ((1U << 0) | (2U << 4) | (0U << 8) | (1U << 12))

/**
 * @brief RP2350 IMAGE_DEF descriptor placed in the .image_def flash section.
 *
 * Consumed by the boot ROM to identify the image type and locate the
 * Cortex-M33 vector table.
 *
 * @note It must stay in .image_def, which the linker script places at
 *       0x10000100, inside the first 4 KB of flash that the boot ROM scans.
 */
const struct rp2350_image_def tiku_rp2350_image_def
__attribute__((section(".image_def"), used)) = {
    .marker_start      = 0xFFFFDED3U,
    /* word: byte0=type=0x42, byte1=size=1, bytes2-3=flags */
    .image_type_word   = ((uint32_t)TIKU_IMAGE_TYPE_FLAGS << 16)
                       | (1U << 8) | 0x42U,
    /* word: byte0=type=0x03, byte1=size=2, bytes2-3=pad=0 */
    .vector_table_hdr  = (2U << 8) | 0x03U,
    .vector_table_addr = (uint32_t)tiku_rp2350_vectors,
    /* word: byte0=0xff, bytes1-2=item-words-between-markers=3 */
    .last_word         = (3U << 8) | 0xFFU,
    .next_block_offset = 0U,
    .marker_end        = 0xAB123579U,
};

/*---------------------------------------------------------------------------*/
/* BOOT2 PLACEHOLDER                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Placeholder word that keeps .boot2 non-empty.
 *
 * For an image with the IMAGE_DEF block above, the boot ROM sets up XIP and
 * jumps to the reset handler itself, so .boot2 holds no second-stage loader;
 * the linker pads the section to 256 bytes.
 *
 * @note A flash chip that needs another clock divider or read command needs
 *       a real boot2 routine in place of this word.
 */
const uint32_t tiku_rp2350_boot2_marker
__attribute__((section(".boot2"), used)) = 0xDEADBE2FU;
