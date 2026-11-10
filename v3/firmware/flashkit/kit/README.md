# garantia-knx flash kit

Flashes U3, the STM32 on the garantia-knx-direct board, from a laptop over USB while the board is on the normal KNX bus. Windows and macOS (Apple Silicon or Intel). Everything needed is in this folder, apart from one Homebrew install on the Mac. `MANIFEST.txt` says which firmware it carries.

| Script (Windows `.cmd` in `windows\`, macOS `.command` in `macos/`) | When |
| --- | --- |
| `flash-update` | **Normal case:** a board that already runs garantia firmware. Writes the application only; the boot manager, the ETS configuration and the calibration stay |
| `flash-blank-chip` | **Only a brand-new board** (empty STM32). Writes the boot manager and the application |
| `list-devices` | Diagnosis: shows what dfu-util sees |
| `install-driver-zadig` (Windows only) | One-time USB driver setup |

## Before you go: one-time setup at home

**Windows:** nothing to install now. The driver gets installed the first time the board is in DFU mode (step 4 below).

**macOS** (needs internet once):
1. `brew install dfu-util` in Terminal. Homebrew's build is native on Apple Silicon. Without Homebrew, the kit falls back to its own Intel copy, which needs Rosetta 2: `softwareupdate --install-rosetta --agree-to-license`.
2. If the kit came as a download (not on a USB stick), clear the quarantine once, or macOS refuses to run the scripts: `xattr -dr com.apple.quarantine /path/to/garantia-knx-flashkit-…`. Alternatively, right-click the `.command` file → Open → Open.

## Safety: the laptop must float

**J3's GND is KNX−.** The laptop's USB ground connects straight to the KNX line, so:
- **Run the laptop on battery: unplug its charger.** A charger with an earth pin would earth the KNX line, which the KNX installation rules forbid. A USB isolator between laptop and board is the alternative.
- Nothing else on the laptop: no monitor, no mains-powered USB hub, no Ethernet cable.
- Don't add any other supply to the board. It runs from the bus.

## In the garage

1. **USB-C breakout on J3** (D−, D+, GND; its VCC pin stays unconnected), with the socket pointing away from the RCA jack J4. Keep the breakout's metal at least 2 mm from J4, because J4's shell is the sensor domain. A USB cable then goes to the laptop; a breakout without 5.1 kΩ CC resistors needs a USB-A-to-C cable.
2. **The board on the bus:** the KNX plug in J2, as installed. The bus powers U3; USB doesn't.
3. **Put U3 into DFU mode:**
   - **Running board:** hold SW1 for **3–10 s**. D5 blinks while you hold it. Release. D5 blinks fast for a second, the top-up valve (if used) gets an OFF telegram, then U3 restarts into DFU. Holding over 10 s is a **factory reset**, so let go in time.
   - **New board (blank chip):** it is in DFU by itself as soon as the bus powers it.
   - **A board that doesn't respond:** unplug the KNX plug, wait 10 s, hold SW1, plug in again while holding, then release.

   The boot manager blinks D5 three times on the way into DFU. D5 may stay lit or go dark in DFU; the reliable sign is the laptop seeing "STM32 BOOTLOADER".
4. **Windows, first time only: the driver.** With U3 in DFU, run `windows\install-driver-zadig.cmd` and allow administrator rights. In Zadig:
   1. Options → List All Devices;
   2. pick **STM32 BOOTLOADER** (USB ID **0483 DF11**);
   3. driver **WinUSB**;
   4. Install Driver (or Replace Driver).

   Close Zadig. If STM32CubeProgrammer installed ST's own DFU driver on this PC, Zadig replaces it, after which this kit works and CubeProgrammer's DFU may not.
5. **Run the script:** `flash-update` for a normal update, or `flash-blank-chip` for a new board. It:
   1. checks the image's checksum;
   2. waits until U3 is in DFU;
   3. reads U3's boot manager, and refuses an update without one or a blank-chip flash over one, unless you type YES;
   4. flashes (about 10–20 s; don't unplug anything);
   5. starts the firmware and checks that U3 doesn't come back as a DFU device.

   U3's boot manager verifies the application's CRC at every start, and a bad image sends it straight back to DFU. So **"SUCCESS"** means the new firmware is running. The output is saved in `logs/`.
6. **Afterwards:**
   - Unplug USB. The board keeps running.
   - After `flash-blank-chip`: in ETS, program the individual address (a short press on SW1 turns on programming mode, D5 lights), then download the application. The product file is in `ets/` (`MANIFEST.txt` has its version).
   - After `flash-update`: nothing, unless `MANIFEST.txt` says the ETS application version changed; then download the application from ETS again.

## If something goes wrong

| What you see | What to do |
| --- | --- |
| "No STM32 in DFU mode found" | Step 3 again. On Windows, also step 4 (Device Manager shows "STM32 BOOTLOADER" with a warning sign when the driver is missing). Try another cable; check the breakout's D−/D+ |
| "FLASH FAILED" | Nothing is lost. U3 stays in DFU (or the boot manager returns it there, because the CRC fails). Run the script again |
| "U3 is back in the bootloader" | Release SW1 and run the script again. If it repeats with `flash-update` on a new board, use `flash-blank-chip` |
| Power cut during `flash-update` | Harmless. Power up again, U3 comes up in DFU (a broken application fails the CRC), and you flash again |
| Power cut during `flash-blank-chip` | Usually harmless. Only a cut in the ~10 ms while the boot manager page is written can leave a chip that needs an SWD probe (TP1 SWDIO, TP2 SWCLK, TP5 GND) |

**Never:** a mass or full-chip erase, writing option bytes, or a reset pulse on a blank chip. The scripts do none of these, and they never touch the ETS configuration or the calibration (flash bank 2).

## The log (optional)

U3 writes a text log on test pad TP7 (with TP5 as ground): 115200 baud, 8N1, at every start (version, reset cause, KNX serial, "application OK") and every 10 s (frequency, level, clock source). It needs an **isolated** USB-UART adapter, or the laptop on battery as above. Any serial terminal works, e.g. PuTTY on Windows or `screen /dev/tty.usbserial-* 115200` on macOS.

## Without the scripts

```
dfu-util -d 0483:df11 -a 0 -s 0x08001000:leave -D firmware/garantia-knx-u3-app.bin     # update
dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D firmware/garantia-knx-u3-full.bin    # blank chip only
```

STM32CubeProgrammer also works: USB, open the matching `.hex`, tick "Run after programming", Download.
