/* log.h: text log on TP7 (USART2_TX, 115200 8N1) for bring-up; compiled out with U3_DEBUG_LOG=0.
 * Connect an isolated USB-UART, or power the board from a floating bench supply: TP5 is GND_KNX = KNX-. */
#ifndef LOG_H
#define LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if U3_DEBUG_LOG
void log_str(const char* s);
void log_u32(uint32_t v);
void log_hex(uint32_t v, uint8_t digits);
void log_fix(int32_t v, uint8_t decimals);   /* v / 10^decimals, e.g. log_fix(4321, 2) -> "43.21" */
#else
static inline void log_str(const char* s) { (void)s; }
static inline void log_u32(uint32_t v) { (void)v; }
static inline void log_hex(uint32_t v, uint8_t digits) { (void)v; (void)digits; }
static inline void log_fix(int32_t v, uint8_t decimals) { (void)v; (void)decimals; }
#endif

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
