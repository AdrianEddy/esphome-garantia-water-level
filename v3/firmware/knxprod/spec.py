"""Single source for the KNX application of garantia-knx-direct: parameters and group objects.

gen.py turns this into the firmware header (firmware/u3/src/app/knxprod.h) and gen_xml.py into the ETS
application XML, so the firmware and the .knxprod can't disagree about offsets, defaults or object numbers.
The objects and parameters are described in design.md "KNX application" and "Firmware notes".

Parameter values are stored big-endian in the parameter block (16-bit ones at even offsets), as ETS writes them.
"""

MANUFACTURER = 0x00FA          # OpenKNX / Kaenx community manufacturer ID (DIY, not KNX-certified)
APP_NUMBER = 0x4741            # private application number ("GA"); knx-rgbic uses 0x4B52
APP_VERSION = 0x03             # bump on any parameter/object layout change
REPLACES_VERSIONS = (2,)       # ETS offers an in-place update from these versions, keeping parameters and links
                               # (matched by their IDs). List only versions whose parameter IDs mean the same.
ORDER_NUMBER = "GARANTIA-3"    # max 10 characters (board silkscreen: "GARANTIA KNX v3")
HARDWARE_SERIAL = "4741"       # 4 hex digits, ETS hardware id (knx-rgbic: 0001)
PRODUCT_NAME = "Garantia tank level"
APP_NAME = "Garantia tank level"
BUS_CURRENT_MA = 10            # design.md "Bus and aux budget": FANIN open, declare 10 mA

# Enumerations: name -> [(value, text), ...]
ENUMS = {
    "LevelModel": [(0, "Period-linear (Garantia transducer)"), (1, "Frequency-linear")],
}

# ---- parameters ----
# (name, offset, size_bits, default, min, max, text, block, enum, when)
#   enum: None, or a key of ENUMS (min/max then come from the enumeration)
#   when: None, or (controlling parameter, value): shown in ETS only while that parameter has that value
# Frequencies are in 0.1 Hz. f_empty > f_full for the Garantia transducer (more water, lower frequency).
PARAMS = [
    # General
    ("STARTUP_DELAY_S", 0, 8, 0, 0, 60,
     "Delay after bus voltage return before the device sends anything (s)", "general", None, None),
    ("AVERAGING_S", 1, 8, 10, 1, 120,
     "Averaging time of the frequency (s)", "general", None, None),
    ("FAULT_TIMEOUT_S", 2, 8, 5, 1, 60,
     "Sensor fault after this long without valid pulses (s)", "general", None, None),
    ("F_VALID_MIN", 4, 16, 150, 10, 3000,
     "Lowest valid sensor frequency (0.1 Hz)", "general", None, None),
    ("F_VALID_MAX", 6, 16, 1800, 10, 3000,
     "Highest valid sensor frequency (0.1 Hz)", "general", None, None),

    # Level
    ("LEVEL_MODEL", 3, 8, 0, None, None,
     "Level model", "level", "LevelModel", None),
    ("F_EMPTY", 8, 16, 1450, 10, 3000,
     "Frequency at 0 % (empty, 0.1 Hz)", "level", None, None),
    ("F_FULL", 10, 16, 250, 10, 3000,
     "Frequency at 100 % (full, 0.1 Hz)", "level", None, None),
    ("ALLOW_CAL_OBJECTS", 12, 8, 1, 0, 1,
     "Calibration through group objects (Home Assistant); stored values replace the two frequencies above",
     "level", None, None),
    ("LEVEL_CHANGE_PCT", 13, 8, 1, 0, 50,
     "Send the level on a change of (%, 0 = no)", "level", None, None),
    ("LEVEL_CYCLE_MIN", 14, 8, 30, 0, 255,
     "Send the level cyclically (min, 0 = no)", "level", None, None),
    ("FREQ_CHANGE_D10", 15, 8, 10, 0, 255,
     "Send the frequency on a change of (0.1 Hz, 0 = no)", "level", None, None),
    ("FREQ_CYCLE_MIN", 16, 8, 0, 0, 255,
     "Send the frequency cyclically (min, 0 = no)", "level", None, None),
    ("LOW_ALARM_PCT", 17, 8, 0, 0, 100,
     "Low-level alarm below (%, 0 = off)", "level", None, None),
    ("TANK_VOLUME_L", 18, 16, 0, 0, 65000,
     "Tank volume (l, 0 = don't send the volume)", "level", None, None),

    # Top-up valve (design.md "Firmware notes")
    ("TOPUP_ENABLED", 20, 8, 0, 0, 1,
     "Top-up valve control", "topup", None, None),
    ("TOPUP_ON_PCT", 21, 8, 10, 0, 99,
     "Open the valve below (%)", "topup", None, ("TOPUP_ENABLED", 1)),
    ("TOPUP_OFF_PCT", 22, 8, 12, 1, 100,
     "Close the valve at or above (%)", "topup", None, ("TOPUP_ENABLED", 1)),
    ("TOPUP_MAX_ON_MIN", 23, 8, 30, 1, 255,
     "Maximum opening time per fill, added up across interruptions; then lock and alarm until re-enabled (min)",
     "topup", None, ("TOPUP_ENABLED", 1)),
    ("TOPUP_RISE_PCT", 24, 8, 1, 0, 20,
     "While open, the level must rise by at least (%, 0 = no check)", "topup", None, ("TOPUP_ENABLED", 1)),
    ("TOPUP_RISE_MIN", 25, 8, 10, 1, 120,
     "... within (min)", "topup", None, ("TOPUP_ENABLED", 1)),
    ("TOPUP_REPEAT_S", 26, 8, 60, 0, 255,
     "Repeat ON while open, for an actuator channel with a staircase timer (s, 0 = no)", "topup", None,
     ("TOPUP_ENABLED", 1)),
]
PARAM_SIZE = 28

