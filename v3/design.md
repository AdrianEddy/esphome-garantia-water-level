# garantia-knx-direct (V3): design

A KNX TP1 device that powers the Garantia rainwater-tank transducer and measures its frequency. On KNX it reports the level, the raw frequency, the volume and faults, takes its calibration from Home Assistant, and commands a top-up valve through a separate switching actuator. It replaces the V2 LM2917 board plus an analog KNX input (ABB AE/A 2.1).

The KiCad project in [`hardware/kicad/`](hardware/kicad/) is the source for the schematic and the layout. This document gives the values, the reasons behind them, the layout rules and the firmware design. The firmware source is in [`firmware/`](firmware/), the built firmware 1.2 in [`release/firmware-1.2/`](release/firmware-1.2/), and the JLCPCB package in [`fab/jlcpcb-2026-10-17-a/`](fab/jlcpcb-2026-10-17-a/).

Status: boards built and tested; everything works.

## The sensor

All values were measured with a scope on the installed transducer.

| Property | Value |
| --- | --- |
| Interface | 2-wire current loop. **RCA tip = +12 V feed, RCA shell = signal/return**, through a 110 Ω shunt to the controller's ground (2 × 220 Ω in parallel on the original controller) |
| Current | ≈ 27 mA average (3 V across 110 Ω), pulsed by ≈ ±8 mA (≈ 19–35 mA). The transducer looks like ≈ 340 Ω |
| Signal at the shunt | ≈ 1.7 V p-p, square-ish with sharp edges, on ≈ 3 V DC (≈ 2.2–3.9 V). The high level lasts longer: ≈ 57–65 % of the period |
| Frequency | **145 Hz empty** (pump limit), **25 Hz full** (overflow). More water, lower frequency. Residual water on the probe moves "empty" (125 Hz seen once, ≈ 3 % of level) |
| Level model | Linear in period: `level = (1/f − 1/f_empty) / (1/f_full − 1/f_empty)`. At 50 % that is ≈ 43 Hz; a frequency-linear sensor would read ≈ 85 Hz there |
| Supply | Needs a regulated 12 V. The original controller read unstable when its wall adapter sagged to 10 V |
| Cable | ≈ 17 m, partly buried under the driveway. Any 2-core cable will do at this impedance. The probe is an insulated red/white cord hanging in the water (the manual's error 701 is damaged insulation on the white wire) |
| Original controller | ATtiny2313: shunt → 10 kΩ → capacitor → BAV99 clamp → 10 k/10 k bias → internal analog comparator. Valve ON at 10 %, OFF at 12 %; a 24 V DC valve (≤ 1 A) behind a pipe interrupter for backflow separation |

## Architecture

```
 KNX bus (red/black) ──► NCN5130 ──► 3.3 V ──► STM32G0B1 ◄── XCLK 8 MHz from the NCN5130's crystal
                                                   ▲
                                                   │  LTV-817S optocoupler (5 kVrms)
                                                   │
 aux 30 V (yellow/white) ─► TMR 2412 ─► 12 V ─► RCA tip ─► sensor ─► RCA shell ─► 110 Ω shunt ─► LM2903
                            (1600 VDC)                         (floating sensor domain)
```

The board has three galvanically separate domains, kept ≥ 2 mm apart on every layer:
- **KNX island** (`GND_KNX`): bus-powered NCN5130 transceiver and the STM32G0B1KCU6, which is the KNX device.
- **Aux input** (`GND_AUX`): the 30 V auxiliary pair and the input side of the isolated converter PS1.
- **Sensor** (`GND_S`, floating): PS1's 12 V output, the RCA jack, the shunt and the comparator.

PS1 (1600 VDC) and the optocoupler U4 (5 kVrms) are the only parts that bridge domains.

### Why the sensor domain floats

- **aux− and bus− are joined inside the KNX power supply.** The Hager TYFS110 has one converter feeding C1, a 30 V unchoked output (yellow/white), and C2, the choked KNX bus, with a shared limit I(C1) + I(C2) ≤ 1280 mA ([manual 6LE008991D](https://assets.hager.com/step-content/P/HA_32108214/Document/std.lang.all/6LE008991D_TYFS110_B_A3P_10_LANGUES_PRINT_V4.pdf)). Hager doesn't say whether the two minuses are common, but one converter makes it near-certain. ABB states that its unchoked output "is not electrically isolated from the KNX voltage output" ([SV/S manual](https://library.e.abb.com/public/ffaa004475944f57bf685ac5f1d549aa/SVS_30xx1_PH_en_V1-2_2CDC501049D0201_Rev_A.pdf) §4.1). Weinzierl says aux− and bus− "are not the same and must not be connected to each other" ([DGS 366 manual](https://weinzierl.de/images/products/366/downloads/weinzierl-366-knx-powersupply-dgs-5207-manual-en.pdf)). So `GND_AUX` and `GND_KNX` must not meet on the board: that would give the bus return current a second path through the aux pair.
- **The sensor cable must stay off the KNX line.** A KNX SELV network must not be earthed (KNX TP1 installation guide, pp. 3–4), and bus devices are only tested to 2 kV core-to-earth (p. 23). Inputs that share the bus ground limit their wiring to 5–10 m (Jung 2076 5 m, MDT BE 10 m, ABB US/U 10 m); isolated inputs are rated for 50–100 m. This cable is 17 m long, buried, and ends in a probe in water. An insulation fault on it must not earth the KNX line.
- **A fault on the aux side can take the whole line down**, because C1 and C2 share one current limit. The aux input therefore has its own PTC (Sheet 1).
- **Pulse counting itself doesn't need a quiet ground.** The information is the time between 1.7 V edges, which an offset of tens of millivolts doesn't move. The isolation is for safety and for the power supply's rules.

The price of the floating domain is the analog diagnostics: "no pulses" catches an open cable, a shorted cable, a dead sensor and missing aux power, but can't tell them apart.

Alternatives that don't meet these constraints:
- **12 V from the bus through the NCN5130's DC2:** the cable sits on bus potential. The device would draw ≈ 18–32 mA (declared 40 mA, FANIN 10 kΩ), and a sensor short pulls VDD2 below its undervoltage threshold, which puts the NCN in Stand-By and the device off KNX unless an electronic fuse follows DC2.
- **A non-isolated buck from aux:** the cable sits on aux−, which is bus− inside the power supply.
- **An isolated module with the sensor on `GND_KNX`:** the cable is on bus potential again.
- **A linear regulator from aux:** a 78M12 is rated to 35 V only, and an LM317 in SOT-223 reaches ≈ 124 °C at 31 V / 50 mA.

### Key choices

| Topic | Choice | Reason |
| --- | --- | --- |
| Sensor 12 V | Traco **TMR 2412** (2 W), isolated and regulated, hand-soldered. The TMR 3-2412 (3 W) is a drop-in on the same footprint | 18–36 V in, no minimum load, 1600 VDC, continuous short-circuit protection. Its 167 mA covers a shorted sensor cable (≈ 115 mA in total), and it idles at 15 mA (TMR 3-2412: 18 mA) |
| Signal slicer | **LM2903** comparator on the sensor side, DC-coupled, with a self-averaging threshold and hysteresis. Its open-collector output drives the opto LED directly | Works the same from 25 to 145 Hz; an AC-coupled one-transistor edge detector's coupling capacitor would recover for only ≈ 4.5 ms between pulses at 145 Hz. LM2903 rather than LM393 because it is rated to −40 °C and the garage freezes |
| Signal crossing | LTV-817S optocoupler; STM32 TIM2 input capture on the island side | The only signal across the barrier |
| KNX side | The knx-rgbic island: NCN5130 (bus-powered, DC1 3.3 V, FANIN open, 10 mA class) + STM32G0B1KCU6 running thelsing/knx | A proven design (knx-device-playbook) |
| Timing reference | NCN5130 XCLK (8 MHz, derived from its 16 MHz crystal in Normal state) → R17 33 Ω → STM32 PA15 = TIM2_ETR | HSI16 drifts by up to ≈ 2 % over temperature, and every 1 % of clock error is ≈ 1.2 % of level at the full end. The crystal is ≤ 50 ppm. PA15 has no role in the ROM bootloader |
| Shunt | 2 × 220 Ω 1 % 1 W 2512 in parallel | A tip-to-shell short puts 12 V across it (109 mA, 1.3 W), possibly indefinitely. A PTC wouldn't trip reliably at that current, so the shunt carries it (0.65 W each) |
| RCA jack | Keystone 900 | Proven footprint from the V2 board. Its steel shell carries `SENS_SIG`, which is fine because the sensor domain floats |
| Programming | ROM USB DFU on PA11/PA12 through a 3-pad 2.54 mm header (D−, D+, GND); a USB-C breakout is soldered on for flashing | No programming chip on the board |
| Calibration | f_empty and f_full set in ETS, and also writable or teachable from Home Assistant through group objects | The installation's frequencies drift with the probe and cable |
| Top-up valve | A separate KNX switching actuator, linked by group objects in ETS, its channel set to a staircase/timeout function | The valve closes by itself if this device or the bus fails while filling |
| Assembly | Double-sided JLC assembly; THT parts hand-soldered on top | Smallest board |
| PCB | 4 layers, 1.6 mm, 1 oz; **28 × 46 mm**, 1 mm corner radius; no mounting holes | Three domains plus the island planes; see "Floorplan" |

## Domains and net names

| Domain | Ground | Nets (prefixes) | Contents |
| --- | --- | --- | --- |
| KNX island | `GND_KNX` | `+3V3_KNX`, `VBUS1`, `KNX_*`, `MCU_*` | J2, Sheet 2, Sheet 3, R7, U4 output side |
| Aux input | `GND_AUX` | `AUX_*` | J1, Sheet 1 input side, PS1 pins 1–3 |
| Sensor | `GND_S` | `+12V_S`, `SENS_*` | PS1 pins 6–7, Sheet 4, U4 LED side |

Every net has an explicit name, so the isolation rules can match it by pattern; no auto-named `Net-(…)` net crosses a domain. KiCad prefixes local labels with the sheet path (`/1 Aux power/AUX_V`, `/4 Sensor/SENS_SIG`); only power symbols and global labels keep bare names. The exact patterns are in "PCB layout rules".

Ground and supply symbols: `GND_KNX` = power:GNDD, `GND_AUX` = power:GNDPWR, `GND_S` = power:GNDREF, `+3V3_KNX` = power:+3V3, `+12V_S` = power:+12V, each with its Value renamed.

## Sheet 1: Aux input and isolated 12 V

`AUX_IN` comes from the yellow/white pair: the TYFS110's output C1, 30 V DC nominal, ≤ 32 V. Pin 1 is + (yellow by convention). The KNX standard doesn't fix the aux polarity, so check it with a meter; D1 makes a swap harmless.

| Ref | Value | Part / LCSC | Package | Connection | Note |
| --- | --- | --- | --- | --- | --- |
| J1 | aux plug pins | 2 × WAGO 243-131 (C20252226) for a **WAGO 243-212** yellow/white plug | `garantia:KNX_BusPins_2x1.0mm_P5.75mm_WAGO_243-211_Top` (same pins as J2) | 1 `AUX_IN` (yellow, tongue end), 2 `GND_AUX` | Hand-fitted, DNP. The plug isn't at LCSC; Bürklin or Farnell have it |
| F1 | PTC 100 mA hold, 60 V | BHFUSE BSMD1206-010-60V (C883116) | 1206 | `AUX_IN` → `AUX_F` | Keeps a fault on this board off the TYFS110's shared 1280 mA limit. Normal draw ≈ 30 mA. A failed part between F1 and PS1 (D2, C1 or C2 shorted, a solder bridge) loads that limit until F1 trips (tens of ms) |
| D1 | SS16 | MDD SS16 (C2481) | SMA | `AUX_F` → `AUX_P` | Reverse polarity |
| D2 | SMAJ33A | MDD SMAJ33A (C113966) | SMA | `AUX_P` → `GND_AUX` | After D1, so reverse polarity never stresses it. The 33 V standoff sits above the ≤ 32 V aux. At the currents F1 and the cable allow, it clamps at ≈ 40–45 V, under the TMR's 50 V / 100 ms surge rating (the 53.3 V datasheet clamp is at 7.5 A) |
| C1 | 4.7 µF 50 V X7R | Murata GRM32ER71H475KA88L (C86052) | 1210 | `AUX_P` → `GND_AUX` | π-filter input side |
| L1 | 22 µH, 340 mA, 2.3 Ω | APV AHW2012FE220KTF (C6364733) | wire-wound "0805", body ≈ 2.3 × 1.7 mm; `garantia:L_APV_AHW2012FE` (JLC's land pattern: 1.02 × 1.78 mm pads at ±0.90 mm) | `AUX_P` → `AUX_V` | Keeps the module's ≥ 100 kHz input ripple off the yellow/white pair, which runs alongside the bus pair in the same cable |
| C2 | 4.7 µF 50 V X7R | same as C1 | 1210 | `AUX_V` → `GND_AUX` | At PS1 pins 1/2 |
| PS1 | Traco **TMR 2412** (or TMR 3-2412) | not at LCSC | SIP-8 (21.8 × 9.1 × 11.2 mm), stock `Converter_DCDC_TRACO_TEC2-12xxWI_24xxWI_48xxWI_THT` (same pin pattern and body outline) | 1 −Vin `GND_AUX`; 2 +Vin `AUX_V`; 3 Remote **no connect**; 5, 8 NC; 6 +Vout `+12V_S`; 7 −Vout `GND_S` | Hand-soldered, DNP for JLC |
| C3 | 10 µF 25 V X5R | Samsung CL21A106KAYNNNE (C15850, basic) | 0805 | `+12V_S` → `GND_S` | Near PS1 pins 6/7 on the bottom, ≈ 19 mm of track from pin 6. The TMR needs no output capacitor |

**PS1** ([TMR 3 datasheet](https://www.tracopower.com/sites/default/files/products/datasheets/tmr3_datasheet.pdf); the TMR 2 is the same family):
- 18–36 V in, 50 V surge for 100 ms; 12 V out, regulated (±1 % set, 0.2 % line, 0.5 % load); **no minimum load**.
- At this board's ≈ 0.35 W it runs at ≈ 45–50 % efficiency because of its no-load current, so it takes ≈ 28–30 mA from the aux line.
- Continuous short-circuit protection with automatic recovery. 1600 VDC functional isolation.
- Pin 3 (Remote): open = on; 2–4 mA into −Vin switches it off. It is a live pin referenced to −Vin, so it belongs to the aux domain. There is no pin 4.
- Output capacitance ≤ 170 µF (TMR 2), ≤ 820 µF (TMR 3).
- "Avoid routing PCB traces under the converter": nothing runs under its body.
- Rated for wave soldering at 260 °C / 6 s, so hand soldering is fine.

**π-filter (C1/L1/C2).** With C2 ≈ 3 µF effective at 30 V bias, L1 and C2 resonate at ≈ 20 kHz. The series damping (F1, D1, L1's DCR, the cable) gives Q ≈ 0.5–0.9, so the filter's output impedance peaks at ≈ 1–2.5 Ω, far below the module's negative input resistance: stable with a large margin. Attenuation is ≈ 26× at the module's ≥ 100 kHz switching frequency.

## Sheet 2: KNX bus interface (NCN5130)

The knx-rgbic island's bus interface, with values from the NCN5130 datasheet's Table 6. Parts marked "window" must stay inside their KNX window at any bias and temperature (knx-device-playbook `references/island-hardware.md`). Ground is `GND_KNX` only.

| Ref | Value | Part / LCSC | Package | Connection | Note |
| --- | --- | --- | --- | --- | --- |
| J2 | KNX bus plug pins | 2 × WAGO 243-131 (C20252226) for a WAGO 243-211 red/grey plug (C19633675) | `garantia:KNX_BusPins_2x1.0mm_P5.75mm_WAGO_243-211_Top` | 1 `KNX_P` (red, tongue end), 2 `GND_KNX` | Hand-fitted, DNP |
| D3 | SMAJ40CA | C113970 | SMA | `KNX_P` ↔ `GND_KNX`, at J2 | Bidirectional TVS |
| D4 | SS16 | C2481 | SMA | `KNX_P` → `VBUS1` | Mandatory reverse-polarity diode |
| R1 | 27 Ω ±1 % 1 W | Yageo RC2512FK-0727RL (C877905) | 2512 | `VBUS1` → TXO (3) | Window 24.3–29.7 Ω |
| C4 | 47 nF C0G 50 V | Murata GRM31M5C1H473JA01L (C21812) | 1206 | `VBUS1` → CCP (4) | Window 42.3–51.7 nF; VBUS1–CCP trace under 5 mm |
| C5 | 220 nF C0G ±5 % 50 V | Murata GRM31C5C1H224JE02L (C464977) | 1206 | CEQ1 (7) ↔ CEQ2 (8) | Window 198–242 nF |
| C6 | 100 nF C0G ±5 % 50 V | Walsin 1206N104J500CT (C170182) | 1206 | CAV (5) → `GND_KNX` | Window 80–120 nF |
| C7 | 150 µF 35 V hybrid polymer | Panasonic EEH-ZA1V151P (C341199) | Ø8 × 10.2 mm | VFILT (9) → `GND_KNX` | ≥ 80 µF effective; no solid polymer (leakage). Tall: top side |
| C8 | 100 nF 50 V X7R | Yageo CC0603KRX7R9BB104 (C14663, basic) | 0603 | VFILT → `GND_KNX`, at U2 | |
| L2 | 220 µH, Isat ≥ 250 mA, shielded | Sunlord SWPA5040S221MT (C370497) | 5 × 5 × 4 mm | VSW1 (17) → VDD1M (20) | DC1. Tall: top side, with the DC1 loop closed through two short vias |
| R2 | 1 Ω | FOJAN FRC0402F1R00TS (C2909329) | 0402 | VDD1M (20) → VDD1 (19) | DC1 current sense |
| C9 | 22 µF 10 V X7R | Murata GRM21BZ71A226ME15L (C907991) | 0805 | `+3V3_KNX` → `GND_KNX` | VDD1 needs ≥ 8 µF effective |
| C10, C11 | 100 nF | Samsung CL05B104KB54PNC (C307331, basic) | 0402 | VDDA (40), VDDD (30) → `GND_KNX` | |
| Y1 | 16 MHz, CL 10 pF | YXC X322516MMB4SI (C112972) | 3225 4-pad | XTAL1 (35) → pad 1, XTAL2 (34) → pad 3; pads 2/4 → `GND_KNX` | ≤ 50 ppm total |
| C12, C13 | 12 pF C0G | FH 0402CG120J500NT (C1547) | 0402 | XTAL pins → `GND_KNX` | The routed XTAL1/XTAL2 tracks are 5.17 and 4.89 mm (≈ 0.04 pF apart), so both sides take the same value |
| R3, R4 | 10 kΩ | UNI-ROYAL 0402WGF1002TCE (C25744, basic) | 0402 | `KNX_SAVEB`, `KNX_RESETB` → `+3V3_KNX` | Open-drain outputs |
| U2 | NCN5130MNTWG | C513000 | QFN-40 6 × 6, `knx-rgbic:onsemi_QFN-40-1EP_6x6mm_P0.5mm_EP3.2x3.2mm_ThermalVias` | see straps | 9 vias in the exposed pad |
| R17 | 33 Ω | UNI-ROYAL 0402WGF330JTCE (C25105, basic) | 0402 | XCLK (32) = `XCLK_OUT` → `KNX_XCLK` | Source resistor right at U2 pin 32: softens the 8 MHz edges next to the KNX front end |

**Straps:**
- TREQ (25), MODE1 (23), MODE2 (24), XSEL (33), **XCLKC (21)** → `GND_KNX`. XCLKC low gives an 8 MHz XCLK.
- CSB/UC1 (26) and SCK/UC2 (29) → `GND_KNX` as **hard ties** (no internal pull-downs): 19 200 baud, 9-bit mode.
- V20V (10) and VIN (16) → VFILT.
- VDD2MV (11) → VDD1; VDD2 (13), VDD2MC (12), VSW2 (15) not connected (DC2 unused).
- FANIN (38) open.
- TRIG (22) and ANAOUT (39) not connected.
- VSSA (1), VBUS2 (2), VSS2 (14), VSS1 (18), VSSD (31) and the exposed pad → `GND_KNX`.

**Signals to Sheet 3:**
- SDO/TXD (28) → `KNX_TXD`; SDI/RXD (27) ← `KNX_RXD`.
- SAVEB (36) → `KNX_SAVEB`; RESETB (37) → `KNX_RESETB`.
- XCLK (32) → R17 → `KNX_XCLK`. The net between U2 and R17 is the local `XCLK_OUT`.

Apart from R3/R4's two pins (swapped, equivalent) and the XCLK connection, the island is pin for pin the knx-rgbic double-sided island.

## Sheet 3: Island MCU (STM32G0B1KCU6)

| Ref | Value | Part / LCSC | Package | Connection | Note |
| --- | --- | --- | --- | --- | --- |
| U3 | STM32G0B1KCU6 | C5270237 | UFQFPN-32 5 × 5 | see "U3 pin map" | The KCU7 (C5270235) is a drop-in; KEU6, KBU6 and the "N" variants are not. Stock is often low: check before ordering |
| C14 | 100 nF | C307331 | 0402 | VDD (4) → VSS (5) | |
| C15 | 4.7 µF 16 V X5R | Samsung CL10A475KO8NNNC (C19666, basic) | 0603 | VDD → VSS | DS13560 Fig. 15 |
| C16 | 100 nF | C307331 | 0402 | NRST (6) → `GND_KNX` | Never pulse NRST on a blank chip |
| R5 | 10 kΩ | C25744 | 0402 | `KNX_RXD` → `+3V3_KNX` | The NCN's RXD idles high while U3 is in reset or blank |
| R6 | 1 kΩ | UNI-ROYAL 0402WGF1001TCE (C11702, basic) | 0402 | PB5 → D5 | ≈ 1.3 mA |
| D5 | red LED, programming | KENTO KT-0603R (C2286, basic) | 0603 | R6 → `GND_KNX` | Top side |
| SW1 | programming button | XUNPU TS-1088-AR02016 (C720477, basic) | 3.9 × 3 × 2 mm SMD, `Button_Switch_SMD:SW_SPST_TS-1088-xR020` | PB4 → `GND_KNX` | Internal pull-up; rated −30…+80 °C. Functions in "Firmware notes". Top side |
| R7 | 10 kΩ | C25744 | 0402 | `MCU_PULSE` → `+3V3_KNX` | Optocoupler pull-up |
| C17 | 1 nF 50 V | FH 0402B102K500NT (C1523, basic) | 0402 | `MCU_PULSE` → `GND_KNX` | EMI filter, 10 µs with R7 |
| J3 | USB DFU pads | footprint only | 1 × 3, 2.54 mm, plated holes | 1 `MCU_USB_DM` (PA11), 2 `MCU_USB_DP` (PA12), 3 `GND_KNX` | At the board edge; see below |
| TP1, TP2, TP5 | SWD pads | — | bottom pads | PA13 SWDIO, PA14 SWCLK, `GND_KNX` | ST-Link fallback if a broken boot manager ever blocks USB DFU (connect without reset; 3.3 V from the board) |
| TP7 | debug UART pad | — | bottom pad | PA2 (USART2_TX) | Log output, with TP5 as ground; isolated USB-UART or floating supply only |

**J3:**
- The pad order matches a USB-C breakout's D−, D+, GND; the breakout's VCC pin overhangs the board edge unconnected. No pull-ups on PA11/PA12 (the ROM bootloader's USB needs none).
- A breakout without 5.1 kΩ CC resistors needs a USB-A-to-C cable.
- **J3's GND is KNX−.** An earthed PC would earth the SELV bus, which the KNX TP1 guide forbids. Flash on a bench supply, or from a floating host (a laptop on battery, a USB isolator).
- Fit the breakout with its receptacle pointing away from J4 (towards U3) and its metal ≥ 2 mm from J4's body, because J4's shell is in the sensor domain. Or take it off after flashing.

### U3 pin map

| Pin | Name | Net | Function |
| --- | --- | --- | --- |
| 4, 5, 33 | VDD, VSS, EP | `+3V3_KNX`, `GND_KNX`, `GND_KNX` | |
| 6 | PF2-NRST | `MCU_NRST` | C16 |
| 7 | PA0 | `KNX_SAVEB` | input |
| 8 | PA1 | `KNX_RESETB` | input |
| 9 | PA2 | `MCU_DBG_TX` | USART2_TX → TP7 |
| 22, 23 | PA11, PA12 | `MCU_USB_DM`, `MCU_USB_DP` | USB DFU → J3 |
| 24, 25 | PA13, PA14 | `MCU_SWDIO`, `MCU_SWCLK` | TP1/TP2 |
| 26 | PA15 | `KNX_XCLK` | TIM2_CH1_ETR (AF2): TIM2's external clock (DS13560 Table 13) |
| 27 | PB3 | `MCU_PULSE` | TIM2_CH2 (AF2) input capture ← U4 collector |
| 28 | PB4 | `MCU_BTN` | SW1 |
| 29 | PB5 | `MCU_LED_PROG` | R6/D5 |
| 30 | PB6 | `KNX_RXD` | USART1_TX (AF0) → NCN RXD |
| 31 | PB7 | `KNX_TXD` | USART1_RX (AF0) ← NCN TXD |
| 19, 21 | PA9, PA10 | open | UCPD dead-battery pins; left open |
| others | | no connect | firmware sets them analog |

Pins and the ROM bootloader (AN2606):
- PA0, PA1, PA15, PB3, PB4 and PB5 have no bootloader role. PA4–PA7 (the bootloader's SPI1) and PA3 (its USART2 RX) stay unconnected.
- PB6/PB7 are the bootloader's I2C1 pins. That is harmless: SCL is the MCU's own TX line, held high by R5 and driven by nobody in bootloader mode, so the NCN's TXD toggling SDA only makes start/stop conditions, never an address.

## Sheet 4: Sensor front end (floating domain)

Ground is `GND_S` only, supply `+12V_S`. Nothing on this sheet touches `GND_KNX` or `GND_AUX` except through U4 (5 kVrms) and PS1 (1600 VDC).

| Ref | Value | Part / LCSC | Package | Connection | Note |
| --- | --- | --- | --- | --- | --- |
| F2 | 0 Ω link | UNI-ROYAL 1206W4F0000T5E (C17888, basic) | 1206 | `+12V_S` → `SENS_FEED` | A fuse position fitted with a link. The TMR's short-circuit protection covers a `SENS_FEED`–`GND_S` short, and R8/R9 carry a tip-to-shell short. A 50 mA PTC would reach ≈ 50 Ω after a trip and take ≈ 1.75 V off the feed. BSMD1206-010-60V (C883116, as F1) fits if a PTC is wanted |
| D6 | SMAJ15A | hongjiacheng (C19077535) | SMA | `SENS_FEED` → `GND_S` | On the strip right of U5, ≈ 22 mm of track from J4 (there is no `GND_S` near the jack) |
| J4 | RCA jack | **Keystone 900** | THT, `garantia:KEYSTONE_900` | pad 1 (centre) `SENS_FEED`; S1, S2, S3 (steel shell legs) `SENS_SIG` | Hand-fitted, DNP. **The shell is the signal**: metal that floats with the sensor domain, so it keeps the 2 mm gap to the other domains |
| R8, R9 | 220 Ω 1 % 1 W | UNI-ROYAL 25121WF2200T4E (C421609) | 2512 | `SENS_SIG` → `GND_S`, in parallel | 110 Ω shunt. 40 mW each normally, 0.65 W each with a shorted cable |
| D7 | SMAJ15A | C19077535 | SMA | `SENS_SIG` → `GND_S` | Stays off at 12 V during a cable short. Next to D6, ≈ 10 mm of track from J4's leg S2 |
| R10 | 10 kΩ | C25744 | 0402 | `SENS_SIG` → `SENS_INP` (U5 IN+A, 3) | Upper half of the IN+ divider; limits current into the input clamp |
| R15 | 10 kΩ | C25744 | 0402 | `SENS_INP` → `GND_S` | Lower half: IN+ ≈ `SENS_SIG`/2 |
| C18 | 1 nF | C1523 | 0402 | `SENS_INP` → `GND_S` | ≈ 5 µs filter with R10 ∥ R15 |
| R11 | 1 MΩ | UNI-ROYAL 0402WGF1004TCE (C26083, basic) | 0402 | `SENS_OUT` (1) → `SENS_INP` (3) | Hysteresis ≈ 11.9 V × 5 k / 1.005 M ≈ 59 mV at the pin, ≈ 120 mV p-p at the shunt |
| R12 | 100 kΩ | UNI-ROYAL 0402WGF1003TCE (C25741, basic) | 0402 | `SENS_SIG` → `SENS_TH` (U5 IN−A, 2) | Upper half of the averaging divider |
| R16 | 100 kΩ | C25741 | 0402 | `SENS_TH` → `GND_S` | Lower half: IN− ≈ mean(`SENS_SIG`)/2, from a 50 kΩ source |
| C19 | 10 µF 25 V X5R | Samsung CL21A106KAYNNNE (C15850, basic, as C3) | 0805 | `SENS_TH` → `GND_S` | τ = 50 kΩ × 10 µF = 0.5 s, ≫ the 40 ms period |
| U5 | LM2903DR2G dual comparator, −40…105 °C | onsemi (C57474) | SOIC-8 | 8 VCC `+12V_S`; 4 GND `GND_S`; A: 3 IN+ `SENS_INP`, 2 IN− `SENS_TH`, 1 OUT `SENS_OUT`; B: 5 IN+ → `GND_S`, 6 IN− → `+12V_S`, 7 OUT NC | Output high-Z while the sensor current is high, low while it is low. Unit B is tied so its output is defined low (IN+ < IN−; one input in range is enough, TI note 2) |
| C20 | 100 nF 50 V | C307331 | 0402 | `+12V_S` → `GND_S` | Under U5, across pins 6 (+12 V) and 5 (GND); pins 6 and 8 (VCC) share one short track |
| R13 | 10 kΩ | C25744 | 0402 | `SENS_OUT` → `+12V_S` | Defines the output-high level for the hysteresis |
| R14 | 4.7 kΩ | UNI-ROYAL 0402WGF4701TCE (C25900, basic) | 0402 | `+12V_S` → `SENS_LEDA` | ≈ 2.3 mA into the opto LED: (12 − 1.2 − 0.2) V / 4.7 kΩ. At the lowest CTR that still sinks ≥ 6× R7's 0.33 mA |
| U4 | LTV-817S-TA1-C optocoupler | Lite-On (C109227, basic) | SMD-4, `Package_DIP:SMDIP-4_W9.53mm` | 1 anode `SENS_LEDA`; 2 cathode `SENS_OUT`; 4 collector `MCU_PULSE`; 3 emitter `GND_KNX` | 5 kVrms; CTR 200–400 %. Straddles the sensor ↔ island gap. The LED is on while the sensor current is low. JLC's footprint has 10.0 mm pad rows against KiCad's 9.53 mm; both cover the gull-wing leads and leave > 7 mm across the barrier |

There is no "sensor pulses" LED.

**Comparator.** `SENS_SIG` = 27 mA × 110 Ω ≈ 3.0 V mean. The signal swings +0.75 V / −1.0 V about the mean, so the high level (≈ 3.75 V) is the longer one, ≈ 57–65 % of the period. The dividers (≈ 18 kΩ across the shunt) lower it by < 1 %.
- **Thresholds at `SENS_SIG`:**
  - output high → low below ≈ 2.90–2.94 V;
  - output low → high above ≈ 3.01–3.06 V.
  - The spread is the LM2903's input bias current (≤ 500 nA over temperature, TI SLCS005) into IN−'s 50 kΩ source: ≤ 25 mV at the pin, ≈ 45 mV at the shunt.
- **Margins:** ≈ 0.7 V to the high level and ≈ 0.9 V to the low level.
- **Ripple on IN−:** ≈ 17 mV p-p at 25 Hz at the pin (≈ 34 mV at the shunt), well inside the hysteresis.
- **Start-up:** C19 charges with τ = 0.5 s. Pulses start after ≈ 0.6 s and the threshold has settled after ≈ 2.5 s, so the firmware ignores the first 3 s of pulses.
- **Cable faults:** both inputs stay ≤ 6 V, inside the 0…V_CC − 2 V common-mode range, in every cable state.
  - Tip-to-shell short: `SENS_SIG` ≈ 12 V, both inputs ≈ 6 V. The output sits static; the firmware sees no edges and reports a sensor fault.
  - Open cable: `SENS_SIG` = 0, both inputs ≈ 0 V. Static, same result.

**Sensor-domain current:** sensor 27 mA + opto LED ≈ 1 mA average (on ≈ 35–43 % of the time) + R13 ≈ 0.5 mA + U5 ≈ 0.5 mA ≈ 29 mA at 12 V (≈ 0.35 W). The dividers take their ≈ 0.17 mA from the sensor current, not on top of it.

## Bus and aux budget

| Load | Value |
| --- | --- |
| KNX bus | Island ≈ 3–4 mA at 16 MHz + XCLK drive ≈ 0.05 mA + R7 ≈ 0.1 mA average; D5 only in programming mode. Within the 10.6 mA FANIN-open limit; the device declares BusCurrent = 10 mA |
| Aux (TYFS110 C1) | ≈ 28–30 mA at 30 V (PS1's no-load current, 15 mA for the TMR 2412 or 18 mA for the TMR 3-2412, plus the ≈ 0.35 W load); 160 mA at most into a shorted output. Shares the TYFS110's 1280 mA with the bus |

## Floorplan

Board frame x 100–128, y 100–146 (KiCad mm, y down), 28 × 46 mm with a 1 mm corner radius. PS1 bridges aux and sensor, so its input end (pins 1–3) faces J1 and its output end (pins 6–7) faces J4. The island is one contiguous region that includes J2.

- **Top edge:** J2 (bus, left) and J1 (aux, right) as a **dovetailed WAGO pair at 11.5 mm pitch**, their wire-entry faces 0.3 mm over the top edge. Seen from the top with the wires going up, each plug has + on its left pin and the dovetail tongue on its left side. From left to right the pins are `KNX_P`, `GND_KNX`, `AUX_IN`, `GND_AUX`, all 5.75 mm apart at y = 106.35 (3.25 mm copper gap between the domains). J2's tongue sits 0.6 mm from the left edge, and J1's tongue sits in J2's groove. Their courtyards overlap by 2.1 mm there; that is intended and excluded once in DRC.
  - The `_Top` footprint is the mirror image of knx-rgbic's `KNX_BusPins_2x1.0mm_P5.75mm_WAGO_243-211`, which is drawn as seen from below because knx-rgbic places it on B.Cu.
  - The two plugs use the same footprint and mechanically identical bodies. A bus plug in J1 is harmless but loads the bus with ≈ 30 mA plus C1/C2. A dovetailed pair rotated 180° reverses both, and D1/D4 block that.
- **Right column, top side:** PS1 vertical, pin line at x = 123.5, pin 1 (−Vin) at y = 113.1. The body extends left of the pins, so the pin column is 4.5 mm from the right edge.
- **Bottom-left corner:** J4 at (107.6, 138.4), the barrel out of the bottom edge (2.2 mm of it over the board).
- **KNX island** (left two thirds, above J4):
  - **Bottom: the KNX front end and the NCN5130.** In order from J2: D3/D4 under J2, R1 at y 113.18, the bus caps C6/C4/C5 in a row at y 116.35, then U2 at (109.0, 122.5) with its bus pins facing them across a 2.1 mm fan-out band. Around U2: Y1 with C12/C13 on its XTAL side, C8/R2/C9 on its DC1 side, C10 at pin 40, C11 and R17 below its bottom-left corner, R5 at its RXD pin. The four test pads (TP1, TP2, TP5, TP7) in the bottom-left corner, under U3.
  - **Top: the STM32 and the tall parts.** C7 under the plugs; L2 right above U2's DC1 pins. U3 at (104.0, 127.6), rotated 270°: its USB pins face J3 below it, its PB6/PB7 column faces U2's RXD/TXD pins, and its top row (VDD, NRST, PA0/PA1) faces C14, C16 and R3/R4. C15 and R3/R4 sit above it at y 120.0–120.45. J3 at y 132.6 (pin 1 = D− 1.9 mm from the left edge, so the breakout's VCC pin overhangs the edge). SW1 and D5 right of U3.
- **Aux domain**, bottom side under J1 and PS1's input end: F1 vertical by J1's + pin, D1 horizontal, D2 vertical at the right edge (next to J1's − pin) along the top edge; C1, L1 and C2 below the pins at PS1 pins 1/2.
- **Sensor domain:**
  - bottom: under PS1's lower half (F2, C3), plus the strip right of J4 (U5, D6, D7, R13, R14, C20);
  - top: the corner right of J4 (R8, R9 and the comparator network R10–R12, R15, R16, C18, C19).
- **U4** on the bottom at (114.6, 132.3), transistor pads in the island and LED pads under PS1's lower half. Its pad rows are 9.5 mm apart.
- **Closest cross-domain pads:** J4 S2 to U4 pin 3 at 2.35 mm, C5.2 to C2.2 at 2.40 mm, PS1 pin 3 to F2 at 2.54 mm, J2 pin 2 to J1 pin 1 at 3.25 mm.

**Assembly sides.** Top: the THT parts, hand-soldered (J1, J2, PS1, J4, plus the J3 pads), and JLC-placed C7 and L2 (tall), U3 with C14–C16, R3, R4 and R6, SW1 and D5 (reachable), and the shunt and comparator passives beside J4 (R8–R12, R15, R16, C18, C19): 20 parts. Bottom: all other SMD, 34 parts.

## PCB layout rules

- **Clearance ≥ 2 mm on every layer** between any two domains, for copper and plated-hole walls. Implemented in `garantia-knx-direct.kicad_dru` as two rules on `NetName` patterns, last in the file: "KNX island vs everything else" and "aux vs sensor". They are `NetName` patterns in `.kicad_dru` rather than netclasses, because netclass patterns live in `.kicad_pro`, which KiCad overwrites while the project is open. Each of the 69 nets matches exactly one set:
  - **KNX:** `/2 KNX bus interface/*`, `/3 MCU/*`, `GND_KNX`, `+3V3_KNX`, `KNX_*`, `MCU_PULSE`, `unconnected-(U2-*`, `unconnected-(U3-*`.
  - **AUX:** `/1 Aux power/*`, `GND_AUX`, `unconnected-(PS1-Remote*` (PS1 pin 3 is live, referenced to −Vin).
  - **SENS:** `/4 Sensor/*`, `GND_S`, `+12V_S`, `unconnected-(U5-*` (U5 pin 7 is a live comparator output), `unconnected-(PS1-NC*` (PS1 pins 5/8 sit among the output pins).
  - **No exemptions.** Any new net must match exactly one set. After an edit to `.kicad_dru`, prove the rules again with planted cross-domain tracks on a copy of the board, on all four layers.
- **Aligned gap bands.** The pour outlines give every domain pair one 2 mm band that is the same on all four layers, so copper of different domains never overlaps across a 0.1 mm prepreg (F/In1, In2/B):
  - KNX | AUX: x 112.5 | 114.5 near the plugs, 114.95 | 116.95 below them;
  - KNX | SENS: x 116.1 | 118.25, and y 133.6 | 135.6 above J4;
  - AUX | SENS: y 118.9 | 120.9.
  - The In2 J4 pour is notched 2 mm around U4 pin 3.
- **Other rules in `.kicad_dru`:** 0.15 mm clearance only for copper touching U2's or U3's courtyard (QFN fan-out); solid pour connections to SMD pads and U2's exposed-pad vias; 0.5 mm thermal spokes (0.3 mm gap) for J1–J4 and PS1, except J4's shell legs, which connect solid ("J4 shell solid"). Everything else uses the Default netclass (0.2 mm).
- **Island** (knx-device-playbook "Layout"):
  - VBUS1–CCP under 5 mm; R1/C4/C5/C6 tight; CCP 0.35 mm wide outside the fan-out band, VBUS1 0.3 mm. CAV hops over CCP on F.Cu through two vias; the rest of the front end is on B.Cu.
  - Small DC1 loop; crystal tight, with ground around it.
  - 9 vias in U2's exposed pad. U3's first exposed-pad via sits on a paste-gap intersection, (103.425, 127.025), so its drill clears all nine paste windows; the second, at (105.45, 128.15), touches two window edges.
  - In1 = `GND_KNX` plane; In2 = `+3V3_KNX` under the island.
  - R17 right at U2 pin 32. `KNX_XCLK` is short and kept away from VBUS1/CCP/TXO/CEQ/CAV, the crystal and J2. Its B.Cu part runs over the In2 `+3V3_KNX` plane, its F.Cu part over In1 `GND_KNX`.
- **Sensor domain:** its own `GND_S` pours. The shunt return (R8/R9 → `GND_S` → PS1 pin 7) is short and doesn't run under U5's inputs. `SENS_FEED` reaches J4's centre pin on In2, not on F.Cu under the jack's body.
- **J4 is anchored for cable pull.** Its three steel shell legs (S1–S3, `SENS_SIG`) sit in a solid `SENS_SIG` pour on all four layers (zones `SENS_SIG_J4_*`, x 100–115.2, y ≥ 135.6, priority 2), connected without thermal spokes. 21 stitching vias (0.6/0.3 mm, each tied to its leg by a 0.4 mm stub) rivet the pours together around the legs. The F.Cu pour reaches x 118.3, so R8/R9's shunt ends sit in copper for a shorted cable. No pour of another net under J4. Pin 1 (`SENS_FEED`) keeps a 0.25 mm clearance ring.
- **Aux:** C1/L1/C2 next to PS1 pins 1/2; D2 next to J1.
- **U4 and PS1** are the only parts straddling a gap. Nothing runs between U4's pad rows (x 110.95–118.25, y 129.9–134.7) on In1, In2 and B.Cu: the plane outlines leave that rectangle out, and no track or via enters it. On F.Cu SW1 sits above U4, under the ordinary 2 mm domain rule.
- **J3** at a board edge; D−/D+ short and parallel.
- **Silkscreen:** 1.0 mm text, 0.16 mm stroke, each text ≥ 0.2 mm from every pad. Reference designators are hidden.
  - Top: "KNX red/blk" and "AUX yel/wht" between each plug's pins, under the plug body, visible while plugging in.
  - Bottom (mirrored): "D-", "D+", "G" under J3's pins; "IO", "CK", "G", "TX" at TP1, TP2, TP5, TP7; "GARANTIA KNX" / "v3" and "by Eddy" along the free strip under PS1.
- **Hand-soldering:**
  - J4's shell legs sink into four copper layers: use a hot iron (≥ 60–80 W).
  - Tight joints: J4.S2 (0.71 mm from U5.1), J1.1 (0.61 mm from C1.1) and J2.2 (0.90 mm from D4.1). Solder them with the tip on the side away from the SMD pad, and inspect for bridges.
  - Before the first plug-in, check with an ohmmeter for shorts: `KNX_P`/`VBUS1`–`GND_KNX`, `AUX_V`–`GND_AUX`, `SENS_OUT`–`SENS_SIG` and PS1.3–PS1.2.
- **U4 and reflow:** Lite-On recommends one reflow for the LTV-817S. U4 is on the bottom; with C7 and L2 on top, JLC probably reflows the top last, so U4 sees two reflows (J-STD-020 profile).

## KiCad project

- `hardware/kicad/garantia-knx-direct.kicad_pro`, KiCad 10, with the sheets `01_aux_power`, `02_knx`, `03_mcu` and `04_sensor`. ERC: 0 errors, 0 warnings. The netlist has 69 nets; no net crosses a domain except at PS1's and U4's own pins.
- **Libraries** (`hardware/kicad/lib/`):
  - `knx-rgbic`: the NCN5121MNTWG symbol (used for the NCN5130), the QFN-40 thermal-via footprint, and knx-rgbic's bottom-view WAGO bus-pin footprint (unused).
  - `garantia`: `KEYSTONE_900` (with `3dmodels/Keystone_900.step`); `KNX_BusPins_2x1.0mm_P5.75mm_WAGO_243-211_Top` (J1, J2); `L_APV_AHW2012FE` (L1, from JLC's land pattern, with the stock 0805 inductor 3D model as an approximation); `SW_XKB_TS-1187A_5.1x5.1mm` (unused).
  - `keystone_900`: the SnapEDA symbol.
- **Stackup:** 4 layers, 1.6 mm, 1 oz, dielectrics 0.1 / 1.24 / 0.1 mm.
- **Pours:** 16 zones, clearance 0.25 mm:
  - In1: one ground plane per domain;
  - In2: `+3V3_KNX` under the island, `GND_AUX` and `GND_S` in their domains;
  - F.Cu and B.Cu: ground fills per domain;
  - `SENS_SIG_J4_*` on all four layers (priority 2).
- **Routing:** 499 track segments, 87 vias. kicad-cli DRC with zone refill and schematic parity: 0 unconnected, 0 clearance or isolation errors, 0 parity issues. The only remaining items are the intended J1/J2 courtyard overlap (excluded) and footprint silkscreen past the board edge. No copper of different domains comes closer than 2 mm across layers.
- **Fabrication attributes:** every SMD footprint has the SMD attribute. J1–J4, PS1 and TP1/TP2/TP5/TP7 are DNP and out of the BOM.
- **JLCPCB package** (`fab/jlcpcb-2026-10-17-a/`, with `ORDER-NOTES.md`): Gerbers with both pastes, the PTH drill file and the job file; BOM of 36 LCSC lines and 54 parts (20 top, 34 bottom); a CPL fitted pad by pad against JLC's footprints (L2 +180°; U2, U3, U4 and U5 +270°); edge rails with mouse-bite tabs, kept away from J4's barrel.

## Firmware notes

The firmware in `firmware/` is version 1.2, ETS application 0x4741 version 3. U3 is the KNX device: thelsing/knx's Bau07B0 on the NCN5130, bare metal (PlatformIO, CMSIS registers, no HAL), based on knx-rgbic's U9 firmware (knx-device-playbook `references/firmware.md`) with the local fixes in `firmware/patches/knx-thelsing.patch`. Build, flashing and commissioning steps are in [`firmware/README.md`](firmware/README.md).

**Memory map** (STM32G0B1KC, 256 KB, dual-bank):

| Address | Size | Content |
| --- | --- | --- |
| 0x0800 0000 | 4 KB | Boot manager (1.4 KB used), never rewritten by the application |
| 0x0800 1000 | 124 KB | Application (60.6 KB) |
| 0x0802 0000 | 4 KB | KNX ETS download |
| 0x0802 1000 | 4 KB | Store: two 2 KB pages of calibration and top-up state |

A DFU image covers bank 1 only, so flashing never erases the ETS download or the store.

**NCN5130:**
- USART1 on PB6 (TX) and PB7 (RX), AF0; 19 200 baud, 9-bit.
- **ACR0 = 0x14** (`KNX_NCN_ACR0`): XCLKEN on (XCLK feeds TIM2), DC2 off, V20V off, V20VCLIMIT at its reset value. Datasheet Table 16: reset 0x74; b6 V20VEN, b5 DC2EN, b4 XCLKEN, b3 TRIGEN, b2..0 V20VCLIMIT.
- **XCLK** (datasheet p20, Table 7 notes 18–21) is crystal-derived 8 MHz only in Normal state. In Stand-By after Start-Up, and after a crystal fault, it runs from an inaccurate 4 MHz internal RC.

**Measurement:**
- **TIM2** is a 32-bit counter at 2 MHz in both clock modes: XCLK on ETR (PA15, external clock mode 2) divided by 4 in the ETR prescaler (ETRP must stay ≤ f_TIM/4 = 4 MHz), or HSI16 divided by 8. A 25 Hz period is 80 000 ticks, a 145 Hz period ≈ 13 800, so one period resolves to ≤ 0.01 %; the sensor's own jitter dominates.
- **Capture:** CH2 (PB3) on falling edges of `MCU_PULSE` only, the optocoupler turning on, behind a 16 µs input filter (C17/R7 is 10 µs). The optocoupler turns off 100–200 µs slower than it turns on, so only like edges give clean periods.
- **1-second blocks.** At the end of each block the counter's advance is checked against SysTick (HSI16):
  - A block timed on XCLK counts only if SAVEB stayed high (Normal state) and the XCLK/HSI16 ratio is within **±5 %** of nominal. HSI16 itself is −0.75…+0.5 % at 30 °C and −2…+1.5 % over −40…125 °C (DS13560 Table 44), so a tighter window could reject a good XCLK in a freezing garage, while the fault to catch, XCLK on its 4 MHz RC, is −50 %.
  - A failed block is dropped and the meter falls back to HSI16 with reduced accuracy; on HSI16 it tries XCLK again every 10 s. A capture that spans a clock switch or an overcapture is discarded.
  - Periods outside the valid frequency window are dropped. A block counts with at least 3 valid periods that are a strict majority of what was captured, lost captures included (extra edges from noise split periods). Its period is the median.
- **Settling:** blocks count only after pulses have run for 3 s. That covers power-up and also a cable reconnected after a fault, when C19 has to settle again.
- **Frequency:** the mean of the last *averaging time* block periods (default 10 s).
- **Level:** period-linear by default, or frequency-linear (ETS parameter). At the 145/25 Hz defaults, 43 Hz reads 49.4 %.
- **Sensor fault:** no accepted block for 3 s plus the fault timeout (default 5 s): an open or shorted cable, a dead transducer or missing aux power. The timeout runs on SysTick, so it works without XCLK. The average starts again when pulses return.

**Calibration:**
- f_empty and f_full come from ETS (defaults 145.0 / 25.0 Hz). With "calibration through group objects", Home Assistant can write them (objects 6/7) or teach them (objects 8/9, value 1: the current averaged frequency becomes empty or full).
- One rule everywhere: f_empty ≥ f_full + 1 Hz, both inside the valid window. Invalid ETS values are replaced by the defaults, and top-up stays off until a valid calibration is in use. A teach-in also needs an average timed entirely on XCLK. A refused write sends the value in use back.
- Accepted values are stored in flash, tied to the ETS values they replace, and win until an ETS download changes the ETS f_empty or f_full.
- Objects 6/7 have no callback: thelsing calls callbacks for the device's own sends too, and skips them for a write that arrives while a send is pending. A value in the object that differs from the calibration in use is a bus write.

**Top-up valve** (objects 10–12, through an external switching actuator):
- **Fill session:** starts below the ON level (default 10 %) and ends at the OFF level (default 12 %). The valve's opening time adds up across the whole session, through sensor faults, disables, restarts and ETS downloads, and is limited (default 30 min). The session is stored at its start, its end and every 5 min; a restart that finds it open charges 5 min.
- **Rise check:** while open, the level must rise by x % within y min (default 1 % in 10 min), measured on the unclamped level so a fill starting below 0 % still shows its rise.
- **Lock:** either limit locks top-up and sets object 12. Only object 11 going off → on clears it.
- **Closed:** on a sensor fault, while disabled or locked, without a valid calibration, and while object 10 has no group address. After every start and every close it stays closed for at least 60 s, so the start-up OFF always goes out first.
- **ON repeat:** while open, ON is repeated every 60 s for an actuator channel with a staircase timer (set it to at least twice that, e.g. 5 min). The valve object is sent until the bus confirms it.
- **Persistence:** the enable, the lock and the open session survive power cuts, so a power cut can't re-enable a valve that Home Assistant locked. A new device starts enabled; a device whose store was erased by a flash-ECC error starts disabled until Home Assistant enables it, because the newest enable/lock state may be gone.
- During an ETS download the device can't send anything. Before a download that turns top-up off or moves object 10's group address, disable top-up through object 11.

**Store** (bank 2, 0x0802 1000):
- 32-byte records are appended to one page, CRC-checked; the newest valid record wins. A full page continues on the other page, so the page holding the newest record is never the one being erased.
- Contents: f_empty/f_full from Home Assistant, the top-up enable, the top-up lock and the open fill session.
- Unchanged content isn't rewritten. Safety state goes to flash at once and is retried on failure; calibration at most every 10 s.
- Every bank-2 read is marked, so a flash-ECC error erases only the area (ETS data or store page) being read, whatever FLASH_ECC2R's address field means.

**SW1 and D5:**
- Short press (< 3 s): toggles programming mode (D5 on).
- Held 3–10 s, then released (D5 blinks): USB DFU without a bus power cycle. The valve gets OFF first.
- Held 10 s: factory reset. It sends valve OFF, erases the ETS download and the store, and blinks D5 fast until SW1 is released; after 30 s it resets anyway, so holding for over 40 s ends in DFU.
- Held while bus power comes on: USB DFU through the boot manager.

**Boot manager** (`firmware/u3/src/boot/boot.c`, v2), at every reset:
1. Interrupts off and VTOR on itself (after DFU "leave" the ROM's vector table and NVIC state may still be live).
2. Clears the EMPTY flag and re-enables SWD (the ROM bootloader's Go command clears DBG_SWEN).
3. Resets once more if the ROM bootloader's clocks or USB are still set up, so the application always starts from a clean reset.
4. Samples SW1 for 50 ms and checks the application's descriptor, size, vectors and CRC-32.
5. SW1 held, a request from the application, or no valid application: blinks D5 three times, leaves it on, and jumps to the ROM bootloader (USB DFU) with interrupts re-enabled. Otherwise starts the application.

The application also resets through the boot manager if it finds the ROM's clocks running (DFU "leave" straight into 0x0800 1000).

**Log:** TP7 (PA2, USART2 TX, 115200 8N1). At start: version, reset cause, KNX serial and calibration. Every 10 s: frequency, level, clock source and top-up state. Plus events.

**KNX identity:** manufacturer 0x00FA (the OpenKNX community ID for DIY devices; not KNX-certified), application **0x4741** ("GA") **version 3**, order number "GARANTIA-3", hardware serial 4741, product "Garantia tank level", BusCurrent 10 mA. Twelve objects; a 28-byte parameter block with 16-bit values big-endian. The firmware checks manufacturer, application and version, not the object count, because ETS may write a shorter table when the last objects are hidden.

## KNX application

`firmware/knxprod/spec.py` is the single source for objects and parameters: `gen.py` writes the firmware header and `gen_xml.py` the ETS XML, which OpenKNXproducer signs into the `.knxprod`.

| No. | Object | DPT | Flags | Sent |
| --- | --- | --- | --- | --- |
| 1 | Water level | 8.010 (signed 16-bit, 0.01 %) | CRT | on a change ≥ x % (default 1) and every n min (default 30) |
| 2 | Sensor frequency | 14.033 (4-byte float, Hz) | CRT | on a change ≥ x (default 1.0 Hz); cyclic optional |
| 3 | Water volume (l) | 12.1200 | CRT | with the level; only if a tank volume is set |
| 4 | Sensor fault | 1.005 | CRT | on change |
| 5 | Low-level alarm | 1.005 | CRT | on change (2 % hysteresis; off by default) |
| 6, 7 | Calibration: frequency empty / full | 14.033 | CRWT | after a teach-in, a refused write, the start-up and a bus reconnect |
| 8, 9 | Calibration: tank is empty / full now | 1.017 | CW | trigger, value 1 |
| 10 | Top-up valve | 1.001 | CRT | until the bus confirms it; ON repeated while open |
| 11 | Top-up enable | 1.003 | CRW | written by Home Assistant; off, then on, clears a lock |
| 12 | Top-up fault | 1.005 | CRT | on change |

- Every status goes out again after the start-up phase and after a bus reconnect.
- Objects 6–9 appear only with "calibration through group objects" on; objects 10–12 only with top-up control on.
- **No object has the U flag.** With U, any read answer on those group addresses would act as a write: a recalibration, or a cleared lock. T on 6/7 lets a teach-in or a refused write reach Home Assistant.
- **Level as DPT 8.010:** KNX has no general float percentage. DPT 9.007 is defined as humidity (Home Assistant makes it a humidity sensor) and its steps are coarser than 0.01 % above 41 %; DPT 14 has no percentage subtype, so Home Assistant would show no unit. In Home Assistant the level is `type: percentV16`.

**Parameters** (frequencies in 0.1 Hz, 16-bit):
- General: start-up delay (0 s); averaging time (10 s); fault timeout (5 s); valid frequency window (15.0–180.0 Hz).
- Level: level model (period-linear / frequency-linear); f_empty (145.0 Hz) and f_full (25.0 Hz); calibration through group objects (yes); send on level change (1 %) and cyclic (30 min); send on frequency change (1.0 Hz) and cyclic (off); low-level alarm threshold (off); tank volume (0 = don't send the volume).
- Top-up valve: on/off (off); ON below 10 %, OFF at or above 12 %; maximum opening time per fill (30 min); required rise (1 % within 10 min); ON repeat (60 s).

## Programming

- **USB DFU from the ROM bootloader** (AN2606 §50, Table 111): USB FS on PA11 (D−) and PA12 (D+), clocked from HSI48 with CRS, so no crystal and no external pull-up. VDDUSB is tied to VDD internally on the KCU6.
- **Power:** USB doesn't power the board; the 3.3 V comes from the bus through the NCN5130's DC1. Flash on the KNX bus, or with a 24–30 V DC supply on the bus terminals.
- **Ground:** J3's GND is `GND_KNX`. On the house line use a laptop on battery or a USB isolator, never an earthed PC.
- **Entering DFU:**
  - A blank chip starts the ROM bootloader by itself (EMPTY flag).
  - The factory option bytes ignore the BOOT0 pin (RM0444 Table 17: nBOOT_SEL = 1), and the option bytes are never written. So on a programmed chip the boot manager is the way in: SW1 held at power-up, or SW1 held 3–10 s and released while running.
- **Images:** `garantia-knx-u3-app.hex` (0x0800 1000) for every update; it never touches the boot manager, so an interrupted update stays recoverable. `garantia-knx-u3-full.hex` (0x0800 0000) only for a blank chip: a power cut while it writes page 0 needs an SWD probe (TP1, TP2, TP5) to recover.
- **ROM bootloader V9.2 quirks:** it can't erase several bank-2 pages in one command, so never mass-erase or erase the ETS area with the flasher; its Go command disables SWD (the boot manager re-enables it). Never pulse NRST on a blank chip.
- **Tools:** dfu-util, STM32CubeProgrammer, or the portable flash kit in `firmware/flashkit/`.

## Test points

Only four test pads exist (TP1 SWDIO, TP2 SWCLK, TP5 `GND_KNX`, TP7 log TX). Other measurements use part pads:
- XCLK: R17's `KNX_XCLK` pad, against a counter (8 MHz, ≤ 50 ppm);
- the shunt: R8/R9;
- 3.3 V: C9 or C15;
- bus current: a meter in series with the bus.

## Open items

- **Mid-point check:** measure the frequency with the tank about half full. Period-linear predicts ≈ 43 Hz; ≈ 85 Hz would mean frequency-linear, which the level-model parameter can select.
- **Schematic cosmetics:** overlapping power-symbol and label text on Sheets 2 and 4 (no electrical effect).
