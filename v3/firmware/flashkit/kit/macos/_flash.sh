#!/bin/bash
# _flash.sh MODE   (MODE = update, blank or list; run through the .command files next to it; see ../README.md)
# Flashes U3 of garantia-knx-direct over the STM32 ROM bootloader's USB DFU with dfu-util.
set -u
cd "$(dirname "$0")" || exit 1
HERE="$(pwd)"
KIT="$(cd .. && pwd)"
DEV="0483:df11"
MODE="${1:-}"
mkdir -p "$KIT/logs"
LOG="$KIT/logs/$MODE-$(date +%Y%m%d-%H%M%S).txt"
TMPD="$(mktemp -d)"

finish() {
    rm -rf "$TMPD"
    echo
    read -r -p "Press Return to close. " _
    exit "$1"
}

# dfu-util: Homebrew's native build first (arm64 on Apple Silicon: brew install dfu-util), then the bundled
# x86_64 build, which runs under Rosetta 2 with its own libusb (tools/darwin-x86_64).
DFU=""
for c in "$(command -v dfu-util 2>/dev/null)" /opt/homebrew/bin/dfu-util /usr/local/bin/dfu-util; do
    if [ -n "$c" ] && [ -x "$c" ]; then DFU="$c"; break; fi
done
if [ -z "$DFU" ]; then
    B="$HERE/tools/darwin-x86_64"
    if [ "$(uname -m)" = "arm64" ] && ! /usr/bin/arch -x86_64 /usr/bin/true 2>/dev/null; then
        echo "dfu-util isn't installed, and Rosetta 2 (needed for the bundled copy) isn't either."
        echo "Install one of them once, with internet:"
        echo "  brew install dfu-util          (recommended, native)"
        echo "  softwareupdate --install-rosetta --agree-to-license"
        finish 1
    fi
    xattr -dr com.apple.quarantine "$B" 2>/dev/null
    chmod +x "$B/dfu-util" 2>/dev/null
    export DYLD_FALLBACK_LIBRARY_PATH="$B"
    DFU="$B/dfu-util"
    echo "Using the bundled dfu-util (x86_64 under Rosetta 2). 'brew install dfu-util' is the better choice."
fi

case "$MODE" in
    update) IMG="garantia-knx-u3-app.bin";  ADDR="0x08001000" ;;
    blank)  IMG="garantia-knx-u3-full.bin"; ADDR="0x08000000" ;;
    list)   "$DFU" -l; finish 0 ;;
    *)      echo "Unknown mode '$MODE'. Use flash-update.command, flash-blank-chip.command or list-devices.command."
            finish 1 ;;
esac

echo
echo "=== garantia-knx U3: $MODE with $IMG at $ADDR ==="
echo "dfu-util: $DFU"
echo

# ---- 1. The image is the one the kit was built with
EXP="$(awk -v f="$IMG" '$2 == f { print $1 }' "$KIT/firmware/SHA256SUMS")"
GOT="$(shasum -a 256 "$KIT/firmware/$IMG" 2>/dev/null | awk '{ print $1 }')"
if [ -z "$EXP" ]; then
    echo "No checksum for $IMG in firmware/SHA256SUMS. The kit is incomplete."
    finish 1
fi
if [ "$EXP" != "$GOT" ]; then
    echo "Checksum MISMATCH for firmware/$IMG. The file is damaged: copy the kit again."
    finish 1
fi
echo "Image checksum OK."

# ---- 2. U3 is in DFU mode
while ! "$DFU" -l 2>&1 | grep -q "\[$DEV\]"; do
    echo
    echo "No STM32 in DFU mode found. Put U3 into DFU, then press Return here:"
    echo "  - running board: hold SW1 for 3-10 s (D5 blinks) and release it;"
    echo "  - new or broken board: hold SW1 while bus power comes on (blank chips go to DFU by themselves)."
    echo "Ctrl+C stops."
    read -r _
done
echo "U3 found in DFU mode."

# ---- 3. Boot manager check: an update needs one, a blank-chip flash shouldn't be used over one
HAVEBM=0
"$DFU" -d "$DEV" -a 0 -s 0x08000100:16 -U "$TMPD/bootdesc.bin" >/dev/null 2>&1
if [ "$(head -c 4 "$TMPD/bootdesc.bin" 2>/dev/null)" = "U3BM" ]; then HAVEBM=1; fi
if [ "$MODE" = "update" ] && [ "$HAVEBM" = 0 ]; then
    echo
    echo "This chip has no garantia boot manager (a new board?). Use flash-blank-chip.command instead."
    finish 1
fi
if [ "$MODE" = "blank" ] && [ "$HAVEBM" = 1 ]; then
    echo
    echo "This chip already has the boot manager. For a normal update use flash-update.command:"
    echo "rewriting the boot manager risks a board that only an SWD probe can recover if power fails."
    read -r -p "Type YES to rewrite it anyway: " ANS
    [ "$ANS" = "YES" ] || finish 1
fi

# ---- 4. Flash and leave DFU
echo
echo "Flashing, about 10-20 s. Don't unplug USB or the bus ..."
"$DFU" -d "$DEV" -a 0 -s "$ADDR:leave" -D "$KIT/firmware/$IMG" 2>&1 | tee "$LOG"
if ! grep -q "File downloaded successfully" "$LOG"; then
    echo
    echo "FLASH FAILED: the download didn't complete. U3 stays in DFU; run this script again."
    finish 1
fi

# ---- 5. U3 must not come back as a DFU device: the boot manager checks the application's CRC at every
#      start and returns to DFU if it is bad.
echo
echo "Download complete. Waiting for U3 to start ..."
sleep 6
if "$DFU" -l 2>&1 | grep -q "\[$DEV\]"; then
    echo
    echo "FAILED: U3 is back in the bootloader, so it rejected the new application (or SW1 is still held)."
    echo "Release SW1 and run this script again."
    finish 1
fi
echo
echo "SUCCESS: the new firmware is running (U3 left DFU and didn't come back)."
[ "$MODE" = "blank" ] && echo "Next: in ETS, program the individual address (short press on SW1) and download the application."
echo "Log: $LOG"
finish 0
