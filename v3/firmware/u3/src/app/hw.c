/* hw.c: see hw.h. The KNX UART and flash code are knx-rgbic's U9 hw.c with USART1 on PB6/PB7 instead of
 * USART4 on PA0/PA1; the ESP32 link is replaced by a transmit-only debug log. */
#include "hw.h"
#include "stm32g0xx.h"
#include "../shared/u3_shared.h"

/* ---------------------------------------------------------------- time */

static volatile uint32_t s_ms;

void SysTick_Handler(void) { s_ms++; }

uint32_t hw_millis(void) { return s_ms; }

void hw_delay_us(uint32_t us)
{
    /* SysTick counts down from LOAD at HW_SYSCLK_HZ; use it for sub-millisecond waits. */
    uint32_t ticks = us * (HW_SYSCLK_HZ / 1000000u);
    uint32_t last = SysTick->VAL;
    while (ticks) {
        uint32_t now = SysTick->VAL;
        uint32_t d = (last >= now) ? (last - now) : (last + SysTick->LOAD + 1u - now);
        last = now;
        if (d >= ticks) break;
        ticks -= d;
    }
}

/* ---------------------------------------------------------------- watchdog */

void hw_iwdg_start(void)
{
    IWDG->KR = 0xCCCCu;                 /* start (no-op if already running) */
    IWDG->KR = 0x5555u;                 /* unlock PR/RLR */
    IWDG->PR = 4u;                      /* LSI 32 kHz / 64 = 500 Hz */
    IWDG->RLR = 1000u;                  /* 2 s */
    for (uint32_t t = hw_millis(); IWDG->SR != 0u && (hw_millis() - t) < 100u;) { }
    IWDG->KR = 0xAAAAu;
}

void hw_iwdg_refresh(void) { IWDG->KR = 0xAAAAu; }

/* ---------------------------------------------------------------- ring buffers */

typedef struct {
    uint8_t* buf;
    uint16_t size;                      /* power of two */
    volatile uint16_t head;             /* written by the producer */
    volatile uint16_t tail;             /* written by the consumer */
} ring_t;

static inline uint16_t ring_count(const ring_t* r) { return (uint16_t)((r->head - r->tail) & (r->size - 1u)); }
static inline uint16_t ring_free(const ring_t* r) { return (uint16_t)(r->size - 1u - ring_count(r)); }

static inline int ring_put(ring_t* r, uint8_t b)
{
    uint16_t h = r->head, n = (uint16_t)((h + 1u) & (r->size - 1u));
    if (n == r->tail) return 0;
    r->buf[h] = b;
    r->head = n;
    return 1;
}

static inline int ring_get(ring_t* r)
{
    uint16_t t = r->tail;
    if (t == r->head) return -1;
    uint8_t b = r->buf[t];
    r->tail = (uint16_t)((t + 1u) & (r->size - 1u));
    return b;
}

/* ---------------------------------------------------------------- GPIO helpers */

#define BRR_FOR(baud) ((HW_SYSCLK_HZ + (baud) / 2u) / (baud))

