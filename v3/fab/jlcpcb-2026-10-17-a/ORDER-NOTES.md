# JLCPCB order: garantia-knx-direct (package a, 2026-10-17)

Built from:
- `garantia-knx-direct/garantia-knx-direct.kicad_pcb`, md5 `e6e61592…`, saved 2026-10-17 14:57:16. That is routing v2 (499 tracks, 87 vias) with the v2 pours, after the post-layout review's fix round, plus your bottom-text edits.
- The schematic in the same folder, ERC 0.
- The Gerbers come from a copy of that board refilled by kicad-cli. That copy's DRC (zones refilled, schematic parity) gives 0 unconnected, 0 parity issues and 0 clearance or isolation errors. The cross-layer isolation check gives 0 places under 2 mm.

## Upload

| Order step | File | md5 |
| --- | --- | --- |
| PCB (Gerber + drill) | `garantia-knx-direct-gerbers.zip`: 11 Gerber layers (4 copper, both masks, both silkscreens, **both pastes**, Edge.Cuts), the PTH drill file, the job file | `25f88d7e…` |
| Assembly BOM | `BOM-garantia-knx-direct.csv`: 36 lines, 54 parts, every one with an LCSC number | `4ef8c8b3…` |
| Assembly CPL | `CPL-garantia-knx-direct.csv`: 54 placements, **20 top and 34 bottom** | `aa12ed26…` |

The board has no non-plated holes, so there is no NPTH file. Don't upload the loose `gerbers/` folder or `drill-maps/`: the folder holds the zip's content, and the maps are for your own reference.

## PCB options

- **Layers and thickness:** 4 layers, 1.6 mm, FR-4, 28 × 46 mm. No impedance control.
  - Board Setup has 0.1 mm prepreg / 1.24 mm core. "Specify Stackup: No" is fine. The 2 mm domain gaps are aligned on all four layers, so a thicker prepreg only adds margin.
- **Copper:**
  - outer **1 oz** (the form default);
  - inner **0.5 oz** (the form default) is enough, because the planes carry under 0.2 A. Board Setup's 35 µm only feeds KiCad's thickness sum.
- **Min via hole size/diameter:** the default **0.3 mm (0.4/0.45 mm)**. Every plated hole is 0.3 mm or larger:
  - 87 vias at 0.6/0.3 mm;
  - U2's 9 exposed-pad holes at 0.3 mm;
  - THT holes: J3 and PS1 1.0 mm, J1/J2 1.2 mm, J4 1.57 mm and 3 × 2.67 mm (the shell legs).
- **Tracks and gaps:** 0.2 mm minimum track, 0.2 mm clearance. The only exception is 0.15 mm clearance at the two QFN fan-outs (U2, U3). Both are inside JLC's standard capabilities.
- **Surface finish:** **ENIG**, recommended for the 0.5 mm-pitch QFNs (U2, U3) and their exposed pads.
- **Via covering:** **Plugged** (tented, with ink in the hole), the free option.
  - JLC doesn't fill holes inside a pad's mask opening. So U2's 9 exposed-pad holes and U3's two stay open.
  - U3's first exposed-pad hole sits on a paste-gap intersection; the second touches two paste-window edges by 0.015–0.065 mm (layout review A3, accepted).
- **Silkscreen:** all text is 1.0 mm high with a 0.16 mm stroke. Reference designators are hidden; the labels are on the bottom.
- **Mark on PCB:** the board has no "JLCJLCJLCJLC" placeholder. Pick "Remove Mark", or let JLC place it.
- **Quantity:** order 5 and assemble 2. The number assembled depends on how many U3 you can get.

## Assembly options

- **PCBA type: Standard, both sides.** Economic PCBA assembles one side only.
  - Top, 20 parts: U3 with C14–C16, R3, R4, R6, SW1 and D5; the tall C7 (Ø8 × 10.2 mm) and L2 (5 × 5 × 4 mm); and the sensor passives R8–R12, R15, R16, C18 and C19.
  - Bottom, 34 parts: U2 and the KNX front end, the aux filter, U4, U5 and the rest. The heaviest are the SMA diodes, R1 (2512), C1/C2 (1210) and U5 (SOIC-8).
  - J1–J4, PS1 and TP1/TP2/TP5/TP7 are DNP and appear in neither the BOM nor the CPL.
- **Edge rails ("Added by JLCPCB"), needed for three reasons:**
  - Standard PCBA needs a panel of at least about 70 × 70 mm, and this board is 28 × 46 mm.
  - The board has no fiducials or tooling holes.
  - Parts sit 0–1 mm from all four edges:
    - D2, D6, F2, C3, C19, R10 and R11 on the right;
    - C7, C12, C13, C6, R4 and U3 on the left;
    - D1, F1 and D3 on the top;
    - C19 and C20 on the bottom.
