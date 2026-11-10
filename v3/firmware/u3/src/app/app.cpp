/*
 * app.cpp: the U3 application (design.md "Firmware notes", "KNX application").
 *
 * U3 is the KNX device: thelsing/knx's Bau07B0 on the NCN5130 (USART1), set up as on knx-rgbic's U9.
 *
 * Measurement (meter.c): TIM2 times the optocoupler's falling edges against the NCN's crystal clock (XCLK),
 * in 1-second blocks. Per block: periods outside the valid frequency window are dropped, the median of the
 * rest is the block's period. The moving average of the last "averaging time" block periods gives the
 * frequency and, through the level model, the level. Blocks count only after pulses have run for 3 s, so the
 * comparator's averaging threshold (C19, 0.5 s) has settled, at power-up and after a cable fault alike.
 * No valid block for the fault timeout: sensor fault (open or shorted cable, dead transducer); the average
 * starts again when pulses return.
 *
 * Calibration: f_empty and f_full come from ETS. With "calibration through group objects", Home Assistant
 * can write them (objects 6/7) or teach them (objects 8/9, value 1: the current frequency becomes empty or
 * full); those values are kept in flash (store.c) and win until an ETS download changes the ETS values.
 * Every calibration must have f_empty >= f_full + 1 Hz inside the valid window. Invalid ETS values are
 * replaced by the defaults, and top-up stays off until a valid calibration is in use.
 * Objects 6/7 have no callback: thelsing calls a callback for the device's own sends too, and skips it for a
 * write that arrives while a send is pending (review O-1, G-6). A value in the object that differs from the
 * calibration in use was written from the bus; calObjectsLoop() applies or refuses it.
 *
 * Top-up valve (objects 10-12), through an external switching actuator:
 * - A fill session starts below the ON level and ends at the OFF level. Within a session the valve's opening
 *   time adds up across interruptions (sensor fault, disable, restart, ETS download): the session is kept in
 *   flash at its start and end and every 5 minutes, and a restart that finds it open charges 5 minutes.
 *   Using up the budget (maximum opening time) locks top-up with an alarm (object 12). While open, the level
 *   must rise by x % within y minutes, or top-up locks too (review F-03, O-5, G-4).
 * - The lock clears only when object 11 goes from off to on; a repeated "on" changes nothing (F-01, O-2).
 * - The valve is closed on a sensor fault, while disabled or locked, without a valid calibration, and while
 *   object 10 has no group address. After every start and every close it stays closed for at least 60 s, so
 *   the start-up OFF always goes out first (G-4, O-14).
 * - ON is repeated while open for an actuator with a staircase timer, so the valve closes by itself if this
 *   device or the bus fails. The valve object is sent until the bus confirms it, with a growing back-off
 *   (knx-rgbic's PSU pattern).
 * - The enable, the lock and the session survive power cuts (store.c, written at once and retried on failure).
 *   If a flash ECC error erased the store, top-up starts disabled (owner decision D1).
 *
 * SW1: a short press toggles programming mode (D5 on). Held 3-10 s (D5 blinking), released: USB DFU. Held
 * 10 s: factory reset (the ETS download and the store), then a wait for release, because the boot manager
 * would start DFU if SW1 were still held. Both close the valve first (G-5).
 */
#include "app.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "hw.h"
#include "log.h"
#include "meter.h"
#include "store.h"
#include "platform_g0.h"
#include "version.h"
#include "knxprod.h"
#include "../shared/u3_shared.h"
#include "knx/bau07B0.h"
#include "knx/group_object.h"
#include "knx/property.h"
#include "knx/tpuart_data_link_layer.h"

// ---------------------------------------------------------------- constants

static constexpr uint32_t START_LISTEN_MS = 2500;     // start-up phase after the KNX stack starts
static constexpr uint32_t SETTLE_MS = 3000;           // C19: pulses must run this long before blocks count
static constexpr uint32_t STATUS_MIN_GAP_MS = 1000;   // at most one telegram per second per status object
static constexpr uint32_t TX_TIMEOUT_MS = 10000;      // a valve telegram that is never confirmed counts as failed
static constexpr uint32_t STORE_MIN_GAP_MS = 10000;   // calibration writes: at most one per 10 s
static constexpr uint32_t VALVE_MIN_OFF_MS = 60000;   // closed at least this long after a start or a close
static constexpr uint32_t VALVE_CLOSE_WAIT_MS = 2000; // factory reset / DFU: wait this long for OFF to go out
static constexpr uint32_t FILL_SAVE_MS = 300000;      // an open fill session is stored every 5 min of opening
static constexpr uint32_t FILL_RESUME_PENALTY_MS = 300000;   // charged when a restart finds a session open
static constexpr uint32_t BTN_DEBOUNCE_MS = 30;
static constexpr uint32_t BTN_DFU_MS = 3000;          // released after 3-10 s: USB DFU
static constexpr uint32_t BTN_FACTORY_MS = 10000;
static constexpr uint32_t BTN_RELEASE_WAIT_MS = 30000;
static constexpr uint32_t LOG_PERIOD_MS = 10000;
static constexpr float MIN_CAL_SPAN_HZ = 1.0f;        // f_empty >= f_full + this
static constexpr float LOW_ALARM_HYST_PCT = 2.0f;
static constexpr uint8_t MAX_AVG_BLOCKS = 120;        // = the averaging time's maximum (spec.py)

// ---------------------------------------------------------------- KNX objects

static G0Platform* s_platform;
static Bau07B0* s_bau;
static TpUartDataLinkLayer* s_dll;
static bool s_appOk;          // at start: an ETS download is present and it is this application
static bool s_configured;     // at start: any ETS download present
static bool s_usable;         // s_appOk and the tables are still loaded (false from the start of an ETS download)
static bool s_knxStarted;
static uint8_t s_params[KNXPROD_PARAM_SIZE];