static void gpio_af(GPIO_TypeDef* g, uint32_t pin, uint32_t af, int pullup)
{
    g->MODER = (g->MODER & ~(3u << (pin * 2u))) | (2u << (pin * 2u));
    g->OTYPER &= ~(1u << pin);
    g->OSPEEDR = (g->OSPEEDR & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
    g->PUPDR = (g->PUPDR & ~(3u << (pin * 2u))) | ((pullup ? 1u : 0u) << (pin * 2u));
    volatile uint32_t* afr = &g->AFR[pin >> 3];
    *afr = (*afr & ~(0xFu << ((pin & 7u) * 4u))) | (af << ((pin & 7u) * 4u));
}

static void gpio_input(GPIO_TypeDef* g, uint32_t pin, int pullup)
{
    g->PUPDR = (g->PUPDR & ~(3u << (pin * 2u))) | ((pullup ? 1u : 0u) << (pin * 2u));
    g->MODER &= ~(3u << (pin * 2u));
}

static void gpio_output(GPIO_TypeDef* g, uint32_t pin)
{
    g->BRR = 1u << pin;
    g->OTYPER &= ~(1u << pin);
    g->PUPDR &= ~(3u << (pin * 2u));
    g->MODER = (g->MODER & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
}

/* ---------------------------------------------------------------- USART1: NCN5130 */

/* TX: one TP-UART transaction at a time, up to 263 frame bytes x (service + data) + offsets = ~720 bytes;
 * thelsing writes the whole frame at once and ignores short writes, so the ring must hold it. */
static uint8_t s_knx_rx_buf[512];
static uint8_t s_knx_tx_buf[1024];
static ring_t s_knx_rx = {s_knx_rx_buf, sizeof s_knx_rx_buf, 0, 0};
static ring_t s_knx_tx = {s_knx_tx_buf, sizeof s_knx_tx_buf, 0, 0};
/* One flag per RX slot: the byte had a parity, framing or noise error. In 9-bit mode the NCN5130 marks bus
 * errors (acceptance window, pulse duration) with a wrong parity bit, so thelsing needs it per byte. */
static uint8_t s_knx_rx_err[sizeof s_knx_rx_buf / 8];
static int s_knx_last_err;
static volatile int s_knx_ovf;
static volatile uint32_t s_knx_err;
static volatile hw_isr_hook_t s_knx_hook;

void hw_knx_uart_set_rx_hook(hw_isr_hook_t hook) { s_knx_hook = hook; }

void hw_knx_uart_open(void)
{
    USART_TypeDef* u = USART1;
    RCC->APBENR2 |= RCC_APBENR2_USART1EN;
    gpio_af(GPIOB, 6, 0, 0);            /* R5 holds KNX_RXD high while this pin isn't driven */
    gpio_af(GPIOB, 7, 0, 1);            /* weak pull-up: NCN TXD is push-pull, this only covers its reset */
    s_knx_rx.head = s_knx_rx.tail = 0;
    s_knx_tx.head = s_knx_tx.tail = 0;
    s_knx_ovf = 0;
    u->CR1 = 0;
    u->CR2 = 0;
    u->CR3 = 0;
    u->BRR = BRR_FOR(HW_KNX_BAUD);
    /* M = 01 (9-bit word = 8 data + parity), even parity, RX interrupt. FIFO off (FIFOEN = 0): PE/FE/NE then
     * belong to the byte in RDR, as on knx-rgbic's USART4, which has no FIFO. */
    u->CR1 = USART_CR1_M0 | USART_CR1_PCE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_TE | USART_CR1_RE;
    u->ICR = 0xFFFFFFFFu;
    u->CR1 |= USART_CR1_UE;
    NVIC_SetPriority(USART1_IRQn, 1);
    NVIC_EnableIRQ(USART1_IRQn);
}

void hw_knx_uart_close(void)
{
    NVIC_DisableIRQ(USART1_IRQn);
    USART1->CR1 = 0;
}

int hw_knx_uart_read(void)
{
    uint16_t t = s_knx_rx.tail;
    int b = ring_get(&s_knx_rx);
    if (b >= 0) s_knx_last_err = (s_knx_rx_err[t >> 3] >> (t & 7u)) & 1u;
    return b;
}

int hw_knx_uart_last_error(void) { return s_knx_last_err; }
size_t hw_knx_uart_available(void) { return ring_count(&s_knx_rx); }
uint32_t hw_knx_uart_errors(void) { return s_knx_err; }

int hw_knx_uart_overflow(void)
{
    int o = s_knx_ovf;
    s_knx_ovf = 0;
    return o;
}

size_t hw_knx_uart_write(const uint8_t* p, size_t n)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    if (ring_free(&s_knx_tx) < n) {
        __set_PRIMASK(pm);
        return 0;
    }
    for (size_t i = 0; i < n; i++) ring_put(&s_knx_tx, p[i]);
    USART1->CR1 |= USART_CR1_TXEIE_TXFNFIE;
    __set_PRIMASK(pm);
    return n;
}

void hw_knx_uart_flush(void)
{
    uint32_t t = hw_millis();
    while ((ring_count(&s_knx_tx) || !(USART1->ISR & USART_ISR_TC)) && (hw_millis() - t) < 200u) { }
}

void USART1_IRQHandler(void)
{
    USART_TypeDef* u = USART1;
    int got = 0;
    for (;;) {
        /* FIFO off: PE/FE/NE belong to the byte now in RDR. Bit 8 of RDR is the parity bit. */
        uint32_t isr = u->ISR;
        if (isr & USART_ISR_ORE) {
            s_knx_ovf = 1;
            u->ICR = USART_ICR_ORECF;
        }
        if (!(isr & USART_ISR_RXNE_RXFNE)) {
            if (isr & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE))
                u->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF;
            break;
        }
        uint32_t err = (isr & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE)) ? 1u : 0u;
        if (err) {
            s_knx_err++;
            u->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF;
        }
        uint16_t h = s_knx_rx.head;
        uint8_t b = (uint8_t)(u->RDR & 0xFFu);
        if (ring_put(&s_knx_rx, b)) {
            uint8_t m = (uint8_t)(1u << (h & 7u));
            if (err) s_knx_rx_err[h >> 3] |= m;
            else s_knx_rx_err[h >> 3] &= (uint8_t)~m;
        } else {
            s_knx_ovf = 1;
        }
        got = 1;
    }
    if ((u->CR1 & USART_CR1_TXEIE_TXFNFIE) && (u->ISR & USART_ISR_TXE_TXFNF)) {
        int b = ring_get(&s_knx_tx);
        if (b >= 0) u->TDR = (uint8_t)b;
        else u->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
    }
    if (got && s_knx_hook) s_knx_hook();
}

