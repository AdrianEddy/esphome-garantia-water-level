/* meter.c: see meter.h */
#include "meter.h"
#include "hw.h"
#include "stm32g0xx.h"

#define RING_SIZE      512u              /* power of two; about 2.8 s of pulses at 180 Hz */
#define PROBE_MS       10000u            /* in HSI mode: try XCLK this often */
#define RATE_MIN_PPM   950000u           /* XCLK/HSI16 ratio window, see meter.h */
#define RATE_MAX_PPM   1050000u

static volatile uint32_t s_ring[RING_SIZE];
static volatile uint16_t s_head, s_tail;
static volatile uint32_t s_last;
static volatile uint8_t s_haveLast;
static volatile uint32_t s_lastEdgeMs;
static volatile uint32_t s_edges;
static volatile uint16_t s_lost;

static uint8_t s_mode;
static uint32_t s_blockStartMs, s_blockStartCnt;
static uint8_t s_saveLowSeen;
static uint32_t s_nextProbeMs;
static meter_block_t s_block;

void TIM2_IRQHandler(void)
{
    if (!(TIM2->SR & TIM_SR_CC2IF)) {
        TIM2->SR = ~(TIM_SR_CC2OF | TIM_SR_UIF);
        return;
    }
    /* The data first, then the overcapture flag (RM0444 input capture mode), so an overcapture between the two
     * reads isn't missed (review F-06). Reading CCR2 clears CC2IF. */
    uint32_t c = TIM2->CCR2;
    int lost = (TIM2->SR & TIM_SR_CC2OF) != 0;   /* a capture was overwritten: the period to c is two or more */
    if (lost) {
        TIM2->SR = ~TIM_SR_CC2OF;
        s_lost++;
    } else if (s_haveLast) {
        uint16_t h = s_head, n = (uint16_t)((h + 1u) & (RING_SIZE - 1u));
        if (n != s_tail) {
            s_ring[h] = c - s_last;        /* 32-bit counter: the wrap is harmless */
            s_head = n;
        } else {
            s_lost++;
        }
    }
    s_last = c;
    s_haveLast = 1;
    s_lastEdgeMs = hw_millis();
    s_edges++;
}

/* Called with the TIM2 interrupt disabled. UG loads the prescaler and clears the counter. */
static void set_mode(uint8_t m)
{
    if (m == METER_SRC_XCLK) {
        TIM2->SMCR = TIM_SMCR_ECE | (2u << TIM_SMCR_ETPS_Pos);   /* external clock mode 2, ETR / 4 */
        TIM2->PSC = 0;
    } else {
        TIM2->SMCR = 0;                                         /* internal clock, 16 MHz / 8 */
        TIM2->PSC = 7;
    }
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = ~(TIM_SR_CC2IF | TIM_SR_CC2OF | TIM_SR_UIF);
    s_haveLast = 0;
    s_mode = m;
}

static void pin_af2(GPIO_TypeDef* g, uint32_t pin)
{
    g->PUPDR &= ~(3u << (pin * 2u));
    volatile uint32_t* afr = &g->AFR[pin >> 3];
    *afr = (*afr & ~(0xFu << ((pin & 7u) * 4u))) | (2u << ((pin & 7u) * 4u));
    g->MODER = (g->MODER & ~(3u << (pin * 2u))) | (2u << (pin * 2u));
}

void meter_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
    RCC->APBENR1 |= RCC_APBENR1_TIM2EN;
    pin_af2(GPIOA, 15);                    /* TIM2_ETR <- KNX_XCLK (driven by U2 through R17) */
    pin_af2(GPIOB, 3);                     /* TIM2_CH2 <- MCU_PULSE (R7 pull-up) */

    TIM2->CR1 = 0;
    TIM2->ARR = 0xFFFFFFFFu;
    /* IC2 on TI2, filter fDTS/32 with N = 8: an edge must hold 16 us. MCU_PULSE has a 10 us RC (R7, C17). */
    TIM2->CCMR1 = (1u << TIM_CCMR1_CC2S_Pos) | (0xFu << TIM_CCMR1_IC2F_Pos);
    TIM2->CCER = TIM_CCER_CC2P | TIM_CCER_CC2E;   /* falling edge: the optocoupler turning on */
    set_mode(METER_SRC_HSI);               /* the NCN may still be starting; the first block tries XCLK */
    uint32_t now = hw_millis();
    s_blockStartMs = now;
    s_blockStartCnt = 0;
    s_nextProbeMs = now;
    s_saveLowSeen = !hw_saveb();
    TIM2->DIER = TIM_DIER_CC2IE;
    TIM2->CR1 = TIM_CR1_CEN;
    NVIC_SetPriority(TIM2_IRQn, 2);        /* below the KNX UART (1): ACK decisions come first */
    NVIC_EnableIRQ(TIM2_IRQn);
}

const meter_block_t* meter_poll(uint32_t now)
{
    if (!hw_saveb()) s_saveLowSeen = 1;
    if ((now - s_blockStartMs) < METER_BLOCK_MS) return 0;

    NVIC_DisableIRQ(TIM2_IRQn);
    /* Time and count read together: `now` may be from before a long main-loop pass (an NCN reset, an ETS
     * commit), which would skew the ratio (review F-05, O-11). */
    now = hw_millis();
    uint32_t cnt = TIM2->CNT;
    uint16_t n = 0, dropped = s_lost;
    s_lost = 0;
    while (s_tail != s_head) {
        uint16_t t = s_tail;
        uint32_t p = s_ring[t];
        s_tail = (uint16_t)((t + 1u) & (RING_SIZE - 1u));
        if (n < METER_MAX_PERIODS) s_block.period[n++] = p;
        else dropped++;
    }
    uint32_t elapsed = now - s_blockStartMs;
    uint32_t delta = cnt - s_blockStartCnt;
    uint64_t expected = (uint64_t)elapsed * (METER_TICK_HZ / 1000u);
    uint32_t ppm = expected ? (uint32_t)((uint64_t)delta * 1000000u / expected) : 0u;
    uint8_t src = s_mode, next = s_mode, valid;
    if (src == METER_SRC_XCLK) {
        valid = !s_saveLowSeen && ppm >= RATE_MIN_PPM && ppm <= RATE_MAX_PPM;
        if (!valid) {
            next = METER_SRC_HSI;
            s_nextProbeMs = now + PROBE_MS;
        }
    } else {
        valid = 1;
        if (!s_saveLowSeen && (int32_t)(now - s_nextProbeMs) >= 0) {
            next = METER_SRC_XCLK;
            s_nextProbeMs = now + PROBE_MS;
        }
    }
    if (next != s_mode) {
        set_mode(next);
        cnt = 0;
    }
    s_blockStartCnt = cnt;
    s_blockStartMs = now;
    s_saveLowSeen = !hw_saveb();
    NVIC_EnableIRQ(TIM2_IRQn);

    s_block.count = n;
    s_block.dropped = dropped;
    s_block.valid = valid;
    s_block.source = src;
    s_block.ratePpm = ppm;
    return &s_block;
}

uint32_t meter_last_edge_ms(void) { return s_lastEdgeMs; }
uint32_t meter_edges(void) { return s_edges; }
uint8_t meter_source(void) { return s_mode; }
