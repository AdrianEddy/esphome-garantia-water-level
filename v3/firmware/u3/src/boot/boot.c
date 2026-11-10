/*
 * boot.c: U3 boot manager, bank 1 pages 0-1 (0x0800 0000, 4 KB reserved). Never rewritten by the application.
 *
 * garantia-knx-direct has no host MCU: U3 is flashed over USB DFU by the ST ROM bootloader (design.md
 * "Programming"). The option bytes ignore BOOT0 and are never written, so this boot
 * manager is the way into DFU once the chip isn't blank. Based on knx-rgbic's U9 boot manager, without its
 * ESP32 beacon handshake. At every reset:
 * 0. Interrupts off and VTOR on this image: after DFU "leave" the ROM's vector table and NVIC state may still
 *    be live (review F-09). They are re-enabled only right before the jump to the ROM.
 * 1. Clear FLASH_ACR.PROGEMPTY (a later reset must boot main flash, AN2606) and re-enable SWD (the V9.2 ROM
 *    bootloader's Go clears DBG_SWEN).
 * 2. If the ROM bootloader's clock or USB setup is still there (DFU "leave" jumps to 0x0800 0000), reset once
 *    more, so the application always starts from a clean reset. The flag tells the application why.
 * 3. Keep the reset cause for the application; take its bootloader request (hw_reset_to_bootloader()).
 * 4. Sample SW1 (PB4, internal pull-up, pressed = low) for 50 ms.
 * 5. Check the application: descriptor, size, vectors, CRC-32. A flash ECC double error during the check
 *    (interrupted erase or program, RM0444 3.3.6) makes it invalid instead of hanging in the NMI.
 * 6. SW1 held, a request, or no valid application: blink the programming LED (D5 on PB5) three times, leave it
 *    on, and start the ROM bootloader, which offers USB DFU on PA11/PA12 (and its other interfaces). The blink
 *    shows the DFU entry even if the ROM's start-up de-initialisation turns D5 off again (review O-7). The ROM
 *    runs until the DFU tool sends "leave" or the board loses power. Otherwise start the application.
 * A blank chip (no boot manager yet) boots the ROM bootloader directly through the EMPTY flag.
 */
#include "stm32g0xx.h"
#include "../shared/u3_shared.h"

#define BOOT_VERSION     2u
#define BUTTON_SAMPLE_MS 50u
#define SYSCLK_HZ        16000000u

__attribute__((section(".boot_desc"), used))
const u3_boot_desc_t g_boot_desc = {U3_BOOT_DESC_MAGIC, BOOT_VERSION, 0};

static volatile uint32_t s_eccHit;

/* Flash ECC double errors raise an NMI (RM0444 3.3.8). Clear the flag, note it and continue: the CRC check
 * then fails, which is what a damaged image deserves. Anything else that raises an NMI: reset. */
void NMI_Handler(void)
{
    if (FLASH->ECCR & FLASH_ECCR_ECCD) {
        FLASH->ECCR = FLASH_ECCR_ECCD;
        s_eccHit = 1;
        return;
    }
    if (FLASH->ECC2R & FLASH_ECC2R_ECCD) {
        FLASH->ECC2R = FLASH_ECC2R_ECCD;
        s_eccHit = 1;
        return;
    }
    NVIC_SystemReset();
}

void HardFault_Handler(void) { NVIC_SystemReset(); }

static void iwdg_refresh(void) { IWDG->KR = 0xAAAAu; }   /* harmless if the IWDG isn't running */

static void systick_start(void)
{
    SysTick->LOAD = SYSCLK_HZ / 1000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
}

static void wait_ms(uint32_t ms)
{
    while (ms) {
        iwdg_refresh();
        if (SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) ms--;
    }
}

/* 4. SW1 pulls PB4 to GND_KNX; the internal pull-up (~40 kOhm) charges the short trace in microseconds. */
static int button_held(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    GPIOB->PUPDR = (GPIOB->PUPDR & ~(3u << 8)) | (1u << 8);
    GPIOB->MODER &= ~(3u << 8);
    wait_ms(2);
    for (uint32_t ms = 0; ms < BUTTON_SAMPLE_MS;) {
        iwdg_refresh();
        if (GPIOB->IDR & (1u << 4)) return 0;
        if (SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) ms++;
    }
    return 1;
}

