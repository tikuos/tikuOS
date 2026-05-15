# STM32F411RE Port Justification

## Summary

This STM32F411RE patch turns the port into a buildable baseline by doing
two things at the same time:

1. Wiring STM32 into the generic TikuOS routing layer so the kernel can
   find the correct arch files.
2. Adding enough STM32 arch backends to satisfy the subsystems that are
   brought up unconditionally during boot.

The implementation is intentionally mixed:

- Real bring-up code for UART, GPIO, common CPU helpers, htimer,
  critical-window IRQ masking, wake-source reporting, and region setup.
- Thin compatibility backends for ADC, I2C, SPI, 1-Wire, and GPIO IRQs,
  because the generic shell/VFS layers reference those symbols even when
  the STM32 hardware drivers are not complete yet.

## 1. Device and Board Selection

I added STM32-specific device and board selectors so `PLATFORM_STM32F411`
behaves like the existing MSP430 and RP2350 ports.

Files:

- `arch/stm32f411re/tiku_device_select.h`
- `arch/stm32f411re/devices/tiku_device_stm32f411re.h`
- `arch/stm32f411re/boards/tiku_board_nucleo_f411re.h`

Snippet:

```c
#if defined(TIKU_DEVICE_STM32F411RE)
#include <arch/stm32f411re/devices/tiku_device_stm32f411re.h>
#else
#error "No TikuOS STM32F411 device selected. Define TIKU_DEVICE_STM32F411RE."
#endif

#if defined(TIKU_BOARD_NUCLEO_F411RE)
#include <arch/stm32f411re/boards/tiku_board_nucleo_f411re.h>
#else
#define TIKU_BOARD_NUCLEO_F411RE 1
#include <arch/stm32f411re/boards/tiku_board_nucleo_f411re.h>
#endif
```

Why:

- The rest of the system expects `TIKU_DEVICE_*` and `TIKU_BOARD_*`
  metadata to exist before most kernel headers are included.
- The NUCLEO-F411RE header gives the port a concrete default mapping:
  `PA5` for LD2 and `PA2/PA3` for USART2.

## 2. Generic Routing Changes

The generic layer previously only routed MSP430 and RP2350 in many
places. I added STM32 branches so the kernel resolves to STM32 arch
files instead of failing at compile time.

Touched files:

- `tiku.h`
- `boot/tiku_boot.c`
- `hal/tiku_printf_hal.h`
- `hal/tiku_common_hal.h`
- `hal/tiku_htimer_hal.h`
- `hal/tiku_adc_hal.h`
- `hal/tiku_i2c_hal.h`
- `hal/tiku_spi_hal.h`
- `hal/tiku_onewire_hal.h`
- `hal/tiku_mem_hal.h`
- `hal/tiku_mpu_hal.h`
- `interfaces/gpio/tiku_gpio.h`

Snippet:

```c
#elif defined(PLATFORM_STM32F411)
#include <arch/stm32f411re/tiku_uart_arch.h>
#define TIKU_PRINTF(...) tiku_uart_printf(__VA_ARGS__)
```

Why:

- Boot always calls `tiku_uart_init()`.
- `tiku_mem`, `tiku_htimer`, the shell, VFS, and GPIO layers all come
  through these HAL routing points.

## 3. Real Bring-Up Backends

### UART

Files:

- `arch/stm32f411re/tiku_uart_arch.h`
- `arch/stm32f411re/tiku_uart_arch.c`

This is a real polling USART2 backend using the Nucleo default pins.

Snippet:

```c
_STM32F411_REG(STM32F411_USART_BRR(STM32F411_USART2_BASE)) =
    stm32f411_uart_brr(pclk1, (unsigned long)TIKU_BOARD_UART_BAUD);
_STM32F411_REG(STM32F411_USART_CR1(STM32F411_USART2_BASE)) =
    STM32F411_USART_CR1_UE
    | STM32F411_USART_CR1_TE
    | STM32F411_USART_CR1_RE;
```

