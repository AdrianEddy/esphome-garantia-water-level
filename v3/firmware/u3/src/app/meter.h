/*
 * meter.h: sensor period measurement with TIM2 (design.md "Firmware notes", "Key choices").
 *
 * TIM2 is a 32-bit counter at 2 MHz in either clock mode:
 * - XCLK: external clock mode 2 from ETR (PA15) = the NCN5130's XCLK, 8 MHz from its crystal in Normal state,
 *   divided by 4 in the ETR prescaler (ETRP must stay below f_TIM / 4 = 4 MHz);
 * - HSI: the internal 16 MHz clock divided by 8 (HSI16 is +-0.75 % at 30 degC, -2...+1.5 % over -40...125 degC).
 * Input capture on CH2 (PB3, MCU_PULSE) on falling edges only: the optocoupler turns on fast and off
 * 100-200 us slower, so only like edges give clean periods. The digital input filter ignores pulses
 * shorter than 16 us.
 *
 * Captured periods are collected in 1-second blocks. At the end of each block the counter's advance is
 * compared with the elapsed SysTick time:
 * - in XCLK mode the block counts only while SAVEB stayed high (NCN Normal state) and the XCLK/HSI16 ratio
 *   is within +-5 % of nominal. Not tighter: HSI16's own tolerance reaches -2.75 %
 *   at -40 degC, and the failure to catch is XCLK at the 4 MHz RC (-50 %). A failed block is discarded and
 *   the meter falls back to HSI;
 * - in HSI mode, blocks count with reduced accuracy; every 10 s, with SAVEB high, one block is tried on XCLK.
 * A capture spanning a mode switch or a lost capture (overcapture) is discarded.
 */
#ifndef METER_H
#define METER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METER_TICK_HZ      2000000u       /* counter rate in both modes */
#define METER_BLOCK_MS     1000u
#define METER_MAX_PERIODS  512u           /* per block; more means noise, the rest is dropped */

enum { METER_SRC_HSI = 0, METER_SRC_XCLK = 1 };

typedef struct {
    uint32_t period[METER_MAX_PERIODS];   /* periods in ticks, in capture order */
    uint16_t count;                       /* periods in this block */
    uint16_t dropped;                     /* lost captures or periods beyond METER_MAX_PERIODS */
    uint8_t  valid;                       /* the block's timing can be trusted (see above) */
    uint8_t  source;                      /* METER_SRC_* the periods were timed with */
    uint32_t ratePpm;                     /* counter advance / nominal, in ppm (1000000 = nominal) */
} meter_block_t;

void     meter_init(void);
/* Call every main-loop pass. Returns a completed block about once per second, otherwise NULL. */
const meter_block_t* meter_poll(uint32_t now);
uint32_t meter_last_edge_ms(void);        /* SysTick time of the last capture (any) */
uint32_t meter_edges(void);               /* captures since start */
uint8_t  meter_source(void);              /* current mode */

#ifdef __cplusplus
}
#endif

#endif /* METER_H */