/* ---------------------------------------------------------------- USART2 TX: debug log on TP7 */

static uint8_t s_dbg_buf[1024];
static ring_t s_dbg = {s_dbg_buf, sizeof s_dbg_buf, 0, 0};

static void dbg_open(void)
{
    RCC->APBENR1 |= RCC_APBENR1_USART2EN;
    gpio_af(GPIOA, 2, 1, 0);
    USART2->CR1 = 0;
    USART2->BRR = BRR_FOR(HW_DBG_BAUD);
    USART2->CR1 = USART_CR1_TE | USART_CR1_FIFOEN;   /* 8N1, transmit only, 8-byte FIFO */
    USART2->CR1 |= USART_CR1_UE;
    NVIC_SetPriority(USART2_LPUART2_IRQn, 3);
    NVIC_EnableIRQ(USART2_LPUART2_IRQn);
}

void hw_dbg_write(const char* s, size_t n)
{
#if !U3_DEBUG_LOG
    (void)s;
    (void)n;
    return;
#endif
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    for (size_t i = 0; i < n && ring_put(&s_dbg, (uint8_t)s[i]); i++) { }
    USART2->CR1 |= USART_CR1_TXEIE_TXFNFIE;
    __set_PRIMASK(pm);
}

void hw_dbg_flush(void)
{
#if !U3_DEBUG_LOG
    return;
#endif
    uint32_t t = hw_millis();
    while ((ring_count(&s_dbg) || !(USART2->ISR & USART_ISR_TC)) && (hw_millis() - t) < 200u) hw_iwdg_refresh();
}

void USART2_LPUART2_IRQHandler(void)
{
    USART_TypeDef* u = USART2;
    while ((u->CR1 & USART_CR1_TXEIE_TXFNFIE) && (u->ISR & USART_ISR_TXE_TXFNF)) {
        int b = ring_get(&s_dbg);
        if (b >= 0) u->TDR = (uint8_t)b;
        else u->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
    }
}

/* ---------------------------------------------------------------- NCN5130 status pins, SW1, D5 */

int hw_saveb(void) { return (GPIOA->IDR & (1u << 0)) ? 1 : 0; }
int hw_resetb(void) { return (GPIOA->IDR & (1u << 1)) ? 1 : 0; }
int hw_button(void) { return (GPIOB->IDR & (1u << 4)) ? 0 : 1; }
void hw_led(int on) { if (on) GPIOB->BSRR = 1u << 5; else GPIOB->BRR = 1u << 5; }

/* ---------------------------------------------------------------- flash (bank 2 data) */

static int flash_wait(void)
{
    uint32_t t = hw_millis();
    while (FLASH->SR & (FLASH_SR_BSY1 | FLASH_SR_BSY2 | FLASH_SR_CFGBSY)) {
        if ((hw_millis() - t) > 200u) return -1;
    }
    return 0;
}

#define FLASH_SR_ERRORS (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                         FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR)

static int flash_unlock(void)
{
    if (flash_wait()) return -1;
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xCDEF89ABu;
    }
    FLASH->SR = FLASH_SR_ERRORS | FLASH_SR_EOP;   /* write 1 to clear */
    return (FLASH->CR & FLASH_CR_LOCK) ? -1 : 0;
}

