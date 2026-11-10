/*
 * u3_shared.h: memory map and handover between U3's boot manager and application (STM32G0B1KC, 256 KB).
 * Same layout as knx-rgbic's U9 (knx-device-playbook "STM32G0B1 specifics and memory map"), plus one store page.
 *
 * Flash, dual-bank mode (factory DUAL_BANK = 1):
 *   bank 1  0x0800 0000  4 KB   boot manager (pages 0-1), never rewritten by the application
 *           0x0800 1000  124 KB application (pages 2-63); all code runs from bank 1 (ES0548 2.2.10)
 *   bank 2  0x0802 0000  4 KB   KNX ETS download (a 4 KB RAM image committed here), data only
 *           0x0802 1000  4 KB   store: two 2 KB pages (A/B) of calibration and top-up state (store.c)
 * A DFU image covers bank 1 only, so flashing never erases bank 2 (the V9.2 ROM bootloader can't erase
 * several bank-2 pages in one command, design.md "Firmware notes").
 * If the option bytes were ever set to single-bank, the same addresses are bank 1 pages 64+; the flash
 * driver reads FLASH_OPTR.DUAL_BANK and addresses pages accordingly.
 *
 * RAM: 144 KB at 0x2000 0000. The top 256 bytes are a no-init area that both images exclude, and that the ST
 * ROM bootloader doesn't touch (it uses the first 16 KB, AN2606): 224 bytes reserved for the application,
 * then a 32-byte handover block (u3_shared_t).
 */
#ifndef U3_SHARED_H
#define U3_SHARED_H

#include <stdint.h>

#define U3_FLASH_BASE         0x08000000u
#define U3_BOOT_BASE          0x08000000u
#define U3_BOOT_SIZE          0x00001000u
#define U3_APP_BASE           0x08001000u
#define U3_APP_MAX_SIZE       0x0001F000u          /* up to the end of bank 1 */
#define U3_BANK2_BASE         0x08020000u
#define U3_KNX_DATA_BASE      0x08020000u
#define U3_KNX_DATA_SIZE      0x00001000u          /* = KNX_FLASH_SIZE */
#define U3_STORE_BASE         0x08021000u
#define U3_STORE_SIZE         0x00001000u          /* two pages, written alternately */
#define U3_PAGE_SIZE          2048u
#define U3_SYSMEM_BASE        0x1FFF0000u          /* ST ROM bootloader (USB DFU) */

#define U3_RAM_BASE           0x20000000u
#define U3_RAM_SIZE           0x00024000u          /* 144 KB */
#define U3_NOINIT_ADDR        (U3_RAM_BASE + U3_RAM_SIZE - 256u)   /* 0x2002 3F00, see the linker scripts */
#define U3_PERSIST_ADDR       U3_NOINIT_ADDR       /* 224 bytes, reserved for the application */
#define U3_PERSIST_SIZE       224u
#define U3_SHARED_ADDR        (U3_RAM_BASE + U3_RAM_SIZE - 32u)    /* 0x2002 3FE0 */

/* Descriptors sit right after each image's vector table (48 words = 0xC0 bytes on the G0B1). */
#define U3_DESC_OFFSET        0x100u
#define U3_BOOT_DESC_MAGIC    0x4D423355u          /* "U3BM" */
#define U3_APP_DESC_MAGIC     0x50413355u          /* "U3AP" */

typedef struct {
    uint32_t magic;            /* U3_BOOT_DESC_MAGIC */
    uint16_t version;          /* boot manager version */
    uint16_t reserved;
} u3_boot_desc_t;

typedef struct {
    uint32_t magic;            /* U3_APP_DESC_MAGIC */
    uint16_t fwVersion;        /* 0xMMmm */
    uint16_t reserved0;
    uint32_t imageSize;        /* bytes from U3_APP_BASE, multiple of 8; filled in by tools/post_app.py */
    uint32_t crc32;            /* zlib CRC-32 over imageSize bytes with this field read as 0; post_app.py */
    uint32_t reserved[4];
} u3_app_desc_t;

#define U3_APP_DESC_CRC_OFFSET  (U3_DESC_OFFSET + 12u)

/* Handover block in RAM, survives a system reset (not a power-on reset). */
typedef struct {
    uint32_t bootReq;          /* U3_BOOTREQ_MAGIC: the application asks for the ROM bootloader (USB DFU) */
    uint32_t bootReqInv;       /* ~U3_BOOTREQ_MAGIC */
    uint32_t resetCsr;         /* RCC_CSR as the boot manager found it (it then clears the flags) */
    uint32_t resetCsrInv;
    uint32_t bootFlags;        /* U3_BOOTF_*: what the boot manager saw at the last start */
    uint32_t bootFlagsInv;
    uint32_t appFlags;         /* U3_APPF_*: set by the application's NMI before its reset */
    uint32_t appFlagsInv;
} u3_shared_t;

#define U3_SHARED             ((volatile u3_shared_t*)U3_SHARED_ADDR)
#define U3_BOOTREQ_MAGIC      0xB0071EADu
#define U3_BOOTF_FROM_ROM     0x01u                /* arrived from the ROM bootloader (DFU leave): reset once */
#define U3_APPF_STORE_LOST    0x01u                /* a flash ECC error erased the store: start with top-up off */

/* zlib CRC-32, nibble table: small enough for the boot manager */
static inline uint32_t u3_crc32_update(uint32_t crc, uint8_t b)
{
    static const uint32_t t[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    crc ^= b;
    crc = (crc >> 4) ^ t[crc & 15u];
    crc = (crc >> 4) ^ t[crc & 15u];
    return crc;
}

static inline uint32_t u3_crc32(const uint8_t* p, uint32_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) crc = u3_crc32_update(crc, p[i]);
    return ~crc;
}

/* CRC of an application image in flash, with the descriptor's crc32 field read as zero. */
static inline uint32_t u3_app_crc(const uint8_t* base, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < size; i++) {
        uint8_t b = (i >= U3_APP_DESC_CRC_OFFSET && i < U3_APP_DESC_CRC_OFFSET + 4u) ? 0u : base[i];
        crc = u3_crc32_update(crc, b);
    }
    return ~crc;
}

#endif /* U3_SHARED_H */
