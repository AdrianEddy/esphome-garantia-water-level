/*
 * hw.h: U3 (STM32G0B1KCU6) hardware layer for the application. Based on knx-rgbic's U9 hw layer.
 *
 * Pins (design.md "U3 pin map")
 *   PB6  USART1_TX (AF0) -> KNX_RXD -> NCN5130 SDI/RXD (R5 pull-up)
 *   PB7  USART1_RX (AF0) <- KNX_TXD (NCN5130 SDO/TXD)
 *   PA0  KNX_SAVEB (input, R3 pull-up), PA1 KNX_RESETB (input, R4 pull-up)
 *   PA2  USART2_TX (AF1) -> TP7, debug log, 115200 8N1
 *   PA15 TIM2_ETR (AF2) <- KNX_XCLK (NCN5130 XCLK, 8 MHz, through R17)
 *   PB3  TIM2_CH2 (AF2) <- MCU_PULSE (U4 collector, R7 pull-up, C17)
 *   PB4  MCU_BTN, SW1 to GND_KNX (internal pull-up); PB5 MCU_LED_PROG -> R6 -> D5
 *   PA11/PA12 USB (ROM DFU only), PA13/PA14 SWD; everything else is unconnected and stays analog.
 * Clock: HSI16, no PLL. KNX timing comes from the NCN's crystal; so does the sensor timing (TIM2 on XCLK).
 */
#ifndef HW_H
#define HW_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HW_SYSCLK_HZ     16000000u
#define HW_KNX_BAUD      19200u      /* NCN5130 UC1/UC2 strapped low: 19200 baud, 9-bit (8E1) */
#define HW_DBG_BAUD      115200u

void     hw_init(void);
uint32_t hw_millis(void);
void     hw_delay_us(uint32_t us);

/* Independent watchdog, about 2 s. */
void     hw_iwdg_start(void);
void     hw_iwdg_refresh(void);

/* USART1: NCN5130. RX bytes are buffered by the interrupt, which then calls the hook (thelsing's ISR path). */
void     hw_knx_uart_open(void);
void     hw_knx_uart_close(void);
int      hw_knx_uart_read(void);                 /* -1 if empty */
size_t   hw_knx_uart_available(void);
size_t   hw_knx_uart_write(const uint8_t* p, size_t n);  /* interrupt-safe, all or nothing */
int      hw_knx_uart_overflow(void);             /* returns and clears the overflow flag */
void     hw_knx_uart_flush(void);                /* waits until everything is on the wire */
uint32_t hw_knx_uart_errors(void);               /* parity, framing, noise (counted, bytes still delivered) */
int      hw_knx_uart_last_error(void);           /* 1 if the byte last read had a parity, framing or noise error */
typedef void (*hw_isr_hook_t)(void);
void     hw_knx_uart_set_rx_hook(hw_isr_hook_t hook);

/* USART2 TX: debug log on TP7. Drops what doesn't fit; never blocks. */
void     hw_dbg_write(const char* s, size_t n);
void     hw_dbg_flush(void);

/* Inputs from the NCN5130 (through R3/R4 pull-ups) */
int      hw_saveb(void);                         /* 1 = high (bus OK, NCN in Normal state) */
int      hw_resetb(void);

/* SW1 and D5 */
int      hw_button(void);                        /* 1 = pressed */
void     hw_led(int on);

/* Flash, bank 2 data only: the KNX area (U3_KNX_DATA_*) and the store pages (U3_STORE_*). */
int      hw_flash_erase_page(uint32_t addr);     /* 0 = ok */
int      hw_flash_program(uint32_t addr, const uint8_t* data, uint32_t len);  /* addr and len multiple of 8 */
void     hw_erase_all_data(void);                /* factory reset: KNX area and store */
/* Mark the bank-2 area about to be read (HW_AREA_NONE when done). A flash ECC double error during the read
 * erases that area, whatever FLASH_ECC2R's address field means (review F-02, O-4, G-9). */
#define HW_AREA_NONE   0u
#define HW_AREA_KNX    1u
#define HW_AREA_STORE0 2u                         /* store page 0; page 1 is HW_AREA_STORE0 + 1 */
#define HW_AREA_STORE1 3u
void     hw_flash_read_area(uint8_t area);
int      hw_take_store_lost(void);               /* 1 once if the ECC NMI erased the store before this start */

uint32_t hw_uid_hash(void);                      /* 32-bit digest of the 96-bit unique ID */

/* Reset cause as found by the boot manager */
#define HW_RESET_POWER    0x01u
#define HW_RESET_IWDG     0x02u
#define HW_RESET_SOFTWARE 0x04u
#define HW_RESET_PIN      0x08u
#define HW_RESET_OTHER    0x10u
#define HW_RESET_DFU      0x20u                  /* the boot manager came from the ROM bootloader (DFU leave) */
uint8_t  hw_reset_cause(void);
uint16_t hw_boot_version(void);                  /* 0 if no boot manager descriptor */

void     hw_reset(void) __attribute__((noreturn));
void     hw_reset_to_bootloader(void) __attribute__((noreturn));  /* via the boot manager: USB DFU */

#ifdef __cplusplus
}
#endif

#endif /* HW_H */
