/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - RP2350 (Cortex-M33) startup
 *
 * Provides the bare minimum to get from the boot ROM to main():
 *   1. .boot2 stub (256 bytes that the boot ROM loads into SRAM at
 *      0x20000000 and jumps to). Its job is to bring up the QSPI
 *      interface so the rest of the image can be executed XIP from
 *      flash. We use a minimal "trust the ROM defaults" sequence
 *      that works for the standard Pico 2 W flash chip (W25Q32JV-IQ
 *      class) and just hands control back via the lr the ROM stashed.
 *   2. .image_def block: the RP2350 boot ROM scans the first 4 KB of
 *      flash for an IMAGE_DEF describing the image type/entry. We emit
 *      a minimal "executable, ARM, secure" descriptor.
 *   3. Vector table: 256 entries (16 system + 240 NVIC). All entries
 *      default to a do-nothing handler; drivers that care provide their
 *      own implementation via the matching weak symbol.
 *   4. Reset handler: copy .data, zero .bss, init SystemInit, call main.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Linker-script symbols                                                     */
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
/* main()                                                                    */
/*---------------------------------------------------------------------------*/

extern int main(void);

/** @brief ISR function-pointer type used throughout the vector table. */
typedef void (*rp2350_isr_t)(void);
#define RP2350_NUM_EXT_IRQS  64
extern const rp2350_isr_t tiku_rp2350_vectors[16 + RP2350_NUM_EXT_IRQS];

/*---------------------------------------------------------------------------*/
/* Default handlers (override with own functions of the same name)           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default ISR handler — spin forever on an unhandled exception.
 *
 * Alias target for all weak exception/IRQ stubs. Spinning on WFE keeps
 * the core in a low-power state so a debugger halt lands on a recognisable
 * PC rather than a random instruction stream.
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
 * Each stub can be overridden by a non-weak definition of the same name
 * in any driver or kernel file. The vector table below references these
 * symbols so the linker prefers the real implementation when present.
 * SysTick is included here so the vector table can be populated before
 * the timer arch driver installs its own non-weak handler.
 *
 * External IRQs wired: TIMER0_ALARM0, UART0, IO_BANK0, PIO0_IRQ0, DMA_IRQ0.
 */
void tiku_rp2350_nmi_handler(void)        __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_hard_fault_handler(void) __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_mem_fault_handler(void)  __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_bus_fault_handler(void)  __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_usage_fault_handler(void) __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_secure_fault_handler(void) __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_svc_handler(void)        __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_pendsv_handler(void)     __attribute__((weak, alias("rp2350_default_handler")));

/* SysTick lives here so we can populate the vector table before the
 * timer arch driver supplies the real handler. The arch file overrides
 * this with a non-weak definition. */
void tiku_rp2350_systick_handler(void) __attribute__((weak, alias("rp2350_default_handler")));

/* External IRQ stubs we wire up: TIMER0 alarm 0, UART0, IO_BANK0,
 * PIO0 IRQ 0 (bit-bang completion), DMA IRQ 0 (memcpy completion). */
void tiku_rp2350_timer0_alarm0_isr(void) __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_uart0_isr(void)         __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_io_bank0_isr(void)      __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_pio0_irq0_handler(void) __attribute__((weak, alias("rp2350_default_handler")));
void tiku_rp2350_dma_irq0_handler(void)  __attribute__((weak, alias("rp2350_default_handler")));

/*---------------------------------------------------------------------------*/
/* Reset handler                                                             */
/*---------------------------------------------------------------------------*/

void tiku_rp2350_reset_handler(void) __attribute__((naked, section(".text"), used));

/**
 * @brief RP2350 reset handler: C runtime init and entry to main().
 *
 * Runs with SP already set by the boot ROM (loaded from vectors[0]).
 * Immediately masks all maskable IRQs to prevent SysTick or any other
 * early-programmed IRQ source from firing before the scheduler is ready.
 * Then explicitly writes VTOR, copies .data from flash to SRAM, zeros
 * .bss, and calls main(). The .uninit region is left untouched so
 * warm-reset state survives. Marked naked so the compiler emits no
 * prologue that would touch uninitialised call-saved registers.
 */