Why:

- `MAIN_PRINTF`, shell I/O, and `/dev/console` all depend on a live UART.

### GPIO

Files:

- `arch/stm32f411re/tiku_gpio_arch.h`
- `arch/stm32f411re/tiku_gpio_arch.c`

I mapped TikuOS virtual ports `1..4` onto GPIOA..GPIOD so the existing
GPIO shell/VFS layout still works.

Snippet:

```c
case 1U:
    *gpio_base = STM32F411_GPIOA_BASE;
    *rcc_bit   = STM32F411_RCC_AHB1_GPIOA;
    return 0;
```

Why:

- LEDs, `/dev/gpio/*`, and BASIC GPIO statements need a working raw GPIO
  layer.

### High-Resolution Timer

Files:

- `arch/stm32f411re/tiku_htimer_config.h`
- `arch/stm32f411re/tiku_htimer_arch.c`

I used TIM5 as a free-running 32-bit, 1 MHz hardware timer.

Snippet:

```c
#define TIKU_HTIMER_ARCH_SECOND  1000000UL
#define TIKU_HTIMER_CLOCK_DIFF(a, b) ((int32_t)((a) - (b)))
```

```c
_STM32F411_REG(STM32F411_TIM_PSC(STM32F411_TIM5_BASE)) = psc - 1U;
_STM32F411_REG(STM32F411_TIM_ARR(STM32F411_TIM5_BASE)) = 0xFFFFFFFFUL;
_STM32F411_REG(STM32F411_TIM_CR1(STM32F411_TIM5_BASE)) = STM32F411_TIM_CR1_CEN;
```

Why:

- `tiku_sched_init()` always brings up the htimer subsystem.
- TIM5 is a clean fit for the 32-bit absolute-time model used by TikuOS.

### Common CPU Helpers

Files:

- `arch/stm32f411re/tiku_cpu_common.h`
- `arch/stm32f411re/tiku_cpu_common.c`

This backend provides delay helpers, UID access, and reset-cause
reporting via `RCC_CSR`.

Snippet:

```c
uint16_t tiku_cpu_stm32f411_reset_reason(void)
{
    return (uint16_t)((_STM32F411_REG(STM32F411_RCC_CSR) >> 16) & 0xFFFFU);
}
```

Why:

- `/sys/device/id`, `/sys/boot/reason`, and portable delay calls use
  this layer.

### Critical Window + Wake Query

Files:

- `arch/stm32f411re/tiku_crit_arch.c`
- `arch/stm32f411re/tiku_wake_arch.c`

These are real NVIC/SysTick-aware backends so the timing/power services
do not keep assuming MSP430 interrupt semantics.

Snippet:

```c
crit_state.iser0_saved = _STM32F411_REG(STM32F411_NVIC_ISER0 + 0x0U);
crit_state.iser1_saved = _STM32F411_REG(STM32F411_NVIC_ISER0 + 0x4U);
crit_state.iser2_saved = _STM32F411_REG(STM32F411_NVIC_ISER0 + 0x8U);
```

Why:

- `tiku_crit_begin()` and `/sys/power/wake` are generic kernel services,
  not optional platform extras.

## 4. Memory and MPU Plumbing

### Region Table

File:

- `arch/stm32f411re/tiku_region_arch.c`

I exposed three regions:

1. SRAM below `.uninit`
2. `.uninit` as the current NVM working copy
3. Internal flash as NVM for introspection

Snippet:

```c
if (uninit_end > uninit_start) {
    stm32f411_region_table[idx].base = (const uint8_t *)uninit_start;
    stm32f411_region_table[idx].size =
        (tiku_mem_arch_size_t)(uninit_end - uninit_start);
    stm32f411_region_table[idx].type = TIKU_MEM_REGION_NVM;
    idx++;
}
```

Why:

- `tiku_mem_init()` always calls `tiku_region_arch_get_table()`.
- Persistent buffers must live in a region the generic memory layer
  recognizes as NVM.

