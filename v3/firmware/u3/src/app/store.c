/* store.c: see store.h
 *
 * Invariant: the page holding the newest committed record (s_page) is never erased. Writes go to the write
 * cursor (s_wpage, s_wnext); when it has no erased slot left, the cursor moves to the other page, which holds
 * only older records, and that page is erased first. A slot whose write failed is skipped, never reused. */
#include "store.h"
#include "hw.h"
#include "../shared/u3_shared.h"
#include <stddef.h>
#include <string.h>

#define STORE_MAGIC     0x4C414347u     /* "GCAL" */
#define PAGES           (U3_STORE_SIZE / U3_PAGE_SIZE)
#define SLOTS_PER_PAGE  (U3_PAGE_SIZE / sizeof(store_rec_t))
#define PAYLOAD_OFF     offsetof(store_rec_t, fEmpty)
#define PAYLOAD_LEN     (offsetof(store_rec_t, crc) - PAYLOAD_OFF)

_Static_assert(sizeof(store_rec_t) == 32, "record must be a multiple of the 8-byte flash double word");
_Static_assert(PAGES == 2, "the store alternates between two pages");

static store_rec_t s_last;           /* newest record stored (valid if s_haveLast) */
static int s_haveLast;
static uint32_t s_page;              /* page holding s_last */
static uint32_t s_wpage, s_wnext;    /* write cursor; s_wnext == SLOTS_PER_PAGE: no slot left */

static uint32_t page_base(uint32_t p) { return U3_STORE_BASE + p * U3_PAGE_SIZE; }
static const store_rec_t* slot(uint32_t p, uint32_t i)
{
    return (const store_rec_t*)(page_base(p) + i * sizeof(store_rec_t));
}

/* Every read of a store page is marked, so an ECC double error erases that page only (hw.c NMI). */
static void reading(uint32_t p) { hw_flash_read_area((uint8_t)(HW_AREA_STORE0 + p)); }
static void done(void) { hw_flash_read_area(HW_AREA_NONE); }

static int erased(const store_rec_t* r)
{
    const uint32_t* w = (const uint32_t*)r;
    for (uint32_t i = 0; i < sizeof *r / 4; i++)
        if (w[i] != 0xFFFFFFFFu) return 0;
    return 1;
}

static int valid(const store_rec_t* r)
{
    return r->magic == STORE_MAGIC && r->crc == u3_crc32((const uint8_t*)r, offsetof(store_rec_t, crc));
}

/* a is newer than b, wrap-safe. */
static int newer(uint32_t a, uint32_t b) { return a - b - 1u < 0x7FFFFFFFu; }

int store_load(store_rec_t* out)
{
    uint32_t lastIdx = 0;
    s_haveLast = 0;
    s_page = 0;
    for (uint32_t p = 0; p < PAGES; p++) {
        reading(p);
        for (uint32_t i = 0; i < SLOTS_PER_PAGE; i++) {
            const store_rec_t* r = slot(p, i);
            if (valid(r) && (!s_haveLast || newer(r->seq, s_last.seq))) {
                memcpy(&s_last, r, sizeof s_last);
                s_haveLast = 1;
                s_page = p;
                lastIdx = i;
            }
        }
        done();
    }
    /* Write cursor: the first erased slot after the newest record. Nothing stored: page 0 from the start (it
     * gets erased first if it isn't blank). */
    s_wpage = s_page;
    s_wnext = s_haveLast ? SLOTS_PER_PAGE : 0u;
    if (s_haveLast) {
        reading(s_page);
        for (uint32_t j = lastIdx + 1u; j < SLOTS_PER_PAGE; j++)
            if (erased(slot(s_page, j))) {
                s_wnext = j;
                break;
            }
        done();
    }
    if (s_haveLast) memcpy(out, &s_last, sizeof *out);
    return s_haveLast;
}

int store_save(const store_rec_t* in)
{
    if (s_haveLast && memcmp((const uint8_t*)in + PAYLOAD_OFF, (const uint8_t*)&s_last + PAYLOAD_OFF, PAYLOAD_LEN) == 0)
        return 0;   /* unchanged: no write (wear, review G-11, O-6) */

    store_rec_t r;
    memcpy(&r, in, sizeof r);
    r.magic = STORE_MAGIC;
    r.seq = s_haveLast ? s_last.seq + 1u : 1u;
    r.crc = u3_crc32((const uint8_t*)&r, offsetof(store_rec_t, crc));

    int fresh = 0;
    if (s_wnext < SLOTS_PER_PAGE) {
        reading(s_wpage);
        fresh = erased(slot(s_wpage, s_wnext));
        done();
    }
    if (!fresh) {
        s_wpage = s_haveLast ? 1u - s_page : 0u;   /* never the page holding the newest record */
        s_wnext = 0;
        if (hw_flash_erase_page(page_base(s_wpage)) != 0) {
            s_wnext = SLOTS_PER_PAGE;               /* not erased: erase again next time */
            return -1;
        }
    }
    uint32_t page = s_wpage, addr = (uint32_t)slot(page, s_wnext);
    s_wnext++;                                      /* the slot is used, whatever happens next */
    int rc = hw_flash_program(addr, (const uint8_t*)&r, sizeof r);
    reading(page);
    if (rc == 0 && memcmp((const void*)addr, &r, sizeof r) != 0) rc = -1;
    done();
    if (rc != 0) return -1;
    memcpy(&s_last, &r, sizeof s_last);
    s_haveLast = 1;
    s_page = page;
    return 0;
}