- **The RCA jack:** J4 (hand-fitted later) sits on the bottom edge, in the 15 mm next to the left corner as seen from the top. Its barrel overhangs that edge by about 2 mm. A mouse-bite nub there could catch the mating plug's sleeve.
- **U4's reflow:** the LTV-817S datasheet recommends one reflow ("One time soldering reflow is recommended", Lite-On, temperature profile page). U4 is on the bottom. C7 and L2, the heavy parts, are on top, so JLC will probably reflow the top last, and U4 will see two reflows.
  - Recommended: accept that, as written in the remark below (J-STD-020 profile; a recommendation, not a limit).
  - If you'd rather not, delete the remark's last-but-one sentence, and JLC will answer the question instead.
- **Depanelling:** Advanced Options → "Depanel boards & edge rail before delivery": **Yes**.
- **How to ask:** Advanced Options → "Assembly remark": Yes, and paste:

  > Edge rails: please attach rails with mouse-bite tabs, not V-cut, and depanel before delivery: parts sit 0-1 mm from all four edges. The bottom edge is the 28 mm edge opposite the two WAGO plug outlines. On that edge, please put NO tab and NO rail contact in the 16 mm next to the corner where the RCA jack's silkscreen outline runs off the board: the jack barrel will overhang the edge there. The optocoupler U4 (LTV-817S, bottom side) is recommended for one reflow: if possible, please reflow the bottom side last; if the top side must go last because of C7 and L2, please proceed. If this is not possible, please contact me before production.

  JLC may answer with a panel drawing. Check it for a tab under J4.
- **Confirm Parts Placement: Yes.** This is the board's first order, with new footprints and 34 bottom-side parts. **Confirm Production File: Yes.**
- **Parts to fit by hand** (see `design.md` Sheets 1–4):
  - J1: 2 × WAGO 243-131 pins (LCSC C20252226), for a WAGO 243-212 yellow/white plug (not at LCSC; Bürklin or Farnell).
  - J2: 2 × WAGO 243-131 pins (C20252226), for a WAGO 243-211 red/grey plug (C19633675).
  - J4: Keystone 900 RCA jack (your stock). Its shell legs sit in solid copper on four layers, so use a hot iron (≥ 60–80 W).
  - PS1: Traco TMR 2412 (or TMR 3-2412).
  - J3: the USB-C breakout, fitted once for flashing. Fit it with its receptacle pointing away from J4 and its metal ≥ 2 mm from J4's body, or take it off after the first flash.
  - Before the first plug-in, follow the hand-soldering and ohmmeter notes in `design.md` "PCB layout rules".

## Checks in JLC's portal before you pay

1. **DFM/Gerber check:** it should read 4 layers, 28 × 46 mm (28.05 × 46.05 with the outline stroke), PTH only, no slots. Possible notes:
   - the 0.15 mm clearances at U2's and U3's fan-outs;
   - the open holes in U2's and U3's exposed pads;
   - footprint silkscreen past the board edge (J1, J2, J4), which JLC clips.
2. **BOM matching:** confirm all 36 lines. JLC assembly stock on 2026-10-17:

   | Part | LCSC | Stock | Note |
   | --- | --- | --- | --- |
   | U3 STM32G0B1KCU6 | C5270237 | **0** (LCSC 0) | **Must be sourced; options below** |
   | C7 EEH-ZA1V151P | C341199 | 728 | |
   | U2 NCN5130MNTWG | C513000 | 1,879 | |
   | L2 SWPA5040S221MT | C370497 | 5,188 | |
   | R1 RC2512FK-0727RL | C877905 | 11,773 | |
   | everything else | | ≥ 17,000 | |

   **20 lines are extended parts**, so expect about $57–60 in setup fees. C19077535 (D6/D7) is "preferred extended".

   **U3, the options:**
   - **a) Recommended:** in the BOM step, use JLC's **Global Sourcing** (or pre-order into your parts inventory) for STM32G0B1KCU6. JLC buys it from an outside distributor. Expect a few days' delay and a sourcing fee. Check DigiKey or Mouser stock first.
   - b) The STM32G0B1KCU7 (C5270235) is a pin-for-pin drop-in and allowed by the hard rules, but only 1 was in stock. That is enough for one board: change the BOM line's part number to C5270235 for that board.
   - c) Leave U3 off the JLC order (delete its BOM and CPL lines) and fit it by hand. That means a 0.5 mm-pitch UFQFPN-32 with an exposed pad, so hot air and paste. Not recommended.
   - d) Wait for JLC or LCSC to restock.
