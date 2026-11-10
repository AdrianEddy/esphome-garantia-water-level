/* platform_g0.cpp: see platform_g0.h */
#include "platform_g0.h"
#include "knx/bits.h"
#include "hw.h"
#include "../shared/u3_shared.h"
#include <string.h>

static_assert(KNX_FLASH_SIZE == U3_KNX_DATA_SIZE, "KNX_FLASH_SIZE must match the bank-2 data area");

G0Platform::G0Platform()
{
    _memoryType = Eeprom;
}

uint32_t G0Platform::uniqueSerialNumber() { return hw_uid_hash(); }

void G0Platform::restart() { hw_reset(); }

void G0Platform::fatalError()
{
    // Nothing sensible to continue with: restart through the boot manager. The ETS download in bank 2
    // stays, so a transient fault doesn't look like a factory reset.
    hw_reset();
}

// ---- UART ----
void G0Platform::setupUart() { hw_knx_uart_open(); }
void G0Platform::closeUart() { hw_knx_uart_close(); }
int G0Platform::uartAvailable() { return (int)hw_knx_uart_available(); }
size_t G0Platform::writeUart(const uint8_t data) { return hw_knx_uart_write(&data, 1); }
size_t G0Platform::writeUart(const uint8_t* buffer, size_t size) { return hw_knx_uart_write(buffer, size); }
int G0Platform::readUart() { return hw_knx_uart_read(); }

size_t G0Platform::readBytesUart(uint8_t* buffer, size_t length)
{
    size_t n = 0;
    uint32_t t = hw_millis();
    while (n < length && (hw_millis() - t) < 50u) {
        int b = hw_knx_uart_read();
        if (b >= 0) buffer[n++] = (uint8_t)b;
    }
    return n;
}

bool G0Platform::overflowUart() { return hw_knx_uart_overflow() != 0; }
bool G0Platform::uartByteError() { return hw_knx_uart_last_error() != 0; }
void G0Platform::flushUart() { hw_knx_uart_flush(); }

// ---- non-volatile memory ----
alignas(8) static uint8_t s_ee[U3_KNX_DATA_SIZE];
static bool s_eeLoaded;

uint8_t* G0Platform::getEepromBuffer(uint32_t size)
{
    if (size > sizeof s_ee) fatalError();
    if (!s_eeLoaded) {
        // A flash ECC double error here (a commit cut short) raises the NMI, which erases the ETS area (hw.c).
        hw_flash_read_area(HW_AREA_KNX);
        memcpy(s_ee, (const void*)U3_KNX_DATA_BASE, sizeof s_ee);
        hw_flash_read_area(HW_AREA_NONE);
        s_eeLoaded = true;
    }
    return s_ee;
}

void G0Platform::commitToEeprom()
{
    if (!s_eeLoaded) return;
    for (uint32_t off = 0; off < sizeof s_ee; off += U3_PAGE_SIZE) {
        uint32_t addr = U3_KNX_DATA_BASE + off;
        // ETS often rewrites unchanged data: skip identical pages (wear, and ~22 ms per erase)
        hw_flash_read_area(HW_AREA_KNX);
        bool same = memcmp((const void*)addr, s_ee + off, U3_PAGE_SIZE) == 0;
        hw_flash_read_area(HW_AREA_NONE);
        if (same) continue;
        hw_iwdg_refresh();
        if (hw_flash_erase_page(addr) != 0) fatalError();
        if (hw_flash_program(addr, s_ee + off, U3_PAGE_SIZE) != 0) fatalError();
    }
}

// ---- free functions thelsing expects on non-Arduino platforms (bits.h) ----
uint32_t millis() { return hw_millis(); }

void delay(uint32_t ms)
{
    uint32_t t = hw_millis();
    while ((hw_millis() - t) < ms) hw_iwdg_refresh();
}

void delayMicroseconds(unsigned int us) { hw_delay_us(us); }
void pinMode(uint32_t, uint32_t) {}
void digitalWrite(uint32_t, uint32_t) {}
uint32_t digitalRead(uint32_t) { return 0; }
void attachInterrupt(uint32_t, voidFuncPtr, uint32_t) {}