// Parameters come from a copy taken at start, so they stay valid while ETS replaces the tables. Without a
// matching ETS download the defaults apply (the measurement still runs and logs).
static uint32_t param(uint32_t off, uint32_t bits, uint32_t dflt)
{
    if (!s_appOk || off + bits / 8u > KNXPROD_PARAM_SIZE) return dflt;
    return bits == 16 ? ((uint32_t)s_params[off] << 8) | s_params[off + 1] : s_params[off];
}
#define P(name) param(PARAM_##name, PARAM_##name##_BITS, PARAM_##name##_DEFAULT)

// Every group-object access goes through go(): nothing while a download replaces the tables, and never past
// the end of the object table (GroupObjectTableObject::get() doesn't check). ETS may write a shorter table
// when the last objects are hidden; those then simply don't exist (review A9).
static GroupObject* go(uint16_t asap)
{
    if (!s_usable || asap < 1 || asap > s_bau->groupObjectTable().entryCount()) return nullptr;
    return &s_bau->groupObjectTable().get(asap);
}

static bool linked(uint16_t asap) { return go(asap) && s_bau->isGroupObjectLinked(asap); }

static bool goBusy(uint16_t asap)
{
    GroupObject* g = go(asap);
    if (!g) return false;
    ComFlag cf = g->commFlag();
    return cf == Transmitting || cf == WriteRequest;
}

// Writes a raw value into an object (bus byte order) without sending: reads (R flag) see it at once.
static bool goSet(uint16_t asap, const uint8_t* v, size_t n)
{
    GroupObject* g = go(asap);
    if (!g || g->valueSize() != n) return false;   // ETS gave this object another size: don't touch it
    memcpy(g->valueRef(), v, n);
    return true;
}

static bool goSend(uint16_t asap, const uint8_t* v, size_t n)
{
    if (!linked(asap) || !goSet(asap, v, n)) return false;
    go(asap)->objectWritten();
    return true;
}

static void goSetBit(uint16_t asap, bool b) { uint8_t v = b ? 1 : 0; goSet(asap, &v, 1); }
static bool goSendBit(uint16_t asap, bool b) { uint8_t v = b ? 1 : 0; return goSend(asap, &v, 1); }

// ---------------------------------------------------------------- datapoint types

static float absf(float x) { return x < 0 ? -x : x; }

static void be32(uint32_t v, uint8_t out[4])
{
    out[0] = (uint8_t)(v >> 24);
    out[1] = (uint8_t)(v >> 16);
    out[2] = (uint8_t)(v >> 8);
    out[3] = (uint8_t)v;
}

// DPT 14.xxx: IEEE 754 single, big-endian
static void dpt14Encode(float f, uint8_t out[4])
{
    uint32_t u;
    memcpy(&u, &f, 4);
    be32(u, out);
}

static bool dpt14Decode(const uint8_t* v, float& f)
{
    uint32_t u = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) | ((uint32_t)v[2] << 8) | v[3];
    if ((u & 0x7F800000u) == 0x7F800000u) return false;   // infinity or NaN
    memcpy(&f, &u, 4);
    return true;
}

// DPT 8.010 (percent V16): signed 16-bit, 0.01 %, big-endian. The level is clamped to 0-100 %, so 0-10000.
static void dpt8Percent(float pct, uint8_t out[2])
{
    int32_t v = 0;
    if (pct > 0) v = pct >= 327.67f ? 32767 : (int32_t)(pct * 100.0f + 0.5f);
    out[0] = (uint8_t)((uint16_t)v >> 8);
    out[1] = (uint8_t)v;
}

// ---------------------------------------------------------------- calibration in use

static float s_fEmpty, s_fFull;             // Hz
static bool s_defaultWindow;                // the ETS calibration was invalid: default window and calibration
static bool s_calFallback;                  // the defaults stand in for an invalid ETS calibration: no top-up

// Valid frequency window in 0.1 Hz: ETS's, unless it is inverted or the ETS calibration didn't fit it.
static void windowD10(uint32_t& fMin, uint32_t& fMax)
{
    fMin = P(F_VALID_MIN);
    fMax = P(F_VALID_MAX);
    if (s_defaultWindow || fMin < 10 || fMax <= fMin) {
        fMin = PARAM_F_VALID_MIN_DEFAULT;
        fMax = PARAM_F_VALID_MAX_DEFAULT;
    }
}

// The one calibration check (review A5): the Garantia transducer reads lower with more water, so
// f_empty >= f_full + 1 Hz, and both inside the valid window.
static bool calValid(float fe, float ff)
{
    uint32_t lo, hi;
    windowD10(lo, hi);
    return fe >= ff + MIN_CAL_SPAN_HZ && ff >= lo / 10.0f && fe <= hi / 10.0f;
}

// ---------------------------------------------------------------- measurement

static float s_avgBuf[MAX_AVG_BLOCKS];      // block median periods (ticks), ring
static bool s_avgXclk[MAX_AVG_BLOCKS];      // that block was timed on XCLK
static uint8_t s_avgCount, s_avgHead;
static bool s_haveValue;                    // an average exists and the sensor isn't faulted
static float s_freq;                        // Hz
static float s_levelRaw, s_level;           // %, unclamped and clamped to 0-100
static bool s_fault;
static uint32_t s_lastValidMs;              // last accepted block
static uint32_t s_lastGoodMs, s_runStart;   // pulse runs, for the settling time
static uint32_t s_blocksOk, s_blocksBad, s_blocksHsi;
static uint32_t s_period[METER_MAX_PERIODS];

static float levelRawFromFreq(float f)
{
    float x;
    if (P(LEVEL_MODEL) == 1) x = (f - s_fEmpty) / (s_fFull - s_fEmpty);
    else x = (1.0f / f - 1.0f / s_fEmpty) / (1.0f / s_fFull - 1.0f / s_fEmpty);
    return x * 100.0f;
}

