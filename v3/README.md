# V3: KNX device

The overview, how it works, ordering, flashing and the Home Assistant configuration are in the main [README, section V3](../README.md#v3--knx-device).

| Folder / file | What |
| --- | --- |
| [`design.md`](design.md) | The design document: sensor facts, architecture and why the sensor domain floats, every sheet with values, pins and LCSC numbers, the floorplan, layout rules, firmware design, the KNX application |
| [`hardware/kicad/`](hardware/kicad/) | KiCad 10 project, the source for the schematic and PCB: 4 sheets, 4-layer PCB, design rules (`.kicad_dru`, including the 2 mm isolation rules), project libraries |
| [`fab/jlcpcb-2026-10-17-a/`](fab/jlcpcb-2026-10-17-a/) | JLCPCB package: Gerbers, BOM, CPL (fitted pad by pad against JLC's footprints), `ORDER-NOTES.md` |
| [`firmware/`](firmware/) | Firmware source (STM32G0B1, thelsing/knx), ETS product generator, flash kit; [`firmware/README.md`](firmware/README.md) |
| [`release/firmware-1.2/`](release/firmware-1.2/) | Built firmware 1.2 images and the signed `.knxprod` (the level in 0.01 % steps) |