BLOCKS = [
    ("general", "General",
     "Garantia rainwater-tank transducer. The sensor frequency is measured against the NCN5130 crystal clock."),
    ("level", "Level",
     "Period-linear: level = (1/f - 1/f_empty) / (1/f_full - 1/f_empty). Defaults: 145 Hz empty, 25 Hz full."),
    ("topup", "Top-up valve",
     "Link the valve object to a switching actuator channel set to a staircase or timeout function (e.g. 5 min, "
     "at least twice the ON repeat time), so the valve closes by itself if this device or the bus fails while "
     "filling. Keep the rise percentage below the OFF-ON difference unless the tank fills fast. A lock is cleared "
     "by switching 'Top-up enable' off and on again. The valve stays closed for at least 60 s after every start "
     "and every close."),
]

# ---- group objects ----
# (name, number, text, dpt, size, flags, block, when)
# Flags: C communication, R read, W write, T transmit, U update. No I (read on init): thelsing would issue
# the reads by itself at every start. No U either: the device never reads these objects, and U would make any
# read answer on their group addresses act as a write (review F-04, O-2).
KOS = [
    ("LEVEL", 1, "Water level", "DPST-8-10", "2 Bytes", "CRT", "level", None),
    ("FREQUENCY", 2, "Sensor frequency", "DPST-14-33", "4 Bytes", "CRT", "level", None),
    ("VOLUME", 3, "Water volume", "DPST-12-1200", "4 Bytes", "CRT", "level", None),
    ("SENSOR_FAULT", 4, "Sensor fault", "DPST-1-5", "1 Bit", "CRT", "level", None),
    ("LOW_ALARM", 5, "Low-level alarm", "DPST-1-5", "1 Bit", "CRT", "level", None),
    ("CAL_F_EMPTY", 6, "Calibration: frequency empty", "DPST-14-33", "4 Bytes", "CRWT", "level",
     ("ALLOW_CAL_OBJECTS", 1)),
    ("CAL_F_FULL", 7, "Calibration: frequency full", "DPST-14-33", "4 Bytes", "CRWT", "level",
     ("ALLOW_CAL_OBJECTS", 1)),
    ("CAL_EMPTY_NOW", 8, "Calibration: tank is empty now (send 1)", "DPST-1-17", "1 Bit", "CW", "level",
     ("ALLOW_CAL_OBJECTS", 1)),
    ("CAL_FULL_NOW", 9, "Calibration: tank is full now (send 1)", "DPST-1-17", "1 Bit", "CW", "level",
     ("ALLOW_CAL_OBJECTS", 1)),
    ("VALVE", 10, "Top-up valve", "DPST-1-1", "1 Bit", "CRT", "topup", ("TOPUP_ENABLED", 1)),
    ("TOPUP_ENABLE", 11, "Top-up enable (off, then on, clears a lock)", "DPST-1-3", "1 Bit", "CRW", "topup",
     ("TOPUP_ENABLED", 1)),
    ("TOPUP_FAULT", 12, "Top-up fault", "DPST-1-5", "1 Bit", "CRT", "topup", ("TOPUP_ENABLED", 1)),
]
MAX_KO = max(k[1] for k in KOS)


def check():
    """Offsets inside the block, no overlaps, 16-bit values aligned, defaults in range, unique numbers."""
    used = {}
    names = {p[0] for p in PARAMS}
    for name, off, bits, dflt, lo, hi, text, block, enum, when in PARAMS:
        assert bits in (8, 16), name
        assert bits == 8 or off % 2 == 0, f"{name}: 16-bit parameter at an odd offset"
        assert off + bits // 8 <= PARAM_SIZE, f"{name}: outside the parameter block"
        for b in range(off, off + bits // 8):
            assert b not in used, f"{name} overlaps {used[b]}"
            used[b] = name
        if enum:
            assert dflt in [v for v, _ in ENUMS[enum]], name
            assert all(0 <= v < (1 << bits) for v, _ in ENUMS[enum]), name
        else:
            assert 0 <= lo <= dflt <= hi < (1 << bits), name
        assert block in [b[0] for b in BLOCKS], name
        assert when is None or when[0] in names, name
    nums = [k[1] for k in KOS]
    assert sorted(nums) == list(range(1, MAX_KO + 1)), "object numbers must be 1..MAX_KO without gaps"
    for k in KOS:
        assert k[7] is None or k[7][0] in names, k[0]
        assert set(k[5]) <= set("CRWTUI") and "I" not in k[5], k[0]


check()