### Memory Backend

Files:

- `arch/stm32f411re/tiku_mem_arch.h`
- `arch/stm32f411re/tiku_mem_arch.c`

This backend is intentionally minimal: `.persistent` lives in SRAM
`.uninit`, reads/writes are plain copies, and `nvm_flush()` is a no-op.

Snippet:

```c
void tiku_mem_arch_nvm_flush(void)
{
    /* Full flash-backed persistence is not implemented yet on STM32F411RE.
     * The live .persistent working copy remains in SRAM .uninit. */
}
```

Why:

- This is enough to let the persistence-facing layers build and work
  across warm resets without pretending full flash-backed storage is
  already done.

### MPU Backend

Files:

- `arch/stm32f411re/tiku_mpu_arch.h`
- `arch/stm32f411re/tiku_mpu_arch.c`

This is a bookkeeping shim plus MemManage diagnostics, not yet a full
ARMv7-M MPU policy.

Snippet:

```c
void tiku_stm32f411_mem_manage_handler(void)
{
    uint32_t cfsr = _STM32F411_REG(STM32F411_SCB_CFSR);
    g_mpu_diag.violation_count++;
    g_mpu_diag.violation_flags = (uint16_t)(cfsr & 0x00FFU);
    ...
}
```

Why:

- The kernel always calls the MPU hooks.
- A small diagnostic implementation is better than leaving the port
  unbuildable.

## 5. Compatibility Backends

Files:

- `arch/stm32f411re/tiku_adc_arch.*`
- `arch/stm32f411re/tiku_i2c_arch.*`
- `arch/stm32f411re/tiku_spi_arch.*`
- `arch/stm32f411re/tiku_onewire_arch.*`
- `arch/stm32f411re/tiku_gpio_irq_arch.c`

These are present because the generic shell/VFS code references those
symbols even before the STM32 drivers are feature-complete.

Example:

```c
int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return g_i2c_ready ? TIKU_I2C_ERR_NACK : TIKU_I2C_ERR_BUSY;
}
```

Why:

- Without them, the build still fails at link time.
- Returning honest failure codes is safer than pretending the bus
  transaction succeeded.

## 6. Higher-Level Fixes Outside `arch/stm32f411re`

### BASIC includes

`kernel/shell/basic/tiku_basic.c` now routes GPIO by platform and uses
the generic ADC/I2C/WDT headers instead of only enabling those bridges
on MSP430.

### Reset-cause capture

`kernel/vfs/tiku_vfs_tree.c` now captures reset reason through the
portable helper instead of directly reading `SYSRSTIV`.

Snippet:

```c
/* Capture reset cause before anything else clears it. */
boot_reset_cause = tiku_common_reset_reason();
```

Why:

- That lets `/sys/boot/reason`, `/sys/boot/rstiv`, and `/sys/last_reset`
  work on STM32 instead of staying MSP430-only.

### Makefile

The STM32 source list was expanded so the new arch files actually build.

Snippet:

```make
SRCS += arch/stm32f411re/tiku_uart_arch.c
SRCS += arch/stm32f411re/tiku_mem_arch.c
SRCS += arch/stm32f411re/tiku_mpu_arch.c
SRCS += arch/stm32f411re/tiku_region_arch.c
SRCS += arch/stm32f411re/tiku_gpio_arch.c
SRCS += arch/stm32f411re/tiku_spi_arch.c
```

## Current Limits

This patch is a usable baseline, not a finished production STM32 port.

Still intentionally limited:

- No flash-backed persistence yet; `.persistent` is currently SRAM
  `.uninit` on STM32F411RE.
- ADC, I2C, SPI, and 1-Wire are compatibility backends, not final
  hardware drivers.
- GPIO edge IRQ configuration is still stubbed.
- MPU enforcement is not yet a full region-based protection policy.

That is a deliberate tradeoff: the port now builds around real boot-path
hardware while leaving the remaining peripheral work isolated and easy
to extend incrementally.