static float clampPct(float x)
{
    if (!(x > 0)) return 0;   // also NaN
    return x > 100 ? 100 : x;
}

static void updateLevel()
{
    s_levelRaw = levelRawFromFreq(s_freq);
    if (!(s_levelRaw == s_levelRaw)) s_levelRaw = 0;   // NaN
    s_level = clampPct(s_levelRaw);
}

static uint8_t averagingBlocks()
{
    uint32_t n = P(AVERAGING_S);
    return (uint8_t)(n < 1 ? 1 : n > MAX_AVG_BLOCKS ? MAX_AVG_BLOCKS : n);
}

static void validWindowTicks(uint32_t& tMin, uint32_t& tMax)
{
    uint32_t fMin, fMax;
    windowD10(fMin, fMax);
    tMin = (uint32_t)((uint64_t)METER_TICK_HZ * 10u / fMax);
    tMax = (uint32_t)((uint64_t)METER_TICK_HZ * 10u / fMin);
}

static uint8_t avgIndex(uint8_t i) { return (uint8_t)((s_avgHead + MAX_AVG_BLOCKS - 1u - i) % MAX_AVG_BLOCKS); }

// Every block in the current average was timed on XCLK (teach-in needs that, review O-8).
static bool averageOnXclk()
{
    for (uint8_t i = 0; i < s_avgCount; i++)
        if (!s_avgXclk[avgIndex(i)]) return false;
    return s_avgCount > 0;
}

static void recompute()
{
    uint8_t n = averagingBlocks();
    if (s_avgCount > n) s_avgCount = n;
    uint8_t need = n < 3 ? n : 3;
    if (s_avgCount < need) return;
    float sum = 0;
    for (uint8_t i = 0; i < s_avgCount; i++) sum += s_avgBuf[avgIndex(i)];
    float period = sum / s_avgCount;
    s_freq = (float)METER_TICK_HZ / period;
    updateLevel();
    s_haveValue = true;
    if (s_fault) {
        s_fault = false;
        log_str("sensor: pulses back\r\n");
    }
}

static void onBlock(const meter_block_t& b, uint32_t now)
{
    uint32_t tMin, tMax;
    validWindowTicks(tMin, tMax);
    uint16_t n = 0;
    for (uint16_t i = 0; i < b.count; i++)
        if (b.period[i] >= tMin && b.period[i] <= tMax) s_period[n++] = b.period[i];
    // Good: trusted timing, at least 3 valid periods, and they are a strict majority of what was captured,
    // lost captures included (a noisy block, with extra edges splitting periods, doesn't count; review G-12).
    bool good = b.valid && n >= 3 && (uint32_t)n * 2u > (uint32_t)b.count + b.dropped;
    if (!good) {
        s_blocksBad++;
        return;
    }
    if (!s_lastGoodMs || (now - s_lastGoodMs) > SETTLE_MS) s_runStart = now;   // pulses (re)started
    s_lastGoodMs = now;
    if ((now - s_runStart) < SETTLE_MS) return;                              // C19 still settling

    for (uint16_t i = 1; i < n; i++) {                                       // insertion sort, n <= 512
        uint32_t v = s_period[i];
        uint16_t j = i;
        for (; j > 0 && s_period[j - 1] > v; j--) s_period[j] = s_period[j - 1];
        s_period[j] = v;
    }
    float median = (n & 1) ? (float)s_period[n / 2] : 0.5f * ((float)s_period[n / 2 - 1] + (float)s_period[n / 2]);
    s_avgBuf[s_avgHead] = median;
    s_avgXclk[s_avgHead] = b.source == METER_SRC_XCLK;
    s_avgHead = (uint8_t)((s_avgHead + 1u) % MAX_AVG_BLOCKS);
    if (s_avgCount < MAX_AVG_BLOCKS) s_avgCount++;
    s_lastValidMs = now;
    s_blocksOk++;
    if (b.source != METER_SRC_XCLK) s_blocksHsi++;
    recompute();
}

static void faultLoop(uint32_t now)
{
    uint32_t timeout = 1000u * P(FAULT_TIMEOUT_S);
    if (!s_fault && (now - s_lastValidMs) > SETTLE_MS + timeout) {
        // Counted from the last accepted block, which needs SETTLE_MS of pulses after a start or a gap.
        s_fault = true;
        s_haveValue = false;
        s_avgCount = 0;
        log_str("sensor: FAULT, no valid pulses\r\n");
    }
}

// ---------------------------------------------------------------- store

static uint16_t s_etsEmpty, s_etsFull;      // ETS values (0.1 Hz)
static bool s_topupEnabled = true;          // object 11
static bool s_topupLocked;                  // top-up fault lock
static bool s_session;                      // a fill session is open
static uint32_t s_sessionUsedMs;            // its opening time so far
static uint32_t s_sessionSavedMs;           // s_sessionUsedMs at the last store
static bool s_storeDirty;                   // calibration changed: stored at most every STORE_MIN_GAP_MS
static bool s_storeUrgent;                  // enable, lock or session changed: stored at once
static uint32_t s_storeAt, s_storeRetryAt;
static uint8_t s_storeFails;

static bool calAllowed() { return s_appOk && P(ALLOW_CAL_OBJECTS) != 0; }

static void calFromEts()
{
    s_defaultWindow = false;
    s_fEmpty = s_etsEmpty / 10.0f;
    s_fFull = s_etsFull / 10.0f;
    s_calFallback = false;
    if (calValid(s_fEmpty, s_fFull)) return;
    // ETS can't enforce the cross-parameter rules: use the defaults with their own window, and keep top-up
    // off until a valid calibration is in use (review G-7, O-1c).
    s_defaultWindow = true;
    s_fEmpty = PARAM_F_EMPTY_DEFAULT / 10.0f;
    s_fFull = PARAM_F_FULL_DEFAULT / 10.0f;
    s_calFallback = s_appOk;
    if (s_appOk) log_str("calibration: ETS values invalid, defaults in use, top-up off\r\n");
}