static __attribute__((noreturn)) void start_rom_bootloader(void)
{
    /* D5: three blinks, then on while U3 sits in the ROM bootloader (R6 1 kOhm, about 1.3 mA). PB4's pull-up
     * stays. */
    GPIOB->MODER = (GPIOB->MODER & ~(3u << 10)) | (1u << 10);
    for (int i = 0; i < 3; i++) {
        GPIOB->BSRR = 1u << 5;
        wait_ms(150);
        GPIOB->BRR = 1u << 5;
        wait_ms(150);
    }
    GPIOB->BSRR = 1u << 5;
    SysTick->CTRL = 0;
    /* Pull-ups on the unconnected receive/select pins of the bootloader's other interfaces (USART2_RX PA3,
     * USART1_RX PA10, SPI1_NSS PA4), so noise can't open one of them instead of DFU. Belt and braces: the
     * bootloader may set its own pulls. PA13/PA14 (SWD) keep their reset configuration. */
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3u << 6) | (3u << 8) | (3u << 20))) | (1u << 6) | (1u << 8) | (1u << 20);
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    SYSCFG->CFGR1 = (SYSCFG->CFGR1 & ~SYSCFG_CFGR1_MEM_MODE) | SYSCFG_CFGR1_MEM_MODE_0;  /* system flash at 0 */
    SCB->VTOR = U3_SYSMEM_BASE;
    uint32_t sp = *(volatile uint32_t*)(U3_SYSMEM_BASE);
    uint32_t pc = *(volatile uint32_t*)(U3_SYSMEM_BASE + 4u);
    __set_MSP(sp);
    __DSB();
    __ISB();
    __enable_irq();                       /* the ROM expects interrupts enabled (AN2606), as after a reset */
    ((void (*)(void))pc)();
    for (;;) { }
}

/* Interrupts stay disabled: the application's hw_init() enables them once its vector table is set. */
static __attribute__((noreturn)) void start_app(void)
{
    SysTick->CTRL = 0;
    RCC->IOPRSTR = RCC_IOPRSTR_GPIOBRST;   /* PB4 back to its reset state; the application sets up its pins */
    RCC->IOPRSTR = 0;
    RCC->IOPENR &= ~RCC_IOPENR_GPIOBEN;
    uint32_t sp = *(volatile uint32_t*)(U3_APP_BASE);
    uint32_t pc = *(volatile uint32_t*)(U3_APP_BASE + 4u);
    SCB->VTOR = U3_APP_BASE;
    __set_MSP(sp);
    __DSB();
    __ISB();
    ((void (*)(void))pc)();
    for (;;) { }
}

static int app_valid(void)
{
    s_eccHit = 0;
    const u3_app_desc_t* d = (const u3_app_desc_t*)(U3_APP_BASE + U3_DESC_OFFSET);
    uint32_t magic = d->magic, size = d->imageSize, crc = d->crc32;
    if (s_eccHit || magic != U3_APP_DESC_MAGIC) return 0;
    if (size < U3_DESC_OFFSET + sizeof(u3_app_desc_t) || size > U3_APP_MAX_SIZE || (size & 7u)) return 0;
    uint32_t sp = *(const uint32_t*)U3_APP_BASE;
    uint32_t pc = *(const uint32_t*)(U3_APP_BASE + 4u);
    if (sp < U3_RAM_BASE || sp > U3_NOINIT_ADDR || (sp & 3u)) return 0;
    if (!(pc & 1u) || pc < U3_APP_BASE || pc >= U3_APP_BASE + size) return 0;
    uint32_t c = u3_app_crc((const uint8_t*)U3_APP_BASE, size);
    return !s_eccHit && c == crc;
}

/* 2. The ROM bootloader runs DFU from HSI48 (with CRS) and may leave the PLL, USB or a USART clocked. After a
 * reset all of these are off. */
static int rom_leftovers(void)
{
    return (RCC->CFGR & RCC_CFGR_SWS) != 0u || (RCC->CR & (RCC_CR_PLLON | RCC_CR_HSI48ON)) != 0u ||
           (RCC->APBENR1 & (RCC_APBENR1_USBEN | RCC_APBENR1_CRSEN | RCC_APBENR1_USART2EN)) != 0u ||
           (RCC->APBENR2 & RCC_APBENR2_USART1EN) != 0u;
}

int main(void)
{
    /* 0. */
    __disable_irq();
    SCB->VTOR = U3_BOOT_BASE;
    __DSB();
    __ISB();

    /* 1. */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_PROGEMPTY) | FLASH_ACR_DBG_SWEN;

    /* 2. */
    if (rom_leftovers()) {
        U3_SHARED->bootFlags = U3_BOOTF_FROM_ROM;
        U3_SHARED->bootFlagsInv = ~U3_BOOTF_FROM_ROM;
        NVIC_SystemReset();
    }

    /* 3. */
    uint32_t csr = RCC->CSR;
    U3_SHARED->resetCsr = csr;
    U3_SHARED->resetCsrInv = ~csr;
    RCC->CSR |= RCC_CSR_RMVF;
    int requested = U3_SHARED->bootReq == U3_BOOTREQ_MAGIC && U3_SHARED->bootReqInv == ~U3_BOOTREQ_MAGIC;
    U3_SHARED->bootReq = 0;
    U3_SHARED->bootReqInv = 0;

    /* 4. */
    systick_start();
    int held = button_held();

    /* 5., 6. */
    if (held || requested || !app_valid()) start_rom_bootloader();
    start_app();
}