void tiku_rp2350_reset_handler(void) {
    /* Mask all maskable IRQs *immediately*. Cortex-M resets with
     * PRIMASK = 0 (IRQs enabled), so anything that programs an IRQ
     * source while we're still initialising the kernel — for
     * instance SysTick.TICKINT in tiku_clock_arch_init() — would
     * fire its handler before tiku_sched_init() has built the
     * process queue, dereferencing NULL.
     *
     * The scheduler re-enables IRQs at the top of tiku_sched_loop(). */
    __asm__ volatile ("cpsid i" ::: "memory");

    /* Point the M33 VTOR at our vector table explicitly. The boot
     * ROM is supposed to do this from the IMAGE_DEF VECTOR_TABLE
     * item, but if anything is off the resulting silent hard fault
     * is brutal to diagnose, so do it ourselves. The vector-table
     * address must be aligned per VTOR.TBLOFF requirements (we
     * align to 512 in the linker script). */
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

    /* The .uninit region is intentionally NOT zeroed — it holds
     * boot-counter / device-name state that survives warm resets. */

    (void)main();

    /* Should never return; if it does, halt cleanly. */
    while (1) {
        __asm__ volatile ("wfe");
    }
}

/*---------------------------------------------------------------------------*/
/* Vector table                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Cortex-M33 vector table for RP2350.
 *
 * Placed in the .vectors section so the linker script aligns it to the
 * VTOR.TBLOFF requirement (512-byte boundary in the linker script). The
 * boot ROM reads SCB.VTOR from the IMAGE_DEF VECTOR_TABLE item and loads
 * SP from entry 0 before jumping to entry 1 (reset handler).
 *
 * RP2350 exposes IRQs 0..51 (datasheet §3.6.1). The array is sized to
 * 16 (system exceptions) + 64 (external IRQs) = 80 entries to cover all
 * used IRQs with margin. Unused slots are filled with rp2350_default_handler
 * so an unexpected IRQ ends up in a debuggable spin loop rather than
 * executing a NULL pointer.
 *
 * Typedef rp2350_isr_t and RP2350_NUM_EXT_IRQS are declared near the
 * top of this file so the reset handler can reference the array before
 * it is defined textually.
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

    /* All remaining slots default to rp2350_default_handler via the
     * designated-init zero, which the linker fills with NULL — but
     * NULL is a valid pointer here that would cause a hard fault
     * if dispatched. Replace nulls explicitly. */
    [16 +  5 ... 16 +  9] = rp2350_default_handler,
    [16 + 11 ... 16 + 14] = rp2350_default_handler,
    [16 + 16 ... 16 + 20] = rp2350_default_handler,
    [16 + 22 ... 16 + 32] = rp2350_default_handler,
    [16 + 34 ... 16 + 63] = rp2350_default_handler,
};

/*---------------------------------------------------------------------------*/
/* RP2350 IMAGE_DEF block                                                    */
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
 *   word 1  IMAGE_TYPE item — packs everything into ONE word:
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
 *           of the image — which on our layout is the .boot2 padding
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
 * The boot ROM scans the first 4 KB of flash for a block starting with
 * marker_start (0xFFFFDED3) and ending with marker_end (0xAB123579).
 * We emit the minimal "executable, ARM, secure" descriptor with a
 * VECTOR_TABLE item so the ROM points VTOR at our vector table. See
 * pico-sdk picobin.h for the authoritative field encoding.
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

/**
 * @brief Combined IMAGE_TYPE flags for "ARM secure executable on RP2350".
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
 * Cortex-M33 vector table. Must remain in the .image_def section (defined
 * in the linker script at a fixed offset within the first 4 KB of flash).
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
/* Boot2: minimal QSPI XIP setup                                             */
/*---------------------------------------------------------------------------*/

/*
 * On RP2350 the boot ROM bootloader is much more capable than on the
 * RP2040 — for a basic image marked with the IMAGE_DEF block above,
 * the ROM enables XIP itself and jumps to our reset handler with
 * flash already executing. Our .boot2 region therefore only has to
 * exist (the linker pads it to 256 bytes) and need not contain any
 * special second-stage payload. Keep an explicit 16-bit literal so
 * the section is non-empty and the linker layout stays well-defined.
 *
 * If you ever need to swap to a non-default flash chip that requires
 * a custom CS/CLK ratio or different read command, replace this stub
 * with a hand-tuned routine in arch/arm-rp2350/devices/boot2_*.S
 * (see the RP2350 SDK for reference implementations).
 */
/**
 * @brief Minimal .boot2 placeholder for RP2350 (boot ROM already enables XIP).
 *
 * On RP2350 the boot ROM handles XIP setup before jumping to the reset
 * handler, so .boot2 need not contain a real second-stage payload. This
 * 32-bit constant keeps the .boot2 section non-empty so the linker layout
 * is well-defined. Replace with a hand-tuned routine if a non-default
 * flash chip requires a custom CS/CLK ratio or read command.
 */
const uint32_t tiku_rp2350_boot2_marker
__attribute__((section(".boot2"), used)) = 0xDEADBE2FU;