static void storeInit()
{
    s_etsEmpty = (uint16_t)P(F_EMPTY);
    s_etsFull = (uint16_t)P(F_FULL);
    calFromEts();
    int lost = hw_take_store_lost();
    store_rec_t r;
    if (!s_appOk) return;
    if (store_load(&r)) {
        s_topupEnabled = r.topupEnabled != 0;
        s_topupLocked = r.topupLocked != 0;
        if (calAllowed() && r.etsEmpty == s_etsEmpty && r.etsFull == s_etsFull && calValid(r.fEmpty, r.fFull)) {
            s_fEmpty = r.fEmpty;
            s_fFull = r.fFull;
            s_calFallback = false;
            log_str("calibration from store\r\n");
        }
        if (r.fillActive && P(TOPUP_ENABLED)) {
            // A fill was open when this device stopped: resume it, charging the unsaved time conservatively.
            s_session = true;
            s_sessionUsedMs = (uint32_t)r.fillUsedS * 1000u + FILL_RESUME_PENALTY_MS;
            s_sessionSavedMs = s_sessionUsedMs;
            s_storeUrgent = true;
            log_str("top-up: fill session resumed\r\n");
        }
    }
    if (lost) {
        // The ECC NMI erased (part of) the store: the newest enable/lock state may be gone (owner decision D1).
        s_topupEnabled = false;
        s_session = false;
        s_storeUrgent = true;
        log_str("store: LOST after a flash error, top-up disabled until enabled again\r\n");
    }
}

static void storeLoop(uint32_t now)
{
    if (!s_appOk || !(s_storeDirty || s_storeUrgent) || !hw_saveb()) return;   // not while the bus drops
    if (!s_storeUrgent && (now - s_storeAt) < STORE_MIN_GAP_MS) return;
    if ((int32_t)(now - s_storeRetryAt) < 0) return;
    store_rec_t r;
    memset(&r, 0, sizeof r);
    r.fEmpty = s_fEmpty;
    r.fFull = s_fFull;
    r.etsEmpty = s_etsEmpty;
    r.etsFull = s_etsFull;
    r.topupEnabled = s_topupEnabled;
    r.topupLocked = s_topupLocked;
    r.fillActive = s_session;
    uint32_t usedS = s_sessionUsedMs / 1000u;
    r.fillUsedS = (uint16_t)(usedS > 0xFFFFu ? 0xFFFFu : usedS);
    if (store_save(&r) == 0) {
        s_storeDirty = s_storeUrgent = false;   // cleared only once stored (review G-3)
        s_storeFails = 0;
        s_storeAt = now;
        return;
    }
    static const uint16_t backoffS[] = {2, 10, 30, 60};
    s_storeRetryAt = now + 1000u * backoffS[s_storeFails < 3 ? s_storeFails : 3];
    if (s_storeFails < 0xFF) s_storeFails++;
    log_str("store: WRITE FAILED, retrying\r\n");
}

// ---------------------------------------------------------------- calibration objects

static bool s_calSend[2];                   // send object 6/7: after a teach-in, a refused write, the start-up
static uint32_t s_calSentAt[2];

static float calValue(uint8_t which) { return which ? s_fFull : s_fEmpty; }

// The object always holds the calibration in use, so calObjectsLoop() can tell a bus write from our own change.
static void calSet(uint8_t which, float f)   // 0 = empty, 1 = full
{
    (which ? s_fFull : s_fEmpty) = f;
    uint8_t b[4];
    dpt14Encode(f, b);
    goSet(which ? KO_CAL_F_FULL : KO_CAL_F_EMPTY, b, 4);
    s_calFallback = false;
    s_storeDirty = true;
    if (s_haveValue) updateLevel();
    log_str(which ? "calibration: full = " : "calibration: empty = ");
    log_fix((int32_t)(f * 100.0f + 0.5f), 2);
    log_str(" Hz\r\n");
}

static void calWritten(uint8_t which, const uint8_t* v)
{
    float f;
    if (calAllowed() && dpt14Decode(v, f) && calValid(which ? s_fEmpty : f, which ? f : s_fFull)) {
        if (f != calValue(which)) calSet(which, f);
    } else {
        s_calSend[which] = true;    // refused: put the value in use back on the bus (once per write)
        log_str("calibration: write refused\r\n");
    }
}

static void calTeach(uint8_t which)
{
    float fe = which ? s_fEmpty : s_freq, ff = which ? s_freq : s_fFull;
    if (!calAllowed() || !s_haveValue || s_fault || !averageOnXclk() || !calValid(fe, ff)) {
        log_str("calibration: teach-in refused (no valid XCLK-timed value, or out of range)\r\n");
        return;
    }
    calSet(which, s_freq);
    s_calSend[which] = true;
}

// Objects 6/7 hold the calibration in use. A different value, outside our own send, came from the bus (a
// write: no U flag, so never a read answer). Also runs during the start-up phase; sends wait for its end.
static void calObjectsLoop(uint32_t now, bool maySend)
{
    if (!calAllowed()) return;
    for (uint8_t i = 0; i < 2; i++) {
        uint16_t ko = i ? KO_CAL_F_FULL : KO_CAL_F_EMPTY;
        GroupObject* g = go(ko);
        if (!g || g->valueSize() != 4 || goBusy(ko)) continue;
        uint8_t cur[4];
        dpt14Encode(calValue(i), cur);
        if (memcmp(g->valueRef(), cur, 4) != 0) {
            uint8_t v[4];
            memcpy(v, g->valueRef(), 4);
            calWritten(i, v);
            dpt14Encode(calValue(i), cur);
            memcpy(g->valueRef(), cur, 4);
            if (g->commFlag() == Updated) g->commFlag(Ok);
        }
        if (!s_calSend[i] || !maySend) continue;
        if (!linked(ko)) {
            s_calSend[i] = false;
        } else if ((now - s_calSentAt[i]) >= STATUS_MIN_GAP_MS && goSend(ko, cur, 4)) {
            s_calSend[i] = false;
            s_calSentAt[i] = now;
        }
    }
}