static void flash_lock(void) { FLASH->CR |= FLASH_CR_LOCK; }

static int in_area(uint32_t addr, uint32_t len, uint32_t base, uint32_t size)
{
    return addr >= base && len <= size && addr - base <= size - len;
}

static int in_data(uint32_t addr, uint32_t len)
{
    return in_area(addr, len, U3_KNX_DATA_BASE, U3_KNX_DATA_SIZE) || in_area(addr, len, U3_STORE_BASE, U3_STORE_SIZE);
}

int hw_flash_erase_page(uint32_t addr)
{
    if (!in_data(addr, U3_PAGE_SIZE) || (addr % U3_PAGE_SIZE)) return -1;
    uint32_t bank2 = 0, page;
    if (FLASH->OPTR & FLASH_OPTR_DUAL_BANK) {
        bank2 = addr >= U3_BANK2_BASE;
        page = (addr - (bank2 ? U3_BANK2_BASE : U3_FLASH_BASE)) / U3_PAGE_SIZE;
    } else {
        page = (addr - U3_FLASH_BASE) / U3_PAGE_SIZE;
    }
    if (flash_unlock()) return -1;
    uint32_t cr = FLASH->CR & ~(FLASH_CR_PNB | FLASH_CR_BKER | FLASH_CR_PG | FLASH_CR_PER | FLASH_CR_MER1);
    cr |= FLASH_CR_PER | ((page << FLASH_CR_PNB_Pos) & FLASH_CR_PNB) | (bank2 ? FLASH_CR_BKER : 0u);
    FLASH->CR = cr;
    FLASH->CR = cr | FLASH_CR_STRT;
    int rc = flash_wait();
    FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_BKER | FLASH_CR_PNB);
    if (FLASH->SR & FLASH_SR_ERRORS) rc = -1;
    flash_lock();
    return rc;
}

int hw_flash_program(uint32_t addr, const uint8_t* data, uint32_t len)
{
    if (!in_data(addr, len) || (addr & 7u) || (len & 7u)) return -1;
    if (flash_unlock()) return -1;
    int rc = 0;
    FLASH->CR |= FLASH_CR_PG;
    for (uint32_t i = 0; i < len && rc == 0; i += 8u) {
        uint32_t w0, w1;
        __builtin_memcpy(&w0, data + i, 4);
        __builtin_memcpy(&w1, data + i + 4u, 4);
        if (w0 == 0xFFFFFFFFu && w1 == 0xFFFFFFFFu) continue;  /* erased already */
        *(volatile uint32_t*)(addr + i) = w0;
        __ISB();
        *(volatile uint32_t*)(addr + i + 4u) = w1;
        rc = flash_wait();
        if (FLASH->SR & FLASH_SR_ERRORS) rc = -1;
        FLASH->SR = FLASH_SR_EOP;
        hw_iwdg_refresh();
    }
    FLASH->CR &= ~FLASH_CR_PG;
    flash_lock();
    return rc;
}

static void erase_area(uint32_t base, uint32_t size)
{
    for (uint32_t a = base; a < base + size; a += U3_PAGE_SIZE) {
        hw_iwdg_refresh();
        hw_flash_erase_page(a);
    }
}

void hw_erase_all_data(void)
{
    erase_area(U3_KNX_DATA_BASE, U3_KNX_DATA_SIZE);
    erase_area(U3_STORE_BASE, U3_STORE_SIZE);
}

static volatile uint8_t s_readArea;

void hw_flash_read_area(uint8_t area) { s_readArea = area; }

int hw_take_store_lost(void)
{
    volatile u3_shared_t* s = U3_SHARED;
    int lost = s->appFlags == ~s->appFlagsInv && (s->appFlags & U3_APPF_STORE_LOST);
    s->appFlags = 0;
    s->appFlagsInv = 0;
    return lost;
}

/* ---------------------------------------------------------------- identity, reset */