3. **Component Placements preview, both sides. This is mandatory for this order.**
   - **Why:** JLC doesn't publish its bottom-side rotation convention. The CPL uses the one the kicad-jlcpcb-tools plugin uses: the bottom seen from below, rotation = 180° − KiCad's rotation, plus the same footprint correction as on top. Fitting knx-rgbic's verified U5 line with that model reproduced it exactly, but no bottom-side order has been confirmed in JLC's preview yet. Every line here was matched pad by pad against JLC's own footprints (see "Placement corrections").
   - **How to check:** each model's leads should sit on the gold pads, and each pin-1 dot or polarity mark should be where the table says. Corners are named after the board edges, so they hold in both views:
     - the **plug** edge (top, J1/J2);
     - the **RCA** edge (bottom, J4);
     - the **J3** edge (left, the USB pads);
     - the **PS1** edge (right).

   | Part | Side | CPL rotation | Pin 1 / polarity mark should be |
   | --- | --- | --- | --- |
   | U2 NCN5130 (QFN-40) | bottom | 180 | corner toward the plug edge and the J3 edge |
   | U3 STM32G0B1 (QFN-32) | top | 180 | corner toward the plug edge and the PS1 edge |
   | U4 LTV-817S | bottom | 270 | pin 1 (LED anode) in the row toward the PS1 edge, at its plug-edge end. The LED pins (1, 2) face PS1, the transistor pins (3, 4) face J3 |
   | U5 LM2903 (SOIC-8) | bottom | 180 | corner toward the plug edge and the J3 edge |
   | Y1 crystal | bottom | 270 | pad 1 at the corner toward the plug edge and the PS1 edge |
   | D1, D4 SS16 | bottom | 0 | cathode band toward the PS1 edge |
   | D2 SMAJ33A, D6, D7 SMAJ15A | bottom | 270 | cathode band toward the plug edge |
   | D5 red LED | top | 90 | cathode toward the RCA edge |
   | C7 150 µF | top | 180 | "+" (the chamfered end) toward the PS1 edge, the "−" stripe toward the J3 edge |

   - D3 (SMAJ40CA) is bidirectional. L2 and SW1 are unpolarised.
   - Some 3D models are rotated within JLC's own footprint. Judge by the pads and pin-1 marks, not the model's look.
   - **Land differences:** these JLC lands differ from ours symmetrically, centred on the part, so the parts land right even though the gold pads look offset:
     - U4: pad rows 10.0 mm against our 9.53 mm;
     - D6/D7: SMA pad centres 0.4 mm further out;
     - D1/D2/D4: 0.2 mm further out.
   - If a part looks wrong, fix it in the preview and tell me which one, so the corrections file gets the same change.

## Known DRC items

The DRC on the board copy (zones refilled, schematic parity) lists 6 items. All are known:
- **Courtyard overlap J1/J2 (1):** the dovetailed WAGO pair is intended. J1's tongue sits in J2's groove, and the overlap is excluded in the project.
- **Silkscreen (5), cosmetic:**
  - J1's and J2's plug outlines and J4's barrel outline run past the edge (4); JLC clips them;
  - the J1 and J2 plug outlines overlap each other (1).

## Placement corrections

JLC places each part with its own EasyEDA footprint. Those footprints don't share KiCad's zero rotation or origin, so the CPL is written from a pad-by-pad fit rather than from KiCad's position file:
- **Footprints:** JLC's footprint for each of the 36 BOM parts, from lceda.cn. 23 came from knx-rgbic-doublesided's cache; 13 were fetched 2026-10-17 (C86052, C15850, C1523, C19077535, C883116, C17888, C6364733, C421609, C26083, C25900, C25105, C109227, C57474).
- **Fit:** each footprint is placed at 0/90/180/270 (mirrored for the bottom) against our pads and matched by pad number. Mid X/Y is where JLC's footprint centre must go.
- **Polarity:** in JLC's symbols pad 1 is the cathode for every diode (C2481, C113966, C19077535), as in KiCad's D_SMA. The red LED C2286 numbers its anode pad 1, so its pads are swapped in the fit.
- **Results** (`cpl-corrections.json`): 5 parts differ from KiCad's own rotation. No part needs an offset.
  - L2 +180° (unpolarised, pads numbered the other way);
  - U2, U3 and U5 +270°, the same as on knx-rgbic;
  - U4 +270°.
- **Independent check** (`verify_cpl.py`): placing JLC's pads at every CPL line lands them within 0.25 mm of ours on all 54 lines, except the symmetric land differences above (D6/D7 0.40 mm, U4 0.24 mm).
- **Tooling:** `scratch/fab/`:
  - `make_bom.py`: kicad-cli's BOM merged by LCSC number. C3 "10uF 25V" and C19 "10uF" are the same part, C15850.
  - `jlcstock.py`: JLC stock and tier.
  - `make_corr.py`, plus the playbook's `cplmatch.py`, `write_cpl.py` and `verify_cpl.py`.

## Status

All files are generated, non-empty and checked:
- **Gerbers and drill:**
  - 4 copper layers at 35 µm in the job file;
  - F.Paste 79 and B.Paste 120 apertures, on the 54 assembled parts only: no paste on the test pads, the THT pins or any DNP part;
  - PTH: 87 vias and U2's 9 exposed-pad holes at 0.3 mm, J3/PS1 10 × 1.0 mm, J1/J2 4 × 1.2 mm, J4 1.57 mm and 3 × 2.67 mm;
  - no NPTH.
  - The outer layers were rendered from the zip's Gerbers and match the board.
- **BOM:** 36 lines, one per LCSC part, 54 placements.
- **CPL:** the same 54 designators, both sides, no DNP part.
