# V3 firmware 1.2 and ETS product file

The firmware for U3, the STM32G0B1KCU6 on the bus-powered KNX side of the V3 board, and the ETS product file. U3 is the KNX device: it runs [thelsing/knx](https://github.com/thelsing/knx) on the NCN5130 transceiver and measures the Garantia transducer's frequency through the LTV-817S optocoupler. On KNX it reports level, frequency, volume and faults, takes its calibration from Home Assistant, and can drive a top-up valve through a separate switching actuator.

Ready-built images and the signed `.knxprod` (application 0x4741 version 3) are in [`../release/firmware-1.2/`](../release/firmware-1.2/). The design and the KNX application are in [`../design.md`](../design.md) ("Firmware notes", "KNX application").

## Layout

| Path | What |
| --- | --- |
| `u3/` | PlatformIO project: `u3_boot` (boot manager, 4 KB at 0x0800 0000) and `u3_app` (KNX application at 0x0800 1000) |
| `u3/src/shared/u3_shared.h` | Memory map (boot, application, ETS data, two store pages), image descriptors, RAM handover, CRC-32 |
| `u3/src/boot/boot.c` | Boot manager: USB DFU when SW1 is held at reset, on request or without a valid application; otherwise checks the application's CRC and starts it |
| `u3/src/app/hw.c` | Registers: clock, USART1 (NCN5130), USART2 TX (log), GPIO, flash, IWDG, the flash-ECC NMI |
| `u3/src/app/platform_g0.cpp` | thelsing Platform (ETS data as a 4 KB RAM image, Eeprom type) |
| `u3/src/app/meter.c` | TIM2: period capture on PB3, counted on the NCN5130's crystal clock (XCLK on PA15) or HSI16, 1-second blocks with a clock check |
| `u3/src/app/store.c` | Two flash pages for the calibration and top-up state that change at run time |
| `u3/src/app/app.cpp` | Application: measurement, level, objects, calibration, top-up valve, SW1/D5, log |
| `u3/src/app/knxprod.h` | Generated from `knxprod/spec.py`; don't edit |
| `u3/tools/` | Post-build: patches the descriptor (size, CRC-32) and writes the DFU images in `u3/build/` |
| `knxprod/spec.py` | **Single source** for parameters and objects; `gen.py` writes `knxprod.h`, `gen_xml.py` the ETS XML |
| `patches/knx-thelsing.patch` | Local fixes to thelsing/knx (TP-UART receive path in the interrupt, one group request in flight, unlinked objects not sent, and more; see "Local fixes") |
| `get-knx-thelsing.sh` | Fetches thelsing/knx at `980c047` into `knx-thelsing/` and applies the patch |
| `flashkit/` | Portable flash kit for a laptop (Windows, macOS): `kit/` (scripts, README), `make_flashkit.py`; `fetch_vendor.sh` downloads dfu-util 0.11 and Zadig 2.9 (`vendor/VENDOR.md`) |

## Build

```sh
sh get-knx-thelsing.sh                        # once: thelsing/knx at 980c047 + patches/knx-thelsing.patch
cd u3 && uvx --from platformio pio run        # both images; writes the DFU images in u3/build/
cd knxprod && OKP=/path/to/OpenKNXproducer-x64.exe sh build.sh   # knxprod.h, the ETS XML, the signed .knxprod
sh flashkit/fetch_vendor.sh && python flashkit/make_flashkit.py  # optional: the portable flash kit, in flashkit/dist/
```

- **PlatformIO 6.2.** `uvx --from platformio pio` runs it without an install. Platform `ststm32`, framework `cmsis`: registers only, no HAL.
- **OpenKNXproducer** ([OpenKNX/OpenKNXproducer](https://github.com/OpenKNX/OpenKNXproducer), 4.3.12 was used) runs on Windows and signs with the local ETS6 installation.
- **After changing `spec.py`**, run `build.sh` **and** rebuild the firmware, because `knxprod.h` changes.
- **Version numbers:**
  - Bump `APP_VERSION` in `spec.py` on any change to the parameter or object layout. The firmware ignores an ETS download of another version.
  - Bump `u3/src/app/version.h` on every firmware release.

Sizes for 1.2: the application is 60.6 KB (48 % of bank 1), the boot manager 1.4 KB.

## Flashing (USB DFU)

USB doesn't power the board. The 3.3 V comes from the bus through the NCN5130, so flash on the KNX bus or with a 24–30 V DC supply on the bus terminals.

**J3's GND is KNX−.** Use a laptop on battery or a USB isolator, never an earthed PC on a real KNX line.

| Image | At | Use |
| --- | --- | --- |
| `garantia-knx-u3-app.hex` (or `.bin`) | 0x0800 1000 | **Every update.** It never touches the boot manager, so an interrupted update stays recoverable: the boot manager rejects the broken application and starts DFU. The `.hex` carries its address |
| `garantia-knx-u3-full.hex` (or `.bin`) | 0x0800 0000 | **A blank chip only** (or a new boot manager). It rewrites page 0. A power cut while page 0 is written leaves a chip that only an SWD probe can recover, so flash it on a stable supply |

1. Solder a USB-C breakout to J3 (D−, D+, GND; its VCC pin stays unconnected), with the socket pointing away from the RCA jack J4.
2. Enter DFU, one of:
   - **Blank chip:** it starts the ROM bootloader by itself.
   - **Running device:** hold SW1 for 3–10 s (D5 blinks), then release. D5 blinks fast for 1 s, and the valve gets OFF first if top-up is in use.
   - **Any state:** hold SW1 while bus power comes on.

   The boot manager blinks D5 three times and leaves it on. The ROM bootloader may switch it off again, so the reliable sign is the PC seeing "STM32 BOOTLOADER" (DFU in FS mode).
3. Flash. The flash kit's scripts do this with checks; by hand:
   - **dfu-util:**
     - update: `dfu-util -d 0483:df11 -a 0 -s 0x08001000:leave -D garantia-knx-u3-app.bin`;
     - blank chip: `dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D garantia-knx-u3-full.bin`.

     On Windows it needs the WinUSB driver for the bootloader device, set up once with [Zadig](https://zadig.akeo.ie).
   - **STM32CubeProgrammer:** connect over USB, open the `.hex`, tick "Run after programming", Download.
4. After "leave", the application resets once through the boot manager if the ROM's clocks are still running, and the log says "(after DFU)". If nothing starts, power-cycle the bus.

Don'ts:
- **No mass erase or full-chip erase.** The images cover flash bank 1 only, so the ETS download and the store in bank 2 survive an update. The V9.2 ROM bootloader also can't erase several bank-2 pages in one command.
- **Never write the option bytes.**
- **Never pulse NRST on a blank chip.**

SWD fallback, if a broken boot manager ever blocks DFU: TP1 (SWDIO), TP2 (SWCLK), TP5 (GND), connecting without reset.

## ETS commissioning

1. Import `garantia-knx.knxprod` into the ETS6 catalog: manufacturer 0x00FA (the OpenKNX community ID for DIY devices), "Garantia tank level", application 0x4741 version 3.
2. **Individual address:** a short press on SW1 (under 3 s) toggles programming mode (D5 on). Then program the address in ETS. The KNX serial (00FA:xxxxxxxx) is printed in the log at every start, so programming by serial number also works.
3. **Download the application.** Until the first download the device has no objects, but it measures and logs anyway. The log line after a start says "application OK" and shows how many objects ETS wrote.

**Factory reset:** hold SW1 for 10 s. It passes the 3–10 s DFU window, where D5 blinks; keep holding. It sends valve OFF, erases the ETS download and the store, and blinks D5 fast until SW1 is released. U3 then restarts unconfigured. After 30 s it resets anyway, so holding for over 40 s ends in DFU.

**Before an ETS download that turns top-up off or moves object 10's group address**, disable top-up through object 11 first. During a download the device can't send anything; only the actuator's staircase timer would close a valve that was open.

## What it does

**Measurement:**
- TIM2 counts the NCN5130's XCLK (8 MHz from its crystal) divided by 4, or HSI16 divided by 8: 2 MHz either way.
- It captures falling edges of `MCU_PULSE` (the optocoupler turning on), behind a 16 µs input filter.
- The periods are collected in 1-second blocks:
  - A block timed on XCLK counts only if the NCN5130 stayed in Normal state (SAVEB high) and XCLK/HSI16 was within ±5 % of nominal. Otherwise it is dropped, and the meter falls back to HSI16. On HSI16 it tries XCLK again every 10 s.
  - Periods outside the valid frequency window (ETS, 15–180 Hz by default) are dropped. A block counts with at least 3 valid periods that are a strict majority of what was captured; its median is its period.
- Blocks count only after pulses have run for 3 s, so the comparator's averaging threshold (C19) has settled.
- The frequency is the average of the last *averaging time* block periods (default 10 s).
- **Level:** period-linear by default, `(1/f − 1/f_empty) / (1/f_full − 1/f_empty)`, or frequency-linear. At the 145/25 Hz defaults, 43 Hz reads 49.4 %.
- **Sensor fault:** no accepted block for 3 s plus the fault timeout (default 5 s), for example an open or shorted cable or a dead transducer.

**Group objects:**

| No. | Object | DPT | Flags | Sent |
| --- | --- | --- | --- | --- |
| 1 | Water level | 8.010 (0.01 %) | CRT | on a change ≥ x % (default 1) and every n min (default 30) |
| 2 | Sensor frequency | 14.033 | CRT | on a change ≥ x (default 1.0 Hz); cyclic optional |
| 3 | Water volume (l) | 12.1200 | CRT | on a change of the level threshold's share of the volume, and the level's cycle |
| 4 | Sensor fault | 1.005 | CRT | on change |
| 5 | Low-level alarm | 1.005 | CRT | on change (2 % hysteresis; off by default) |
| 6, 7 | Calibration: frequency empty / full | 14.033 | CRWT | after a teach-in, a refused write, the start-up and a bus reconnect |
| 8, 9 | Calibration: tank is empty / full now | 1.017 | CW | (trigger, value 1) |
| 10 | Top-up valve | 1.001 | CRT | until the bus confirms it; ON repeated while open |
| 11 | Top-up enable | 1.003 | CRW | (Home Assistant writes it; off, then on, clears a lock) |
| 12 | Top-up fault | 1.005 | CRT | on change |

- Every status goes out again after the start-up phase and after a bus reconnect.
- Objects 6–9 appear only with "calibration through group objects" set to Yes; objects 10–12 only with top-up control on.
- No object has the U flag: read answers never act as writes.

**Calibration:**
- f_empty must be at least 1 Hz above f_full, and both must lie inside the valid window.
- ETS values that break this are replaced by the defaults (145/25 Hz), and top-up stays off until a valid calibration arrives.
- Written values (6/7) and teach-ins (8/9 with value 1) are checked the same way. A teach-in also needs a valid average timed entirely on XCLK. A refused write sends the value in use back.
- Accepted values are stored in flash and win until an ETS download changes the ETS f_empty or f_full.

**Top-up valve:**
- **A fill session** starts below the ON level (default 10 %) and ends at the OFF level (12 %). The valve's opening time adds up across the whole session, through sensor faults, disables, restarts and ETS downloads, and the total is limited (default 30 min).
- **While open**, the level must rise by x % within y min (default 1 % in 10 min).
- **Either limit locks top-up** and sets object 12. Object 11 off and then on clears the lock.
- **The valve is closed:** on a sensor fault, while disabled or locked, without a valid calibration, or while object 10 has no group address.
- **After every start and every close** it stays closed for at least 60 s.
- **While open**, ON is repeated every 60 s for an actuator channel with a staircase timer (set the timer to at least twice that, e.g. 5 min). The valve then closes by itself if this device or the bus fails.
- **The enable, the lock and the session survive power cuts.** If a flash error ever erased the store, top-up starts disabled until it is enabled again.

**Log** on TP7 (PA2, 115200 8N1, TP5 as ground; isolated USB-UART only): the version, reset cause, KNX serial and calibration at start; the frequency, level, clock source and top-up state every 10 s; and events.

## Local fixes to thelsing/knx

`patches/knx-thelsing.patch` is a `git diff` against upstream `980c047`. The changes come from the author's knx-rgbic project, where they were reviewed and run on hardware. Each is marked `knx-rgbic` in the source:
- **TP-UART receive path in the USART interrupt** (`USE_TP_RX_QUEUE`). Group telegrams need their ACK decided while the frame is still arriving. Transmit confirmations complete in `loop()`, outside the interrupt.
- **One group request in flight at a time.** The application layer has one confirmation slot per request type.
- **Objects without a group address aren't sent.** Upstream sends them to 0/0/0.
- **A frame containing a byte with a UART parity error is dropped.** That is the NCN5130's error flag in 9-bit mode.
- **An NCN reset outside `reset()` re-runs the initialisation**, including the optional ACR0 write (`KNX_NCN_ACR0`).
- **A free-list fix in `memory.cpp`**, and byte-order macros for bare metal (`KNX_BARE_METAL`).

## Licences

- **Source code:** the code written for this project in `u3/`, `knxprod/` and `flashkit/` is under the repository's MIT licence.
- **thelsing/knx** is GPL-3.0, and so is `patches/knx-thelsing.patch`.
- **Built firmware:** the images in `../release/` link thelsing/knx, so they are distributed under GPL-3.0. Their complete source is this folder, plus thelsing/knx at `980c047` with the patch applied (`get-knx-thelsing.sh`).
- **The flash kit's third-party tools** (dfu-util GPL-2.0+, libusb LGPL-2.1, Zadig GPL-3.0) aren't stored here; `fetch_vendor.sh` downloads them from their official releases.
- **Not KNX-certified:** only the NCN5130 is KNX-certified. A DIY device on manufacturer ID 0x00FA isn't a certified KNX product.
