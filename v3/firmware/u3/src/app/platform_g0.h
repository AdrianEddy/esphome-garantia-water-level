/* platform_g0.h: thelsing/knx Platform for U3 (STM32G0B1, bare metal, no Arduino); knx-rgbic U9 platform. */
#pragma once

#include "knx/platform.h"

class G0Platform : public Platform
{
  public:
    G0Platform();

    uint32_t uniqueSerialNumber() override;
    void restart() override;
    void fatalError() override;

    // UART to the NCN5130 (USART1)
    void setupUart() override;
    void closeUart() override;
    int uartAvailable() override;
    size_t writeUart(const uint8_t data) override;
    size_t writeUart(const uint8_t* buffer, size_t size) override;
    int readUart() override;
    size_t readBytesUart(uint8_t* buffer, size_t length) override;
    bool overflowUart() override;
    bool uartByteError() override;
    void flushUart() override;

    // Non-volatile memory: a 4 KB RAM image of bank 2 (thelsing's Eeprom type). Every table lives in RAM, so
    // a commit cut short by a bus drop never leaves the running stack pointing at half-erased flash, and
    // flash is only read once, at start-up. commitToEeprom() writes back only the 2 KB pages that changed.
    uint8_t* getEepromBuffer(uint32_t size) override;
    void commitToEeprom() override;
};