static void calPublish()
{
    for (uint8_t i = 0; i < 2; i++) {
        uint8_t b[4];
        dpt14Encode(calValue(i), b);
        goSet(i ? KO_CAL_F_FULL : KO_CAL_F_EMPTY, b, 4);
    }
}

// ---------------------------------------------------------------- object callback (objects 8, 9, 11)

static void onGroupObject(GroupObject& g)
{
    if (!s_usable || g.commFlag() == WriteRequest) return;   // never for our own sends (these objects have no T)
    const uint8_t* v = g.valueRef();
    switch (g.asap()) {
        case KO_CAL_EMPTY_NOW:
        case KO_CAL_FULL_NOW:
            if (g.valueSize() == 1 && (v[0] & 1)) calTeach(g.asap() == KO_CAL_FULL_NOW);   // value 1 only (B7)
            break;
        case KO_TOPUP_ENABLE:
            if (g.valueSize() == 1) {
                bool en = v[0] & 1;
                if (en == s_topupEnabled) break;   // a repeated value changes nothing: a lock needs off, then on
                if (en) {
                    log_str(s_topupLocked ? "top-up: enabled, lock cleared\r\n" : "top-up: enabled\r\n");
                    s_topupLocked = false;
                    s_session = false;              // a fresh session, with the full budget
                    s_sessionUsedMs = s_sessionSavedMs = 0;
                } else {
                    log_str("top-up: disabled\r\n");
                }
                s_topupEnabled = en;
                s_storeUrgent = true;
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------- status objects

struct Sent {
    bool valid;
    float value;
    uint32_t at;
};
static Sent s_sentLevel, s_sentFreq, s_sentVolume, s_sentFault, s_sentAlarm, s_sentTopupFault;
static bool s_lowAlarm;

static void invalidateStatus()
{
    s_sentLevel.valid = s_sentFreq.valid = s_sentVolume.valid = false;
    s_sentFault.valid = s_sentAlarm.valid = s_sentTopupFault.valid = false;
    s_calSend[0] = s_calSend[1] = calAllowed();
}

// Sends a value status: on the first chance, on a change of at least `change` (0 = no), every `cycleMin`
// minutes (0 = no); never faster than STATUS_MIN_GAP_MS, never while the previous telegram is still out.
// A negative confirmation isn't resent: nothing ACKing a status GA is normal.
static bool statusDue(Sent& s, float value, float change, uint32_t cycleMin, uint32_t now)
{
    if ((now - s.at) < STATUS_MIN_GAP_MS && s.valid) return false;
    if (!s.valid) return true;
    if (change > 0 && absf(value - s.value) >= change) return true;
    return cycleMin && (now - s.at) >= cycleMin * 60000u;
}

static void sendValueStatus(uint16_t ko, Sent& s, float value, const uint8_t* raw, size_t n, float change,
                            uint32_t cycleMin, uint32_t now)
{
    goSet(ko, raw, n);
    if (linked(ko) && !goBusy(ko) && statusDue(s, value, change, cycleMin, now) && goSend(ko, raw, n))
        s = {true, value, now};
}

static void sendBitStatus(uint16_t ko, Sent& s, bool b, uint32_t now)
{
    if (!linked(ko)) {
        goSetBit(ko, b);
        return;
    }
    if ((s.valid && (s.value != 0) == b) || goBusy(ko)) return;
    if (goSendBit(ko, b)) s = {true, b ? 1.0f : 0.0f, now};
}

static void statusLoop(uint32_t now)
{
    if (s_haveValue) {
        uint8_t lv[2];
        dpt8Percent(s_level, lv);
        sendValueStatus(KO_LEVEL, s_sentLevel, s_level, lv, 2, (float)P(LEVEL_CHANGE_PCT), P(LEVEL_CYCLE_MIN), now);
        uint8_t fr[4];
        dpt14Encode(s_freq, fr);
        sendValueStatus(KO_FREQUENCY, s_sentFreq, s_freq, fr, 4, P(FREQ_CHANGE_D10) / 10.0f, P(FREQ_CYCLE_MIN), now);
        // Volume: its own change and cycle record, whatever object 1 does (review O-3, G-10).
        uint32_t vol = P(TANK_VOLUME_L);
        if (vol) {
            float litres = s_level * (float)vol / 100.0f;
            uint8_t vb[4];
            be32((uint32_t)(litres + 0.5f), vb);
            sendValueStatus(KO_VOLUME, s_sentVolume, litres, vb, 4, (float)P(LEVEL_CHANGE_PCT) * (float)vol / 100.0f,
                            P(LEVEL_CYCLE_MIN), now);
        }
        uint32_t thr = P(LOW_ALARM_PCT);
        if (!thr) s_lowAlarm = false;
        else if (s_level < (float)thr) s_lowAlarm = true;
        else if (s_level >= (float)thr + LOW_ALARM_HYST_PCT) s_lowAlarm = false;
        sendBitStatus(KO_LOW_ALARM, s_sentAlarm, s_lowAlarm, now);   // during a fault: the last state stays
    }
    sendBitStatus(KO_SENSOR_FAULT, s_sentFault, s_fault, now);
}

// ---------------------------------------------------------------- top-up valve

static bool s_valveWant;
static int8_t s_valveConfirmed = -1;        // value the bus last confirmed on object 10; -1 = unknown
static bool s_valveSending, s_valveSendingVal, s_valveForce;
static uint32_t s_valveSendAt, s_valveRetryAt, s_valveLastTx;
static uint8_t s_valveErrors;
static uint32_t s_closedUntil;              // the valve stays closed until then (minimum off time)
static uint32_t s_riseAt;
static float s_riseLevel;
static uint32_t s_topupTick;
static bool s_noLinkLogged;

static void valveSetWant(bool on, uint32_t now)
{
    if (on == s_valveWant) return;
    s_valveWant = on;
    s_valveErrors = 0;          // a new value goes out at once, whatever the back-off of the old one
    s_valveRetryAt = now;
}

static uint32_t valveBackoff(uint8_t errors)
{
    static const uint16_t s[] = {2, 5, 10, 30, 60};
    return 1000u * s[errors > 5 ? 4 : (errors ? errors - 1 : 0)];
}

static void valveTransmit(uint32_t now)
{
    GroupObject* g = go(KO_VALVE);
    if (!g) return;
    if (!linked(KO_VALVE)) {
        goSetBit(KO_VALVE, s_valveWant);    // no group address: keep the value readable
        s_valveConfirmed = s_valveWant;
        s_valveSending = s_valveForce = false;
        return;
    }
    if (s_valveSending) {
        ComFlag f = g->commFlag();
        if (f == Ok) {
            s_valveSending = false;
            s_valveConfirmed = s_valveSendingVal;
            s_valveErrors = 0;
            s_valveLastTx = now;
        } else if (f == Error || (now - s_valveSendAt) > TX_TIMEOUT_MS) {
            if (f != Error) g->commFlag(Error);   // still queued or in flight: free the object for the retry
            s_valveSending = false;
            s_valveConfirmed = -1;
            if (s_valveErrors < 0xFF) s_valveErrors++;
            s_valveRetryAt = now + valveBackoff(s_valveErrors);
        }
    }
    uint32_t rep = 1000u * P(TOPUP_REPEAT_S);
    bool cyclic = s_valveWant && rep && s_valveConfirmed == 1 && (now - s_valveLastTx) >= rep;
    bool need = s_valveForce || cyclic || s_valveConfirmed != (s_valveWant ? 1 : 0);
    if (need && !s_valveSending && !goBusy(KO_VALVE) && (int32_t)(now - s_valveRetryAt) >= 0 &&
        goSendBit(KO_VALVE, s_valveWant)) {
        s_valveSending = true;
        s_valveSendingVal = s_valveWant;
        s_valveSendAt = now;
        s_valveForce = false;
    }
}

// Factory reset and DFU entry: send OFF and give it a moment to go out (review G-5). Without KNX running, or
// without a valve group address, there's nothing to send; the actuator's staircase timer remains the backstop.
static void valveCloseNow()
{
    if (!s_knxStarted || !s_usable || !P(TOPUP_ENABLED) || !linked(KO_VALVE)) return;
    uint32_t t = hw_millis();
    while (goBusy(KO_VALVE) && (hw_millis() - t) < VALVE_CLOSE_WAIT_MS) {
        hw_iwdg_refresh();
        s_bau->loop();
    }
    if (goBusy(KO_VALVE) || !goSendBit(KO_VALVE, false)) return;
    t = hw_millis();
    while ((hw_millis() - t) < VALVE_CLOSE_WAIT_MS) {
        hw_iwdg_refresh();
        s_bau->loop();
        GroupObject* g = go(KO_VALVE);
        if (!g || g->commFlag() == Ok || g->commFlag() == Error) break;
    }
    log_str("top-up: valve OFF sent\r\n");
}

static void topupLock(const char* why)
{
    s_topupLocked = true;
    s_session = false;
    s_sessionUsedMs = s_sessionSavedMs = 0;
    s_storeUrgent = true;
    log_str("top-up: LOCKED, ");
    log_str(why);
    log_str("\r\n");
}

static void topupLoop(uint32_t now)
{
    if (!s_appOk || !P(TOPUP_ENABLED)) return;
    uint32_t dt = now - s_topupTick;
    s_topupTick = now;
    float onPct = (float)P(TOPUP_ON_PCT), offPct = (float)P(TOPUP_OFF_PCT);
    if (offPct <= onPct) offPct = onPct + 1.0f;   // ETS can't enforce it: keep a hysteresis
    bool valveLinked = linked(KO_VALVE);
    if (!valveLinked && !s_noLinkLogged) log_str("top-up: object 10 has no group address, no filling\r\n");
    s_noLinkLogged = !valveLinked;
    bool levelOk = s_haveValue && !s_fault;
    bool canFill = s_topupEnabled && !s_topupLocked && !s_calFallback && valveLinked;   // B5

    // The session: from below ON to OFF, across interruptions.
    if (!s_session && canFill && levelOk && s_level < onPct) {
        s_session = true;
        s_sessionUsedMs = s_sessionSavedMs = 0;
        s_storeUrgent = true;
        log_str("top-up: fill session started\r\n");
    } else if (s_session && levelOk && s_level >= offPct) {
        s_session = false;
        s_sessionUsedMs = s_sessionSavedMs = 0;
        s_storeUrgent = true;
        log_str("top-up: level reached, session ended\r\n");
    }
    if (s_valveWant) s_sessionUsedMs += dt;   // opening time: while ON was wanted
    bool open = s_session && canFill && levelOk && (int32_t)(now - s_closedUntil) >= 0;
    if (s_session && s_sessionUsedMs >= 60000u * P(TOPUP_MAX_ON_MIN)) {
        topupLock("maximum opening time");
        open = false;
    }
    if (open && !s_valveWant) {               // (re)opening: a fresh rise window
        s_riseAt = now;
        s_riseLevel = s_levelRaw;
    }
    uint32_t risePct = P(TOPUP_RISE_PCT);
    if (open && s_valveWant && risePct && (now - s_riseAt) >= 60000u * P(TOPUP_RISE_MIN)) {
        // Unclamped level: a fill starting below the sensor's 0 % still shows its rise (review F-14).
        if (s_levelRaw < s_riseLevel + (float)risePct) {
            topupLock("level not rising");
            open = false;
        } else {
            s_riseAt = now;
            s_riseLevel = s_levelRaw;
        }
    }
    if (s_session && (s_sessionUsedMs - s_sessionSavedMs) >= FILL_SAVE_MS) {
        s_sessionSavedMs = s_sessionUsedMs;
        s_storeUrgent = true;
    }
    if (s_valveWant && !open) s_closedUntil = now + VALVE_MIN_OFF_MS;   // closing: stay closed a while
    valveSetWant(open, now);
    valveTransmit(now);
    goSetBit(KO_TOPUP_ENABLE, s_topupEnabled);
    sendBitStatus(KO_TOPUP_FAULT, s_sentTopupFault, s_topupLocked, now);
}

// ---------------------------------------------------------------- start-up, bus reconnect

static uint32_t s_knxStartAt, s_startEndAt;
static bool s_starting = true;
static bool s_busConnectedPrev;

static void startupEnd(uint32_t now)
{
    s_starting = false;
    invalidateStatus();
    // The valve: OFF first after every start (the actuator may still be on from before a bus or power
    // outage); the minimum off time keeps any ON behind it (review G-4).
    s_valveWant = false;
    s_valveConfirmed = -1;
    s_valveRetryAt = now;
    s_closedUntil = now + VALVE_MIN_OFF_MS;
    s_topupTick = now;
    s_busConnectedPrev = s_dll && s_dll->isConnected();
    log_str("knx: start-up phase over\r\n");
}

static void busLoop(uint32_t now)
{
    bool connected = s_dll && s_dll->isConnected();
    if (connected && !s_busConnectedPrev) {
        // Bus back after a disconnect: telegrams sent meanwhile failed, and actuators may be in their bus-return
        // state. Send every status again and re-assert the valve.
        invalidateStatus();
        if (s_valveWant) s_valveForce = true;
        s_valveConfirmed = -1;
        s_valveErrors = 0;
        s_valveRetryAt = now;
        log_str("knx: bus connected\r\n");
    }
    s_busConnectedPrev = connected;
}

// ---------------------------------------------------------------- SW1, D5

static bool s_btnRaw, s_btn;
static uint32_t s_btnChangeAt, s_btnDownAt;

static void blink(uint32_t ms, uint32_t periodMs)
{
    uint32_t t = hw_millis();
    while ((hw_millis() - t) < ms) {
        hw_iwdg_refresh();
        hw_led(((hw_millis() - t) / (periodMs / 2u)) & 1u);
    }
}

static __attribute__((noreturn)) void factoryReset()
{
    valveCloseNow();
    log_str("factory reset: erasing the ETS download and the store\r\n");
    hw_dbg_flush();
    hw_erase_all_data();
    // Wait for release: the boot manager starts USB DFU if SW1 is held at reset (after 30 s it does just that).
    uint32_t t = hw_millis();
    while (hw_button() && (hw_millis() - t) < BTN_RELEASE_WAIT_MS) {
        hw_iwdg_refresh();
        hw_led(((hw_millis() - t) / 100u) & 1u);
    }
    hw_reset();
}

static __attribute__((noreturn)) void enterDfu()
{
    valveCloseNow();
    log_str("entering USB DFU\r\n");
    blink(1000, 100);
    hw_reset_to_bootloader();
}

static void buttonLoop(uint32_t now)
{
    bool raw = hw_button();
    if (raw != s_btnRaw) {
        s_btnRaw = raw;
        s_btnChangeAt = now;
    }
    if (raw != s_btn && (now - s_btnChangeAt) >= BTN_DEBOUNCE_MS) {
        s_btn = raw;
        uint32_t held = now - s_btnDownAt;
        if (raw) {
            s_btnDownAt = now;
        } else if (held < BTN_DFU_MS) {
            bool pm = !s_bau->deviceObject().progMode();
            s_bau->deviceObject().progMode(pm);
            log_str(pm ? "programming mode on\r\n" : "programming mode off\r\n");
        } else if (held < BTN_FACTORY_MS) {
            enterDfu();   // released after 3-10 s (review O-10)
        }
    }
    uint32_t held = now - s_btnDownAt;
    if (s_btn && held >= BTN_FACTORY_MS) factoryReset();
    if (s_btn && held >= BTN_DFU_MS) hw_led((held / 250u) & 1u);   // blinking: release now for DFU
    else hw_led(s_bau->deviceObject().progMode());
}

// ---------------------------------------------------------------- log

static uint32_t s_logAt;

static void logLoop(uint32_t now)
{
    if ((now - s_logAt) < LOG_PERIOD_MS) return;
    s_logAt = now;
    if (s_haveValue) {
        log_str("f=");
        log_fix((int32_t)(s_freq * 100.0f + 0.5f), 2);
        log_str(" Hz level=");
        log_fix((int32_t)(s_level * 10.0f + 0.5f), 1);
        log_str(" %");
    } else {
        log_str("no value");
    }
    log_str(s_fault ? " FAULT" : "");
    log_str(meter_source() == METER_SRC_XCLK ? " clk=XCLK" : " clk=HSI");
    log_str(" blocks ok/bad/hsi=");
    log_u32(s_blocksOk);
    log_str("/");
    log_u32(s_blocksBad);
    log_str("/");
    log_u32(s_blocksHsi);
    log_str(" edges=");
    log_u32(meter_edges());
    log_str(" saveb=");
    log_u32((uint32_t)hw_saveb());
    log_str(" bus=");
    log_u32(s_dll && s_dll->isConnected() ? 1u : 0u);
    log_str(" uart_err=");
    log_u32(hw_knx_uart_errors());
    if (s_appOk && P(TOPUP_ENABLED)) {
        log_str(" valve=");
        log_u32(s_valveWant ? 1u : 0u);
        if (s_session) {
            log_str(" session_s=");
            log_u32(s_sessionUsedMs / 1000u);
        }
        log_str(s_topupLocked ? " LOCKED" : s_topupEnabled ? "" : " disabled");
        if (s_calFallback) log_str(" CAL-INVALID");
    }
    log_str("\r\n");
}

// ---------------------------------------------------------------- KNX setup

// The ID pins the parameter and object layout (APP_VERSION is bumped on any layout change). No object-count
// check: ETS may write a shorter table when the last objects are hidden, and go() handles that (review A9).
static bool applicationMatches()
{
    if (!s_bau->configured()) return false;
    uint8_t pv[5] = {0};
    Property* prop = s_bau->parameters().property(PID_PROG_VERSION);
    if (!prop || prop->read(1, 1, pv) != 1) return false;
    return pv[0] == (KNXPROD_MANUFACTURER >> 8) && pv[1] == (KNXPROD_MANUFACTURER & 0xFF) &&
           pv[2] == (KNXPROD_APP_NUMBER >> 8) && pv[3] == (KNXPROD_APP_NUMBER & 0xFF) && pv[4] == KNXPROD_APP_VERSION;
}

static void knxIsr() { if (s_dll) s_dll->processRxISR(); }

static void knxSetup()
{
    s_platform = new G0Platform();
    s_bau = new Bau07B0(*s_platform);
    DeviceObject& dev = s_bau->deviceObject();
    dev.manufacturerId(KNXPROD_MANUFACTURER);
    dev.bauNumber(s_platform->uniqueSerialNumber());
    uint8_t order[10] = {0};
    memcpy(order, KNXPROD_ORDER_NUMBER, sizeof(KNXPROD_ORDER_NUMBER) - 1 < sizeof order ? sizeof(KNXPROD_ORDER_NUMBER) - 1 : sizeof order);
    dev.orderNumber(order);
    // Stable hardware identity: no application version in it, or a version bump would wipe the address.
    uint8_t hwType[6] = {0x00, 0x00, (uint8_t)(KNXPROD_APP_NUMBER >> 8), (uint8_t)KNXPROD_APP_NUMBER, 0x00, 0x00};
    dev.hardwareType(hwType);
    s_bau->readMemory();
    s_configured = s_bau->configured();
    s_appOk = applicationMatches();
    s_usable = s_appOk;
    if (s_appOk) {
        for (uint32_t i = 0; i < KNXPROD_PARAM_SIZE; i++) s_params[i] = s_bau->parameters().getByte(i);
        for (uint16_t ko : {KO_CAL_EMPTY_NOW, KO_CAL_FULL_NOW, KO_TOPUP_ENABLE})
            if (GroupObject* g = go(ko)) g->callback(onGroupObject);
    }
    s_dll = s_bau->getDataLinkLayer();
    hw_knx_uart_set_rx_hook(knxIsr);
}

// ---------------------------------------------------------------- init / loop

void app_init()
{
    uint32_t now = hw_millis();
    uint8_t cause = hw_reset_cause();
    log_str("\r\ngarantia-knx U3 firmware ");
    log_u32(U3_FW_VERSION >> 8);
    log_str(".");
    log_u32(U3_FW_VERSION & 0xFFu);
    log_str(", boot manager ");
    log_u32(hw_boot_version());
    log_str(", reset 0x");
    log_hex(cause, 2);
    log_str(cause & HW_RESET_DFU ? " (after DFU)\r\n" : "\r\n");

    meter_init();
    knxSetup();
    storeInit();
    calPublish();
    s_storeAt = now - STORE_MIN_GAP_MS;
    s_storeRetryAt = now;
    s_lastValidMs = now;

    log_str("knx serial 00FA:");
    log_hex(s_platform->uniqueSerialNumber(), 8);
    log_str(s_appOk ? ", application OK" : s_configured ? ", OTHER APPLICATION LOADED (ignored)" : ", not configured");
    log_str(", objects ");
    log_u32(s_usable ? s_bau->groupObjectTable().entryCount() : 0u);
    log_str(", empty/full ");
    log_fix((int32_t)(s_fEmpty * 10.0f + 0.5f), 1);
    log_str("/");
    log_fix((int32_t)(s_fFull * 10.0f + 0.5f), 1);
    log_str(" Hz\r\n");

    s_knxStartAt = now + 1000u * P(STARTUP_DELAY_S);
    s_startEndAt = s_knxStartAt + START_LISTEN_MS;
    s_btnRaw = s_btn = hw_button();
    s_btnChangeAt = s_btnDownAt = now;
    s_logAt = now;
}

void app_loop()
{
    uint32_t now = hw_millis();

    // Once ETS starts replacing the tables, configured() stays false until the restart that ends the download.
    if (s_usable && !s_bau->configured()) s_usable = false;

    if (!s_knxStarted && (int32_t)(now - s_knxStartAt) >= 0) {
        s_bau->enabled(true);
        s_knxStarted = true;
    }
    if (s_knxStarted) s_bau->loop();
    if (s_starting && (int32_t)(now - s_startEndAt) >= 0) startupEnd(now);

    if (const meter_block_t* b = meter_poll(now)) onBlock(*b, now);
    faultLoop(now);
    buttonLoop(now);
    calObjectsLoop(now, !s_starting);
    if (!s_starting) {
        busLoop(now);
        statusLoop(now);
        topupLoop(now);
    }
    storeLoop(now);
    logLoop(now);
}

// ---------------------------------------------------------------- C++ runtime without exceptions

void* operator new(size_t n) { void* p = malloc(n); if (!p) hw_reset(); return p; }
void* operator new[](size_t n) { void* p = malloc(n); if (!p) hw_reset(); return p; }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }
extern "C" void __cxa_pure_virtual() { hw_reset(); }