uint32_t hw_uid_hash(void)
{
    const volatile uint8_t* p = (const volatile uint8_t*)UID_BASE;
    uint32_t h = 2166136261u;            /* FNV-1a */
    for (int i = 0; i < 12; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

uint8_t hw_reset_cause(void)
{
    volatile u3_shared_t* s = U3_SHARED;
    uint32_t csr = (s->resetCsr == ~s->resetCsrInv) ? s->resetCsr : RCC->CSR;
    uint8_t c = 0;
    if (csr & RCC_CSR_PWRRSTF) c |= HW_RESET_POWER;
    if (csr & RCC_CSR_IWDGRSTF) c |= HW_RESET_IWDG;
    if (csr & RCC_CSR_SFTRSTF) c |= HW_RESET_SOFTWARE;
    if (csr & RCC_CSR_PINRSTF) c |= HW_RESET_PIN;
    if (csr & (RCC_CSR_WWDGRSTF | RCC_CSR_LPWRRSTF | RCC_CSR_OBLRSTF)) c |= HW_RESET_OTHER;
    if (s->bootFlags == ~s->bootFlagsInv && (s->bootFlags & U3_BOOTF_FROM_ROM)) c |= HW_RESET_DFU;
    s->bootFlags = 0;
    s->bootFlagsInv = 0;
    return c;
}

uint16_t hw_boot_version(void)
{
    const u3_boot_desc_t* d = (const u3_boot_desc_t*)(U3_BOOT_BASE + U3_DESC_OFFSET);
    return d->magic == U3_BOOT_DESC_MAGIC ? d->version : 0;
}

void hw_reset(void)
{
    hw_dbg_flush();
    hw_knx_uart_flush();   /* don't cut a TP-UART service in half */
    NVIC_SystemReset();
    for (;;) { }
}

void hw_reset_to_bootloader(void)
{
    hw_dbg_flush();
    hw_knx_uart_flush();
    __disable_irq();
    U3_SHARED->bootReq = U3_BOOTREQ_MAGIC;
    U3_SHARED->bootReqInv = ~U3_BOOTREQ_MAGIC;
    __DSB();
    NVIC_SystemReset();
    for (;;) { }
}

/* ---------------------------------------------------------------- init */

void hw_init(void)
{
    __disable_irq();
    SCB->VTOR = U3_APP_BASE;

    /* SYSCLK = HSISYS = HSI16 / 1, set explicitly rather than trusting the reset state. 0 wait states are fine
     * at 16 MHz. SWD stays enabled (the V9.2 ROM bootloader's Go clears DBG_SWEN). */
    RCC->CR = (RCC->CR & ~RCC_CR_HSIDIV) | RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) { }
    RCC->CFGR &= ~RCC_CFGR_SW;
    while ((RCC->CFGR & RCC_CFGR_SWS) != 0u) { }
    FLASH->ACR |= FLASH_ACR_DBG_SWEN;

    /* Started straight from the ROM bootloader (DFU "leave" at 0x0800 1000 skips the boot manager): its PLL,
     * HSI48, USB or bus prescalers may still be set, and the UART baud rates would be wrong. Reset through the
     * boot manager for a clean start (review G-8, O-9). After any reset these are all zero. */
    if ((RCC->CFGR & (RCC_CFGR_SWS | RCC_CFGR_HPRE | RCC_CFGR_PPRE)) != 0u ||
        (RCC->CR & (RCC_CR_PLLON | RCC_CR_HSI48ON)) != 0u ||
        (RCC->APBENR1 & (RCC_APBENR1_USBEN | RCC_APBENR1_CRSEN)) != 0u) {
        U3_SHARED->bootFlags = U3_BOOTF_FROM_ROM;
        U3_SHARED->bootFlagsInv = ~U3_BOOTF_FROM_ROM;
        NVIC_SystemReset();
    }

    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;

    /* UCPD dead-battery pull-downs off: PA9/PA10 are left open (design.md U3 pin map). */
    SYSCFG->CFGR1 |= SYSCFG_CFGR1_UCPD1_STROBE | SYSCFG_CFGR1_UCPD2_STROBE;

    /* A reset after the ROM bootloader must boot the application, not the bootloader (AN2606). */
    FLASH->ACR &= ~FLASH_ACR_PROGEMPTY;

    /* Unused pins stay analog (reset state). PA11/PA12 (USB, no pull-ups on this board) stay analog too. */
    gpio_input(GPIOA, 0, 0);            /* KNX_SAVEB, R3 */
    gpio_input(GPIOA, 1, 0);            /* KNX_RESETB, R4 */
    gpio_input(GPIOB, 4, 1);            /* SW1, internal pull-up */
    gpio_output(GPIOB, 5);              /* D5 off */

    SysTick->LOAD = HW_SYSCLK_HZ / 1000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    NVIC_SetPriority(SysTick_IRQn, 0);

#if U3_DEBUG_LOG
    dbg_open();
#endif
    __enable_irq();
}

/* ---------------------------------------------------------------- flash ECC */

/* A flash ECC double error raises an NMI (RM0444 3.3.8). It follows an erase or program that was cut short
 * (RM0444 3.3.6), typically by a bus voltage drop during a write.
 * - In bank 2: the data can't be trusted. Erase the area it hit and restart; without this, every boot would read
 *   the bad double word again. The area is the one being read (hw_flash_read_area()). Without a marker, the
 *   address field decides, read both ways RM0444 allows: a double-word offset from 0x0800 0000 (CMSIS masks
 *   15 bits, the whole 256 KB) or from the bank. If neither reading lands in a known area, erase both.
 *   A store error erases only its page (the other one may still hold a good record) and leaves a flag: the
 *   application can't tell whether the newest record was lost, so it starts with top-up off (G-2, owner D1).
 * - In bank 1 (code): ask the boot manager for the ROM bootloader, so the image can be flashed again.
 * ECCC and ECCD are cleared in both registers: while one is set, the address field isn't updated (G-9).
 * SysTick can't preempt the NMI, so the erase's timeout doesn't run; the IWDG covers a hung erase. */
static int area_of(uint32_t addr)
{
    if (in_area(addr, 8u, U3_STORE_BASE, U3_PAGE_SIZE)) return HW_AREA_STORE0;
    if (in_area(addr, 8u, U3_STORE_BASE + U3_PAGE_SIZE, U3_PAGE_SIZE)) return HW_AREA_STORE1;
    if (in_area(addr, 8u, U3_KNX_DATA_BASE, U3_KNX_DATA_SIZE)) return HW_AREA_KNX;
    return HW_AREA_NONE;
}

void NMI_Handler(void)
{
    uint32_t e1 = FLASH->ECCR, e2 = FLASH->ECC2R;
    FLASH->ECCR = FLASH_ECCR_ECCD | FLASH_ECCR_ECCC;
    FLASH->ECC2R = FLASH_ECC2R_ECCD | FLASH_ECC2R_ECCC;
    if (e2 & FLASH_ECC2R_ECCD) {
        int area = s_readArea;
        if (area == HW_AREA_NONE) {
            uint32_t off = (e2 & FLASH_ECC2R_ADDR_ECC) >> FLASH_ECC2R_ADDR_ECC_Pos;
            int a1 = area_of(U3_FLASH_BASE + off * 8u);     /* offset from main flash */
            int a2 = area_of(U3_BANK2_BASE + off * 8u);     /* offset from bank 2 */
            area = a1 != HW_AREA_NONE ? a1 : a2;
        }
        if (area == HW_AREA_KNX || area == HW_AREA_NONE) erase_area(U3_KNX_DATA_BASE, U3_KNX_DATA_SIZE);
        if (area != HW_AREA_KNX) {
            if (area == HW_AREA_STORE0 || area == HW_AREA_NONE) hw_flash_erase_page(U3_STORE_BASE);
            if (area == HW_AREA_STORE1 || area == HW_AREA_NONE) hw_flash_erase_page(U3_STORE_BASE + U3_PAGE_SIZE);
            U3_SHARED->appFlags = U3_APPF_STORE_LOST;
            U3_SHARED->appFlagsInv = ~U3_APPF_STORE_LOST;
        }
        NVIC_SystemReset();
    }
    if (e1 & FLASH_ECCR_ECCD) {
        U3_SHARED->bootReq = U3_BOOTREQ_MAGIC;
        U3_SHARED->bootReqInv = ~U3_BOOTREQ_MAGIC;
    }
    NVIC_SystemReset();
}

/* ---------------------------------------------------------------- newlib support */

extern uint8_t end;                     /* linker: start of heap */
extern uint32_t _estack;
extern uint32_t _Min_Stack_Size;

void* _sbrk(ptrdiff_t incr)
{
    static uint8_t* heap = 0;
    if (!heap) heap = &end;
    uint8_t* limit = (uint8_t*)&_estack - (uint32_t)&_Min_Stack_Size;
    if (heap + incr > limit) return (void*)-1;
    uint8_t* prev = heap;
    heap += incr;
    return prev;
}
