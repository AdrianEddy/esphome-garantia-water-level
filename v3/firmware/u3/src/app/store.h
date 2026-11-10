/*
 * store.h: run-time settings kept in flash (bank 2, two pages at U3_STORE_BASE), apart from the ETS download.
 *
 * Holds what Home Assistant or the top-up logic change at run time: the two calibration frequencies, the top-up
 * enable, the top-up fault lock and the open fill session. Records are appended to one page; when it is full
 * the next record starts the other page, which is erased first. So the newest committed record survives a
 * power cut at any point: the page that holds it is never the one being erased (review G-2, O-6). A record cut
 * short is caught by its CRC, or by the ECC NMI (hw.c), which erases the store and tells the application.
 * The newest valid record (by sequence number) wins.
 */
#ifndef STORE_H
#define STORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t magic;
    uint32_t seq;
    float    fEmpty;            /* Hz */
    float    fFull;             /* Hz */
    uint16_t etsEmpty;          /* the ETS parameters (0.1 Hz) these frequencies replace */
    uint16_t etsFull;
    uint8_t  topupEnabled;      /* object KO_TOPUP_ENABLE */
    uint8_t  topupLocked;       /* top-up fault lock, until re-enabled */
    uint8_t  fillActive;        /* a fill session was open */
    uint8_t  reserved0;
    uint16_t fillUsedS;         /* its opening time so far (s) */
    uint16_t reserved1;
    uint32_t crc;               /* CRC-32 of the bytes before it */
} store_rec_t;

int  store_load(store_rec_t* out);         /* 1 = a valid record was found */
/* Writes the record if its content (everything but magic, seq and crc) differs from the newest one stored;
 * sets magic, seq and crc itself. 0 = stored or unchanged, -1 = the write failed (try again later). */
int  store_save(const store_rec_t* in);

#ifdef __cplusplus
}
#endif

#endif /* STORE_H */
