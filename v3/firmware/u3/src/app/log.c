/* log.c: see log.h */
#include "log.h"

#if U3_DEBUG_LOG
#include "hw.h"

void log_str(const char* s)
{
    const char* e = s;
    while (*e) e++;
    hw_dbg_write(s, (size_t)(e - s));
}

void log_u32(uint32_t v)
{
    char b[10];
    int n = 0;
    do {
        b[sizeof b - 1 - n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v && n < (int)sizeof b);
    hw_dbg_write(b + sizeof b - n, (size_t)n);
}

void log_hex(uint32_t v, uint8_t digits)
{
    char b[8];
    if (digits > 8) digits = 8;
    for (int i = digits - 1; i >= 0; i--) {
        b[i] = "0123456789ABCDEF"[v & 15u];
        v >>= 4;
    }
    hw_dbg_write(b, digits);
}

void log_fix(int32_t v, uint8_t decimals)
{
    uint32_t div = 1;
    for (uint8_t i = 0; i < decimals; i++) div *= 10u;
    if (v < 0) {
        log_str("-");
        v = -v;
    }
    log_u32((uint32_t)v / div);
    if (!decimals) return;
    log_str(".");
    uint32_t frac = (uint32_t)v % div;
    for (uint32_t d = div / 10u; d; d /= 10u) {
        char c = (char)('0' + (frac / d) % 10u);
        hw_dbg_write(&c, 1);
    }
}
#endif
